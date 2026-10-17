// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: FileTransform without the COM/ActiveX plugin
// system. Plugin pipelines are parsed and resolved as upstream does, and
// their line-expression items ("le:...") run as unpackers and prediffers,
// as WinMerge runs them; an item that names a plugin is refused, there
// being no plugins on this platform. The pipeline parsing, the line
// provider and the expression passes follow WinMerge's FileTransform.cpp
// (GPL-2.0-or-later, © the WinMerge contributors).
#ifndef _WIN32

#include "pch.h"
#include "FileTransform.h"
#include <optional>
#include <tuple>
#include <Poco/Exception.h>
#include "MergeApp.h"
#include "unicoder.h"
#include "UniFile.h"
#include "TFile.h"
#include "Environment.h"
#include "paths.h"
#include "Logger.h"
#include "DiffContext.h"
#include "DiffItem.h"
#include "OptionsMgr.h"
#include "OptionsDef.h"
#include "codepage_detect.h"
#include "FilterEngine/FilterExpression.h"
#include "FilterEngine/ILineDataProvider.h"
#include "FilterErrorMessages.h"
#include "TableProps.h"

namespace FileTransform
{

bool AutoUnpacking = false;
bool AutoPrediffing = false;

/**
 * Convert a file from the given codepage to UTF-8 in a temp copy,
 * replacing the Win32/mlang-driven multiformatText path.
 */
bool AnyCodepageToUTF8(int codepage, String & filepath, bool bMayOverwrite)
{
	if (codepage == ucr::CP_UTF_8)
		return true; // nothing to do

	String tempDir = env::GetTemporaryPath();
	if (tempDir.empty())
		return false;
	String tempFilepath = env::GetTemporaryFileName(tempDir, _T("_W3"));
	if (tempFilepath.empty())
		return false;

	UniMemFile fileIn;
	UniStdioFile fileOut;
	if (!fileIn.OpenReadOnly(filepath))
		return false;
	fileIn.ReadBom();
	if (!fileIn.HasBom())
	{
		fileIn.SetUnicoding(ucr::NONE);
		fileIn.SetCodepage(codepage);
	}
	if (!fileOut.OpenCreate(tempFilepath))
	{
		fileIn.Close();
		return false;
	}
	fileOut.SetUnicoding(ucr::UTF8);
	fileOut.SetBom(false);

	String line, eol;
	bool lossy = false;
	while (fileIn.ReadString(line, eol, &lossy))
	{
		fileOut.WriteString(line);
		fileOut.WriteString(eol);
	}
	fileIn.Close();
	fileOut.Close();

	if (bMayOverwrite)
	{
		try
		{
			TFile(filepath).remove();
		}
		catch (...)
		{
		}
	}
	filepath = tempFilepath;
	return true;
}

} // namespace FileTransform

namespace
{

/** What the pipeline resolves to: a plugin (none here) or the line
    expressions of one pass, the files it applies to, its arguments, and
    whether it works on a file. */
using PluginTuple = std::tuple<PluginInfo*, std::vector<String>, uint8_t, std::vector<String>, bool>;

std::pair<String, uint8_t> parseNameAndTargetFlags(const String& token)
{
	String name;
	uint8_t targetFlags = 0;
	const auto pos = token.find_first_of(':');
	if (pos != String::npos)
	{
		name = token.substr(0, pos);
		targetFlags = 0;
		for (size_t i = pos + 1; i < token.length(); ++i)
		{
			const tchar_t ch = token[i];
			if (ch >= '1' && ch <= '3')
				targetFlags |= 1 << (ch - '1');
		}
		if (targetFlags == 0)
			targetFlags = 0xff;
	}
	else
	{
		name = token;
		targetFlags = 0xff;
	}
	return { name, targetFlags };
}

String makeTargetsPrefix(uint8_t targetFlags)
{
	if (targetFlags == 0xff)
		return _T("");
	std::vector<String> targets;
	for (int pos = 0; pos < 3; ++pos)
	{
		if (targetFlags & (1 << pos))
		{
			tchar_t ch = '1' + static_cast<tchar_t>(pos);
			targets.push_back(String(&ch, 1));
		}
	}
	return _T(":") + strutils::join(targets.begin(), targets.end(), _T(","));
}

inline bool isTargetInFlags(int target, unsigned targetFlags)
{
	return ((1 << target) & targetFlags) != 0;
}

/** ResolvePluginPipeline without plugins: the line expressions gathered as
    upstream gathers them (consecutive ones for the same files make one
    pass), <None> and <Automatic> resolving to nothing (no plugin is there
    for an automatic choice to find), and a plugin by name refused. */
bool resolvePluginPipeline(const std::vector<PluginForFile::PipelineItem>& result, bool bReverse,
	std::vector<PluginTuple>& plugins, String* pPluginPipelineResolved, String& errorMessage)
{
	std::vector<PluginForFile::PipelineItem> pipelineResolved;
	for (size_t i = 0; i < result.size(); ++i)
	{
		const auto& pipelineItem = result[i];
		if (std::holds_alternative<PluginForFile::FilterExpressionPipelineItem>(pipelineItem))
		{
			const auto& [targetFlags, expression] = std::get<PluginForFile::FilterExpressionPipelineItem>(pipelineItem);
			if (i == 0 || !std::holds_alternative<PluginForFile::FilterExpressionPipelineItem>(result[i - 1]) ||
				std::get<PluginForFile::FilterExpressionPipelineItem>(result[i - 1]).targetFlags != targetFlags)
			{
				pipelineResolved.push_back(PluginForFile::FilterExpressionPipelineItem{ targetFlags, expression });
				plugins.insert(bReverse ? plugins.begin() : plugins.end(),
					PluginTuple{ nullptr, { expression }, targetFlags, {}, true });
			}
			else
			{
				auto& lastPlugin = bReverse ? plugins.front() : plugins.back();
				std::get<1>(lastPlugin).push_back(expression);
			}
			continue;
		}
		const String& pluginName = PluginForFile::GetPluginPipelineItem(pipelineItem).name;
		if (pluginName == _T("<None>") || pluginName == _("<None>") ||
			pluginName == _T("<Automatic>") || pluginName == _("<Automatic>"))
			continue;
		errorMessage = _("Plugins are not available on this platform yet");
		return false;
	}
	if (pPluginPipelineResolved)
		*pPluginPipelineResolved = PluginForFile::MakePluginPipeline(pipelineResolved);
	return true;
}

class VectorLineDataProvider : public ILineDataProvider
{
public:
	VectorLineDataProvider(std::vector<std::string> lines, const FileTextEncoding& encoding, const TableProps& tableProps)
		: m_lines(std::move(lines)), m_encoding(encoding), m_tableProps(tableProps) {}
	int GetLineCount() const override
	{
		return static_cast<int>(m_lines.size());
	}
	std::string GetLine(int pane, int lineIndex) const override
	{
		(void)pane;
		if (!valid(lineIndex))
			return {};
		auto& line = m_lines[lineIndex];
		return line.substr(0, line.size() - GetEolLength(line));
	}
	int GetColumnCount(int pane, int lineIndex) const override
	{
		(void)pane;
		if (!valid(lineIndex))
			return 0;
		auto& line = m_lines[lineIndex];
		int columnCount = 0;
		bool inQuote = false;
		for (const char ch : line)
		{
			if (ch == m_tableProps.quote)
				inQuote = !inQuote;
			else if (!inQuote && ch == m_tableProps.delimiter)
				columnCount++;
		}
		return columnCount + 1;
	}
	std::string GetColumn(int pane, int lineIndex, int columnIndex) const override
	{
		(void)pane;
		if (!valid(lineIndex))
			return {};
		auto& line = m_lines[lineIndex];
		int columnCount = 0;
		bool inQuote = false;
		std::string column;
		const int length = static_cast<int>(line.size()) - GetEolLength(line);
		for (int i = 0; i < length && columnCount <= columnIndex; ++i)
		{
			const char ch = line[i];
			if (columnCount == columnIndex && (inQuote || ch != m_tableProps.delimiter))
				column += ch;
			if (ch == m_tableProps.quote)
				inQuote = !inQuote;
			else if (!inQuote && ch == m_tableProps.delimiter)
				++columnCount;
		}
		return column;
	}
	std::string GetColumns(int pane, int lineIndex, const std::vector<int>& columns) const override
	{
		if (!valid(lineIndex))
			return {};
		if (!m_tableProps.istable)
			return columns.empty() ? std::string{} : GetLine(pane, lineIndex);
		const std::string delimiter = ucr::toUTF8(String(&m_tableProps.delimiter, 1));
		std::string result;
		for (size_t i = 0; i < columns.size(); ++i)
		{
			if (i > 0)
				result += delimiter;
			result += GetColumn(pane, lineIndex, columns[i]);
		}
		return result;
	}
	int GetRealLineNumber(int pane, int lineIndex) const override
	{
		(void)pane;
		return lineIndex;
	}
	unsigned GetLineFlags(int pane, int lineIndex) const override
	{
		(void)pane; (void)lineIndex;
		return 0;
	}
	unsigned GetLineEol(int pane, int lineIndex) const override
	{
		(void)pane;
		if (!valid(lineIndex))
			return ILineDataProvider::EOL_NONE;
		return GetEolType(m_lines[lineIndex]);
	}
	int GetEolLength(const std::string& line) const
	{
		const EOLFLAGS eolType = GetEolType(line);
		if (eolType == EOL_CRLF)
			return 2;
		else if (eolType == EOL_LF || eolType == EOL_CR)
			return 1;
		else
			return 0;
	}
	ILineDataProvider::EOLFLAGS GetEolType(const std::string& line) const
	{
		if (line.empty())
			return ILineDataProvider::EOL_NONE;
		if (line.back() == '\n')
		{
			if (line.size() >= 2 && line[line.size() - 2] == '\r')
				return ILineDataProvider::EOL_CRLF;
			return ILineDataProvider::EOL_LF;
		}
		if (line.back() == '\r')
			return ILineDataProvider::EOL_CR;
		return ILineDataProvider::EOL_NONE;
	}
	const std::string& GetFullLine(int pane, int lineIndex) const
	{
		(void)pane;
		return m_lines[lineIndex];
	}
	void SetLine(int pane, int lineIndex, const std::string& line)
	{
		(void)pane;
		if (!valid(lineIndex))
			return;
		const std::string& oldLine = m_lines[lineIndex];
		const std::string eol = oldLine.substr(oldLine.size() - GetEolLength(oldLine));
		m_lines[lineIndex] = line + eol;
	}
	const FileTextEncoding& GetEncoding() const
	{
		return m_encoding;
	}

private:
	bool valid(int lineIndex) const
	{
		return lineIndex >= 0 && lineIndex < static_cast<int>(m_lines.size());
	}

	std::vector<std::string> m_lines;
	FileTextEncoding m_encoding;
	TableProps m_tableProps;
};

std::unique_ptr<VectorLineDataProvider> CreateFileLineDataProvider(int target, const String& filepath, const PluginPipelineContext& context)
{
	UniMemFile file;
	if (!file.OpenReadOnly(filepath))
		return nullptr;
	file.ReadBom();
	FileTextEncoding encoding;
	if (!file.HasBom())
	{
		int iGuessEncodingType = GetOptionsMgr()->GetInt(OPT_CP_DETECT);
		int64_t fileSize = file.GetFileSize();
		encoding = codepage_detect::Guess(
			paths::FindExtension(filepath), file.GetBase(), static_cast<size_t>(
				fileSize < static_cast<int64_t>(codepage_detect::BufSize) ?
				fileSize : static_cast<int64_t>(codepage_detect::BufSize)),
			iGuessEncodingType);
		file.SetCodepage(encoding.m_codepage);
	}
	else
	{
		encoding.SetUnicoding(file.GetUnicoding());
		encoding.m_bom = true;
	}
	std::vector<std::string> lines;
	bool linesToRead = true;
	do
	{
		bool lossy;
		String line, eol;
		linesToRead = file.ReadString(line, eol, &lossy);
		if (!linesToRead)
			break;
		lines.push_back(ucr::toUTF8(line + eol));
	} while (linesToRead);
	const auto filenames = strutils::split(context.filteredFilenames, '|');
	const String tablePropertiesPath = target >= 0 && static_cast<size_t>(target) < filenames.size()
		? String(filenames[target].data(), filenames[target].size()) : filepath;
	auto tableProps = context.tableProps.has_value() ? *context.tableProps : MakeTablePropertiesByFileName(tablePropertiesPath);
	return std::make_unique<VectorLineDataProvider>(std::move(lines), encoding, tableProps);
}

std::pair<std::unique_ptr<CDiffContext>, std::unique_ptr<DIFFITEM>> CreateDiffItem(const String& filepath, const FileTextEncoding& encoding)
{
	PathContext paths;
	auto pdi = std::make_unique<DIFFITEM>();
	pdi->diffcode.diffcode = DIFFCODE::TEXT | DIFFCODE::FILE;
	pdi->diffcode.setSideFlag(0);
	paths.SetPath(0, paths::GetParentPath(filepath));
	pdi->diffFileInfo[0].SetFile(paths::FindFileName(filepath));
	pdi->diffFileInfo[0].Update(filepath);
	pdi->diffFileInfo[0].encoding = encoding;
	return { std::make_unique<CDiffContext>(paths, 0), std::move(pdi) };
}

/** The parsed expressions of one pass with their evaluation contexts, and
    the diff context they point into, which has to outlive them. */
struct FilterExpressionSet
{
	std::vector<FilterExpression> filterExpressions;
	std::vector<FilterEvalContext> evalContexts;
	std::unique_ptr<CDiffContext> diffContext;
	std::unique_ptr<DIFFITEM> diffItem;
};

std::optional<FilterExpressionSet> BuildFilterExpressionSet(const String& filepath, const FileTextEncoding& encoding,
	const std::vector<String>& expressions, const VectorLineDataProvider& lineDataProvider, String& errorMessage)
{
	FilterExpressionSet set;
	set.filterExpressions.reserve(expressions.size());
	set.evalContexts.reserve(expressions.size());
	auto [diffContext, diffItem] = CreateDiffItem(filepath, encoding);
	set.diffContext = std::move(diffContext);
	set.diffItem = std::move(diffItem);
	for (const auto& expression : expressions)
	{
		set.filterExpressions.emplace_back(ucr::toUTF8(expression));
		auto& lastFilterExpression = set.filterExpressions.back();
		lastFilterExpression.SetDiffContext(set.diffContext.get());
		if (!lastFilterExpression.Parse())
		{
			errorMessage = FormatFilterErrorSummary(lastFilterExpression);
			return std::nullopt;
		}
		set.evalContexts.emplace_back();
		auto& lastEvalContext = set.evalContexts.back();
		lastEvalContext.provider = &lineDataProvider;
		lastEvalContext.expr = &lastFilterExpression;
		lastEvalContext.di = set.diffItem.get();
	}
	return set;
}

bool ApplyFilterExpressionsToLines(VectorLineDataProvider& lineDataProvider,
	std::vector<FilterExpression>& filterExpressions, std::vector<FilterEvalContext>& evalContexts, String& errorMessage)
{
	const int lineCount = lineDataProvider.GetLineCount();
	std::vector<FilterSharedContext> sharedContexts(filterExpressions.size());
	for (int i = 0; i < lineCount; ++i)
	{
		for (size_t j = 0; j < filterExpressions.size(); ++j)
		{
			auto& filterExpression = filterExpressions[j];
			auto& evalContext = evalContexts[j];
			evalContext.lineIndex = i;
			evalContext.sharedContext = &sharedContexts[j];
			std::string line = filterExpression.TransformLine(evalContext);
			if (filterExpression.errorCode != FILTER_ERROR_NO_ERROR)
			{
				errorMessage = ucr::toTString(filterExpression.errorMessage);
				return false;
			}
			lineDataProvider.SetLine(0, i, line);
		}
	}
	return true;
}

String ApplyFilterExpressionsToFile(int target, const String& filepath, const PluginPipelineContext& context,
	bool bMayOverwrite, const std::vector<String>& expressions, String& errorMessage)
{
	auto lineDataProvider = CreateFileLineDataProvider(target, filepath, context);
	if (!lineDataProvider)
	{
		errorMessage = strutils::format_string2(_("Cannot open file\n%1\n\n%2"), filepath, GetSysError());
		return _T("");
	}
	auto exprSet = BuildFilterExpressionSet(filepath, lineDataProvider->GetEncoding(), expressions, *lineDataProvider, errorMessage);
	if (!exprSet)
		return _T("");
	if (!ApplyFilterExpressionsToLines(*lineDataProvider, exprSet->filterExpressions, exprSet->evalContexts, errorMessage))
		return _T("");

	String tempPath = filepath;
	if (!bMayOverwrite)
	{
		// mkstemp creates the name it reserves, and the file written is
		// that name with the extension: the reserved one goes (WinMerge's
		// temporary folder, where it stays, goes as a whole at the end)
		const String reserved = env::GetTemporaryFileName(env::GetTemporaryPath(), _T("WM"));
		tempPath = reserved + paths::FindExtension(filepath);
		if (!reserved.empty())
		{
			try
			{
				TFile(reserved).remove();
			}
			catch (Poco::Exception& e)
			{
				RootLogger::Error(e.displayText());
			}
		}
	}
	UniStdioFile file;
	if (!file.OpenCreateUtf8(tempPath))
	{
		errorMessage = strutils::format_string2(_("Cannot create file\n%1\n\n%2"), tempPath, GetSysError());
		return _T("");
	}
	const int lineCount = lineDataProvider->GetLineCount();
	for (int i = 0; i < lineCount; ++i)
		file.WriteString(ucr::toTString(lineDataProvider->GetFullLine(0, i)));
	return tempPath;
}

bool ValidateExpressions(const std::vector<String>& expressions, String& errorMessage)
{
	if (!expressions.empty())
	{
		VectorLineDataProvider lineDataProvider({}, FileTextEncoding(), TableProps());
		String filepath;
		BuildFilterExpressionSet(filepath, lineDataProvider.GetEncoding(), expressions, lineDataProvider, errorMessage);
	}
	return errorMessage.empty();
}

} // namespace

// --- PluginForFile / PackingInfo / PrediffingInfo ---

std::vector<PluginForFile::PipelineItem> PluginForFile::ParsePluginPipeline(String& errorMessage) const
{
	return ParsePluginPipeline(m_PluginPipeline, errorMessage);
}

std::vector<PluginForFile::PipelineItem> PluginForFile::ParsePluginPipeline(const String& pluginPipeline, String& errorMessage)
{
	std::vector<PluginForFile::PipelineItem> result;
	errorMessage.clear();

	// Split the pipeline into items by '|', ignoring '|' inside quotes.
	std::vector<String> items;
	String item;
	bool inQuotes = false;
	tchar_t quoteChar = 0;

	{
		const tchar_t* p = pluginPipeline.c_str();
		while (tc::istspace(*p))
			++p;

		if (!*p)
			return result;
	}

	for (const tchar_t* p = pluginPipeline.c_str(); *p; ++p)
	{
		if (inQuotes)
		{
			item += *p;
			if (*p == quoteChar)
			{
				if (*(p + 1) == quoteChar)
					item += *++p;
				else
					inQuotes = false;
			}
		}
		else
		{
			if (*p == '"' || *p == '\'')
			{
				inQuotes = true;
				quoteChar = *p;
				item += *p;
			}
			else if (*p == '|')
			{
				items.push_back(item);
				item.clear();
			}
			else
			{
				item += *p;
			}
		}
	}

	if (inQuotes)
	{
		errorMessage = strutils::format_string1(_("Missing quote in plugin pipeline: %1"), pluginPipeline);
		return result;
	}

	items.push_back(item);

	for (const auto& pipelineItem : items)
	{
		const tchar_t* p = pipelineItem.c_str();
		while (tc::istspace(*p))
			++p;

		if (!*p)
		{
			errorMessage = strutils::format_string1(_("Missing plugin name in pipeline: %1"), pluginPipeline);
			return result;
		}

		uint8_t targetFlags = 0xff;
		String token, name;

		// Line expression:
		//   le:pane:expression
		if (tc::tcsncmp(p, _T("le:"), 3) == 0)
		{
			const tchar_t* expression = p + 3;
			p += 3;

			const tchar_t* q = expression;
			while (*q == '1' || *q == '2' || *q == '3' || *q == ',')
				++q;

			if (q != expression && *q == ':')
			{
				const String target(expression, q);

				targetFlags = 0;
				for (const tchar_t ch : target)
				{
					if (ch >= '1' && ch <= '3')
						targetFlags |= 1 << (ch - '1');
				}

				if (targetFlags == 0)
					targetFlags = 0xff;

				expression = q + 1;
			}

			while (tc::istspace(*expression))
				++expression;

			if (!*expression)
			{
				errorMessage = strutils::format_string1(_("Missing line expression in pipeline: %1"), pluginPipeline);
				return result;
			}

			result.push_back(FilterExpressionPipelineItem{ targetFlags, expression });
			continue;
		}

		// Parse a normal plugin pipeline item.
		inQuotes = false;
		quoteChar = 0;
		std::vector<String> args;

		while (*p)
		{
			if (!inQuotes)
			{
				if (*p == '"' || *p == '\'')
				{
					inQuotes = true;
					quoteChar = *p;
				}
				else if (tc::istspace(*p))
				{
					if (!token.empty())
					{
						if (name.empty())
							std::tie(name, targetFlags) = parseNameAndTargetFlags(token);
						else
							args.push_back(token);
						token.clear();
					}

					while (tc::istspace(*p))
						++p;
					continue;
				}
				else
					token += *p;
			}
			else
			{
				if (*p == quoteChar)
				{
					if (*(p + 1) == quoteChar)
					{
						token += *p;
						++p;
					}
					else
					{
						inQuotes = false;
					}
				}
				else
				{
					token += *p;
				}
			}
			++p;
		}

		if (inQuotes)
		{
			errorMessage = strutils::format_string1(_("Missing quote in plugin pipeline: %1"), pluginPipeline);
			return result;
		}

		if (!token.empty())
		{
			if (name.empty())
			{
				std::tie(name, targetFlags) = parseNameAndTargetFlags(token);
			}
			else
			{
				args.push_back(token);
			}
		}

		if (name.empty())
		{
			errorMessage = strutils::format_string1(_("Missing plugin name in pipeline: %1"), pluginPipeline);
			return result;
		}

		result.push_back(PluginPipelineItem{ targetFlags, name, args, quoteChar });
	}

	return result;
}

String PluginForFile::MakePluginPipeline(const std::vector<PluginForFile::PipelineItem>& list)
{
	size_t i = 0;
	String pipeline;
	for (const auto& pipelineItem : list)
	{
		if (std::holds_alternative<FilterExpressionPipelineItem>(pipelineItem))
		{
			const auto& lineExpressionItem = std::get<FilterExpressionPipelineItem>(pipelineItem);
			pipeline += _T("le");
			if (lineExpressionItem.targetFlags != 0xff)
				pipeline += makeTargetsPrefix(lineExpressionItem.targetFlags);
			pipeline += _T(":") + lineExpressionItem.expression;
		}
		else
		{
			const auto& pluginItem = std::get<PluginPipelineItem>(pipelineItem);
			if (pluginItem.quoteChar && pluginItem.name.find_first_of(_T(" '\"")) != String::npos)
			{
				String nameQuoted = pluginItem.name;
				strutils::replace(nameQuoted, String(1, pluginItem.quoteChar), String(2, pluginItem.quoteChar));
				pipeline += strutils::format(_T("%c%s%c"), pluginItem.quoteChar, nameQuoted, pluginItem.quoteChar);
			}
			else
			{
				pipeline += pluginItem.name;
			}
			pipeline += makeTargetsPrefix(pluginItem.targetFlags);
			for (const auto& arg : pluginItem.args)
			{
				if (pluginItem.quoteChar)
				{
					String argQuoted = arg;
					strutils::replace(argQuoted, String(1, pluginItem.quoteChar), String(2, pluginItem.quoteChar));
					pipeline += strutils::format(_T(" %c%s%c"), pluginItem.quoteChar, argQuoted, pluginItem.quoteChar);
				}
				else
				{
					pipeline += _T(" ") + arg;
				}
			}
		}
		if (i < list.size() - 1)
			pipeline += _T("|");
		i++;
	}
	return pipeline;
}

String PluginForFile::MakeArguments(const std::vector<String>& args, const std::vector<StringView>& variables)
{
	String newstr;
	size_t i = 0;
	for (const auto& arg : args)
	{
		String newarg;
		for (const tchar_t* p = arg.c_str(); *p; ++p)
		{
			if (*p == '%' && *(p + 1) != 0)
			{
				++p;
				tchar_t c = *p;
				if (c == '%')
				{
					newarg += '%';
				}
				else if (c >= '1' && c <= '9')
				{
					if (static_cast<size_t>(c - '1') < variables.size())
						newarg += strutils::to_str(variables[(c - '1')]);
				}
				else
				{
					newarg += *(p - 1);
					newarg += c;
				}
			}
			else
			{
				newarg += *p;
			}
		}
		if (newarg.empty())
		{
			newstr += _T("\"\"");
		}
		else if (newarg.find_first_of(_T(" \t\"")) != String::npos)
		{
			strutils::replace(newarg, _T("\""), _T("\"\""));
			newstr += _T("\"") + newarg + _T("\"");
		}
		else
		{
			newstr += newarg;
		}
		if (i < args.size() - 1)
			newstr += ' ';
		i++;
	}
	return newstr;
}

bool PluginForFile::Validate(String& errorMessage) const
{
	ParsePluginPipeline(errorMessage);
	return errorMessage.empty();
}

bool PackingInfo::GetPackUnpackPlugin(const String& filteredFilenames, bool bUrl, bool bReverse,
	std::vector<std::tuple<PluginInfo*, std::vector<String>, uint8_t, std::vector<String>, bool>>& plugins,
	String *pPluginPipelineResolved, String& errorMessage, int stack) const
{
	// no URL_PACK_UNPACK plugin to pick automatically, and no alias to
	// expand: filteredFilenames, bUrl and stack serve those
	(void)filteredFilenames; (void)bUrl; (void)stack;
	auto result = PluginForFile::ParsePluginPipeline(m_PluginPipeline, errorMessage);
	if (!errorMessage.empty())
		return false;
	return resolvePluginPipeline(result, bReverse, plugins, pPluginPipelineResolved, errorMessage);
}

bool PackingInfo::Unpacking(int target, std::vector<int> * handlerSubcodes, String & filepath,
	const PluginPipelineContext& context)
{
	if (handlerSubcodes)
		handlerSubcodes->clear();

	// no handler : return true
	bool bUrl = paths::IsURL(filepath);
	if (m_PluginPipeline.empty() && !bUrl)
		return true;

	// control value
	String errorMessage;
	std::vector<PluginTuple> plugins;
	if (!GetPackUnpackPlugin(context.filteredFilenames, bUrl, false, plugins, &m_PluginPipeline, errorMessage))
	{
		AppErrorMessageBox(errorMessage);
		return false;
	}

	if (m_bWebBrowser && m_PluginPipeline.empty())
		return true;

	// a pass of line expressions is all a plugin pipeline can hold here
	for (auto& [plugin, expressions, targetFlags, args, bWithFile] : plugins)
	{
		if (!isTargetInFlags(target, targetFlags))
			continue;

		filepath = ApplyFilterExpressionsToFile(target, filepath, context, false, expressions, errorMessage);
		if (!errorMessage.empty())
		{
			AppErrorMessageBox(errorMessage);
			return false;
		}
	}
	return true;
}

bool PackingInfo::pack(int target, String & filepath, const String& dstFilepath,
	const std::vector<int>& handlerSubcodes, const std::vector<StringView>& variables) const
{
	// line expressions do not pack back: only the plugins of the pipeline
	// would, and a pipeline that resolves has none
	(void)target; (void)filepath; (void)handlerSubcodes; (void)variables;
	bool bUrl = paths::IsURL(dstFilepath);
	if (m_PluginPipeline.empty() && !bUrl)
		return true;

	String errorMessage;
	std::vector<PluginTuple> plugins;
	if (!GetPackUnpackPlugin(_T(""), bUrl, true, plugins, nullptr, errorMessage))
	{
		AppErrorMessageBox(errorMessage);
		return false;
	}
	return true;
}

bool PackingInfo::Packing(int target, const String& srcFilepath, const String& dstFilepath,
	const std::vector<int>& handlerSubcodes, const PluginPipelineContext& context) const
{
	String csTempFileName = srcFilepath;
	if (!pack(target, csTempFileName, dstFilepath, handlerSubcodes, context.variables))
		return false;
	try
	{
		if (!paths::IsURL(dstFilepath))
		{
			TFile file1(csTempFileName);
			file1.copyTo(dstFilepath);
			if (srcFilepath != csTempFileName)
				file1.remove();
		}
		return true;
	}
	catch (Poco::Exception& e)
	{
		RootLogger::Error(e.displayText());
		return false;
	}
}

bool PackingInfo::Validate(String& errorMessage) const
{
	std::vector<PluginTuple> plugins;
	if (!GetPackUnpackPlugin(_T(""), false, false, plugins, nullptr, errorMessage))
		return false;
	for (auto& [plugin, expressions, targetFlags, args, bWithFile] : plugins)
	{
		String errorMessage2;
		if (!ValidateExpressions(expressions, errorMessage2))
			errorMessage += (errorMessage.empty() ? _T("") : _T("\n")) + errorMessage2;
	}
	return errorMessage.empty();
}

String PackingInfo::GetUnpackedFileExtension(int target, const String& filteredFilenames,
	int& preferredWindowType) const
{
	// the extension and the window type come from the plugins
	(void)target; (void)filteredFilenames;
	preferredWindowType = -1;
	return String();
}

bool PrediffingInfo::GetPrediffPlugin(const String& filteredFilenames, bool bReverse,
	std::vector<std::tuple<PluginInfo*, std::vector<String>, uint8_t, std::vector<String>, bool>>& plugins,
	String* pPluginPipelineResolved, String& errorMessage, int stack) const
{
	(void)filteredFilenames; (void)stack;
	auto result = PluginForFile::ParsePluginPipeline(m_PluginPipeline, errorMessage);
	if (!errorMessage.empty())
		return false;
	return resolvePluginPipeline(result, bReverse, plugins, pPluginPipelineResolved, errorMessage);
}

bool PrediffingInfo::Prediffing(int target, String & filepath, bool bMayOverwrite,
	const PluginPipelineContext& context)
{
	// no handler : return true
	if (m_PluginPipeline.empty())
		return true;

	// control value
	String errorMessage;
	std::vector<PluginTuple> plugins;
	if (!GetPrediffPlugin(context.filteredFilenames, false, plugins, &m_PluginPipeline, errorMessage))
	{
		AppErrorMessageBox(errorMessage);
		return false;
	}

	// a pass of line expressions is all a plugin pipeline can hold here
	for (const auto& [plugin, expressions, targetFlags, args, bWithFile] : plugins)
	{
		if (!isTargetInFlags(target, targetFlags))
			continue;

		filepath = ApplyFilterExpressionsToFile(target, filepath, context, bMayOverwrite, expressions, errorMessage);
		if (!errorMessage.empty())
		{
			AppErrorMessageBox(errorMessage);
			return false;
		}
	}
	return true;
}

bool PrediffingInfo::Validate(String& errorMessage) const
{
	std::vector<PluginTuple> plugins;
	if (!GetPrediffPlugin(_T(""), false, plugins, nullptr, errorMessage))
		return false;
	for (auto& [plugin, expressions, targetFlags, args, bWithFile] : plugins)
	{
		String errorMessage2;
		if (!ValidateExpressions(expressions, errorMessage2))
			errorMessage += (errorMessage.empty() ? _T("") : _T("\n")) + errorMessage2;
	}
	return errorMessage.empty();
}

#endif // !_WIN32
