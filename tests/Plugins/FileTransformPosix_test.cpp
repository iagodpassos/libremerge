// SPDX-License-Identifier: GPL-2.0-or-later
// LibreMerge: the plugin pipelines of the POSIX FileTransform
// (engine/ports/file_transform_posix.cpp). ParsePluginPipeline and
// ReplaceVariable are upstream's cases (Plugins/Plugins_test.cpp), the ones
// that need no plugin; the rest are LibreMerge's: the line expressions a
// table comparison's "Ignore This Column" puts in the prediffer, run over
// a file, and a plugin by name, which is refused.
#include "pch.h"
#include <gtest/gtest.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "FileTransform.h"
#include "Environment.h"
#include "OptionsMgr.h"
#include "OptionsDef.h"
#include "TableProps.h"
#include "TFile.h"

namespace
{

	TEST(PluginsTest, ParsePluginPipeline)
	{
		String errorMessage;
		auto parseResult = PluginForFile::ParsePluginPipeline(_T(""), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(0, parseResult.size());
		String pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T(""), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T(" "), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(0, parseResult.size());
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T(""), pluginPipeline);


		parseResult = PluginForFile::ParsePluginPipeline(_T("<Automatic>"), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		auto* parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("<Automatic>"), parseResult0->name);
		EXPECT_EQ(0, parseResult0->args.size());
		EXPECT_EQ(0, parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("<Automatic>"), pluginPipeline);


		parseResult = PluginForFile::ParsePluginPipeline(_T(" <Automatic> "), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("<Automatic>"), parseResult0->name);
		EXPECT_EQ(0, parseResult0->args.size());
		EXPECT_EQ(0, parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("<Automatic>"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("'a b'"), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("a b"), parseResult0->name);
		EXPECT_EQ(0, parseResult0->args.size());
		EXPECT_EQ('\'', parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("'a b'"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("'a''b'"), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("a'b"), parseResult0->name);
		EXPECT_EQ(0, parseResult0->args.size());
		EXPECT_EQ('\'', parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("'a''b'"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("a' 'b:1"), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("a b"), parseResult0->name);
		EXPECT_EQ(0b001, parseResult0->targetFlags);
		EXPECT_EQ(0, parseResult0->args.size());
		EXPECT_EQ('\'', parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("'a b':1"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("a' '\"b\":1,3"), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("a b"), parseResult0->name);
		EXPECT_EQ(0b101, parseResult0->targetFlags);
		EXPECT_EQ(0, parseResult0->args.size());
		EXPECT_EQ('"', parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("\"a b\":1,3"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("\"a b"), errorMessage);
		EXPECT_TRUE(!errorMessage.empty());

		parseResult = PluginForFile::ParsePluginPipeline(_T("ExecFilterCommand dir c:\\"), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult0->name);
		EXPECT_EQ(_T("dir"), parseResult0->args[0]);
		EXPECT_EQ(_T("c:\\"), parseResult0->args[1]);
		EXPECT_EQ(0, parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("ExecFilterCommand dir c:\\"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T(" ExecFilterCommand  dir  c:\\ "), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult0->name);
		EXPECT_EQ(_T("dir"), parseResult0->args[0]);
		EXPECT_EQ(_T("c:\\"), parseResult0->args[1]);
		EXPECT_EQ(0, parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("ExecFilterCommand dir c:\\"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("ExecFilterCommand  'dir  c:\\ '"), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult0->name);
		EXPECT_EQ(_T("dir  c:\\ "), parseResult0->args[0]);
		EXPECT_EQ('\'', parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("ExecFilterCommand 'dir  c:\\ '"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("ExecFilterCommand  'dir  c:\\ ' "), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult0->name);
		EXPECT_EQ(_T("dir  c:\\ "), parseResult0->args[0]);
		EXPECT_EQ('\'', parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("ExecFilterCommand 'dir  c:\\ '"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T(" 'a b'  'dir  c:\\ ' "), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("a b"), parseResult0->name);
		EXPECT_EQ(_T("dir  c:\\ "), parseResult0->args[0]);
		EXPECT_EQ('\'', parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("'a b' 'dir  c:\\ '"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T(" 'a b'  dir  c:\\  "), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("a b"), parseResult0->name);
		EXPECT_EQ(_T("dir"), parseResult0->args[0]);
		EXPECT_EQ(_T("c:\\"), parseResult0->args[1]);
		EXPECT_EQ('\'', parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("'a b' 'dir' 'c:\\'"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T(" 'a b'  \"dir  c:\\ \" "), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(1, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("a b"), parseResult0->name);
		EXPECT_EQ(_T("dir  c:\\ "), parseResult0->args[0]);
		EXPECT_EQ('"', parseResult0->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("\"a b\" \"dir  c:\\ \""), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("ExecFilterCommand dir|MakeUpper"), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(2, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult0->name);
		EXPECT_EQ(_T("dir"), parseResult0->args[0]);
		EXPECT_EQ(0, parseResult0->quoteChar);
		auto* parseResult1 = &PluginForFile::GetPluginPipelineItem(parseResult[1]);
		EXPECT_EQ(_T("MakeUpper"), parseResult1->name);
		EXPECT_EQ(0, parseResult1->args.size());
		EXPECT_EQ(0, parseResult1->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("ExecFilterCommand dir|MakeUpper"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("ExecFilterCommand dir | MakeUpper "), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(2, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult0->name);
		EXPECT_EQ(_T("dir"), parseResult0->args[0]);
		EXPECT_EQ(0, parseResult0->quoteChar);
		parseResult1 = &PluginForFile::GetPluginPipelineItem(parseResult[1]);
		EXPECT_EQ(_T("MakeUpper"), parseResult1->name);
		EXPECT_EQ(0, parseResult1->args.size());
		EXPECT_EQ(0, parseResult1->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("ExecFilterCommand dir|MakeUpper"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T(" ExecFilterCommand dir | MakeUpper "), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(2, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult0->name);
		EXPECT_EQ(_T("dir"), parseResult0->args[0]);
		EXPECT_EQ(0, parseResult0->quoteChar);
		parseResult1 = &PluginForFile::GetPluginPipelineItem(parseResult[1]);
		EXPECT_EQ(_T("MakeUpper"), parseResult1->name);
		EXPECT_EQ(0, parseResult1->args.size());
		EXPECT_EQ(0, parseResult1->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("ExecFilterCommand dir|MakeUpper"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T(" ExecFilterCommand 'dir  c:\\ ' | MakeUpper "), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(2, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult0->name);
		EXPECT_EQ(_T("dir  c:\\ "), parseResult0->args[0]);
		EXPECT_EQ('\'', parseResult0->quoteChar);
		parseResult1 = &PluginForFile::GetPluginPipelineItem(parseResult[1]);
		EXPECT_EQ(_T("MakeUpper"), parseResult1->name);
		EXPECT_EQ(0, parseResult1->args.size());
		EXPECT_EQ(0, parseResult1->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("ExecFilterCommand 'dir  c:\\ '|MakeUpper"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("ExecFilterCommand 'dir c:\\'|ExecFilterCommand 'sort'"), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(2, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult0->name);
		EXPECT_EQ(_T("dir c:\\"), parseResult0->args[0]);
		EXPECT_EQ('\'', parseResult0->quoteChar);
		parseResult1 = &PluginForFile::GetPluginPipelineItem(parseResult[1]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult1->name);
		EXPECT_EQ(_T("sort"), parseResult1->args[0]);
		EXPECT_EQ('\'', parseResult1->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("ExecFilterCommand 'dir c:\\'|ExecFilterCommand 'sort'"), pluginPipeline);


		parseResult = PluginForFile::ParsePluginPipeline(_T(" ExecFilterCommand 'dir  c:\\ ' | ExecFilterCommand ' sort ' "), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(2, parseResult.size());
		parseResult0 = &PluginForFile::GetPluginPipelineItem(parseResult[0]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult0->name);
		EXPECT_EQ(_T("dir  c:\\ "), parseResult0->args[0]);
		EXPECT_EQ('\'', parseResult0->quoteChar);
		parseResult1 = &PluginForFile::GetPluginPipelineItem(parseResult[1]);
		EXPECT_EQ(_T("ExecFilterCommand"), parseResult1->name);
		EXPECT_EQ(_T(" sort "), parseResult1->args[0]);
		EXPECT_EQ('\'', parseResult1->quoteChar);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("ExecFilterCommand 'dir  c:\\ '|ExecFilterCommand ' sort '"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("le:replace(Line, \"\\s+\", \"\")|le:1:replace(Line, \"|\", \"\")|le:1,3:replace(Line, \"\\s+\", \"\")|SelectLines:1,3 'abc'"), errorMessage);
		EXPECT_TRUE(errorMessage.empty());
		EXPECT_EQ(4, parseResult.size());
		auto& lineExpressionItem0 = std::get<PluginForFile::FilterExpressionPipelineItem>(parseResult[0]);
		EXPECT_EQ(_T("replace(Line, \"\\s+\", \"\")"), lineExpressionItem0.expression);

		auto& lineExpressionItem1 = std::get<PluginForFile::FilterExpressionPipelineItem>(parseResult[1]);
		EXPECT_EQ(0b001, lineExpressionItem1.targetFlags);
		EXPECT_EQ(_T("replace(Line, \"|\", \"\")"), lineExpressionItem1.expression);

		auto& lineExpressionItem2 = std::get<PluginForFile::FilterExpressionPipelineItem>(parseResult[2]);
		EXPECT_EQ(0b101, lineExpressionItem2.targetFlags);
		EXPECT_EQ(_T("replace(Line, \"\\s+\", \"\")"), lineExpressionItem2.expression);
		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);

		auto* parseResult3 = &PluginForFile::GetPluginPipelineItem(parseResult[3]);
		EXPECT_EQ(_T("SelectLines"), parseResult3->name);
		EXPECT_EQ(_T("abc"), parseResult3->args[0]);
		EXPECT_EQ('\'', parseResult3->quoteChar);

		pluginPipeline = PluginForFile::MakePluginPipeline(parseResult);
		EXPECT_EQ(_T("le:replace(Line, \"\\s+\", \"\")|le:1:replace(Line, \"|\", \"\")|le:1,3:replace(Line, \"\\s+\", \"\")|SelectLines:1,3 'abc'"), pluginPipeline);

		parseResult = PluginForFile::ParsePluginPipeline(_T("|"), errorMessage);
		EXPECT_TRUE(!errorMessage.empty());

		parseResult = PluginForFile::ParsePluginPipeline(_T(" | "), errorMessage);
		EXPECT_TRUE(!errorMessage.empty());

		parseResult = PluginForFile::ParsePluginPipeline(_T("MakeUpper|"), errorMessage);
		EXPECT_TRUE(!errorMessage.empty());

		parseResult = PluginForFile::ParsePluginPipeline(_T("MakeUpper | "), errorMessage);
		EXPECT_TRUE(!errorMessage.empty());

		parseResult = PluginForFile::ParsePluginPipeline(_T("MakeUpper||"), errorMessage);
		EXPECT_TRUE(!errorMessage.empty());

		parseResult = PluginForFile::ParsePluginPipeline(_T("MakeUpper | | "), errorMessage);
		EXPECT_TRUE(!errorMessage.empty());
	}

	TEST(PluginsTest, ReplaceVariable)
	{
		String result;
		EXPECT_STREQ(_T(""), PluginForFile::MakeArguments(std::vector<String>{}, { _T("abc") }).c_str());
		EXPECT_STREQ(_T("\"\""), PluginForFile::MakeArguments(std::vector<String>{_T("")}, { _T("abc") }).c_str());
		EXPECT_STREQ(_T("%"), PluginForFile::MakeArguments(std::vector<String>{_T("%")}, {_T("abc")}).c_str());
		EXPECT_STREQ(_T("abc"), PluginForFile::MakeArguments(std::vector<String>{_T("%1")}, {_T("abc")}).c_str());
		EXPECT_STREQ(_T("%1"), PluginForFile::MakeArguments(std::vector<String>{_T("%%1")}, {_T("abc")}).c_str());
		EXPECT_STREQ(_T("%1a"), PluginForFile::MakeArguments(std::vector<String>{_T("%%1a")}, {_T("abc")}).c_str());
		EXPECT_STREQ(_T("\"\""), PluginForFile::MakeArguments(std::vector<String>{_T("%2")}, {_T("abc")}).c_str());
		EXPECT_STREQ(_T("%TIME%"), PluginForFile::MakeArguments(std::vector<String>{_T("%TIME%")}, {_T("abc")}).c_str());
		EXPECT_STREQ(_T("aaaabcaaa"), PluginForFile::MakeArguments(std::vector<String>{_T("aaa%1aaa")}, {_T("abc")}).c_str());
		EXPECT_STREQ(_T("abcdef"), PluginForFile::MakeArguments(std::vector<String>{_T("%1%2")}, { _T("abc"), _T("def") }).c_str());
	}



/** A file of the given text in the temporary folder, gone with the object. */
class ScratchFile
{
public:
	explicit ScratchFile(const std::string &text)
		: m_path(env::GetTemporaryFileName(env::GetTemporaryPath(), _T("LM")))
	{
		std::ofstream(m_path, std::ios::binary) << text;
	}
	~ScratchFile() { remove(m_path); }
	const String &path() const { return m_path; }

	static std::string read(const String &path)
	{
		std::ifstream in(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
	}
	static void remove(const String &path)
	{
		try
		{
			TFile(path).remove();
		}
		catch (...)
		{
		}
	}

private:
	String m_path;
};

/** The encoding guess of the application: valid UTF-8 reads as UTF-8. */
void guessUtf8()
{
	const int guess = (50001 << 16) | 1;
	GetOptionsMgr()->InitOption(OPT_CP_DETECT, guess);
	GetOptionsMgr()->SaveOption(OPT_CP_DETECT, guess);
}

const char kTable[] =
	"id,name,price\n"
	"1,ma\xc3\xa7\xc3\xa3,10\r\n"
	"2,\"pear, green\",20\n"
	"3\n";

PluginPipelineContext tableContext()
{
	PluginPipelineContext context;
	context.tableProps = TableProps{ true, ',', '"', true };
	return context;
}

/** The text of the file a pipeline leaves for the given file. */
std::string prediffed(const String &pipeline, int target, const std::string &text, bool *ok = nullptr)
{
	guessUtf8();
	ScratchFile file(text);
	String path = file.path();
	PrediffingInfo prediffer(pipeline);
	const bool done = prediffer.Prediffing(target, path, true, tableContext());
	if (ok != nullptr)
		*ok = done;
	EXPECT_EQ(file.path(), path); // a file it may overwrite stays where it is
	return ScratchFile::read(path);
}

TEST(FileTransformPosix, ColumnRangeLeavesAColumnOut)
{
	// CMergeDoc::IgnoreColumnInComparison's item, for every file: the line
	// endings, the quoted cell and a short row stay as they were
	const std::string expected =
		"id,price\n"
		"1,10\r\n"
		"2,20\n"
		"3\n";
	for (int target = 0; target < 3; ++target)
		EXPECT_EQ(expected, prediffed(_T("le:ColumnRange(\"!2\")"), target, kTable));
	// the columns added with Ctrl+Click
	EXPECT_EQ("name\nma\xc3\xa7\xc3\xa3\r\n\"pear, green\"\n\n",
		prediffed(_T("le:ColumnRange(\"!1,!3\")"), 0, kTable));
}

TEST(FileTransformPosix, ColumnRangeForOneFile)
{
	// "In This Pane Only": the second file's first column
	const String pipeline = _T("le:2:ColumnRange(\"!1\")");
	EXPECT_EQ(kTable, prediffed(pipeline, 0, kTable));
	EXPECT_EQ("name,price\nma\xc3\xa7\xc3\xa3,10\r\n\"pear, green\",20\n\n",
		prediffed(pipeline, 1, kTable));
	EXPECT_EQ(kTable, prediffed(pipeline, 2, kTable));
	// a pass for every file and one for the second, in that order
	EXPECT_EQ("price\n10\r\n20\n\n",
		prediffed(_T("le:ColumnRange(\"!2\")|le:2:ColumnRange(\"!1\")"), 1, kTable));
}

TEST(FileTransformPosix, PrediffingWritesANewFileUnlessItMayOverwrite)
{
	guessUtf8();
	ScratchFile file(kTable);
	String path = file.path();
	PrediffingInfo prediffer(String(_T("le:ColumnRange(\"2\")")));
	EXPECT_TRUE(prediffer.Prediffing(0, path, false, tableContext()));
	EXPECT_NE(file.path(), path);
	EXPECT_EQ(kTable, ScratchFile::read(file.path()));
	EXPECT_EQ("name\nma\xc3\xa7\xc3\xa3\r\n\"pear, green\"\n\n", ScratchFile::read(path));
	ScratchFile::remove(path);
}

TEST(FileTransformPosix, PluginsAreRefused)
{
	bool ok = true;
	EXPECT_EQ(kTable, prediffed(_T("MakeUpper"), 0, kTable, &ok));
	EXPECT_FALSE(ok);
	// nothing to run: no plugin is there for an automatic choice
	EXPECT_EQ(kTable, prediffed(_T("<Automatic>"), 0, kTable, &ok));
	EXPECT_TRUE(ok);
	String errorMessage;
	EXPECT_FALSE(PrediffingInfo(String(_T("MakeUpper"))).Validate(errorMessage));
	EXPECT_FALSE(errorMessage.empty());
}

TEST(FileTransformPosix, ValidateParsesTheExpressions)
{
	String errorMessage;
	EXPECT_TRUE(PrediffingInfo(String(_T("le:ColumnRange(\"!2\")|le:1,3:ColumnRange(\"!1,!4\")"))).Validate(errorMessage));
	EXPECT_TRUE(errorMessage.empty());
	EXPECT_FALSE(PrediffingInfo(String(_T("le:ColumnRange(\"!2\""))).Validate(errorMessage));
	EXPECT_FALSE(errorMessage.empty());
	errorMessage.clear();
	EXPECT_FALSE(PackingInfo(String(_T("le:toUpper(Line"))).Validate(errorMessage));
	EXPECT_FALSE(errorMessage.empty());
}

TEST(FileTransformPosix, UnpackingRunsLineExpressions)
{
	// upstream's Unpack_FilterExpression and its invalid twin, over a file
	// of this test's own
	guessUtf8();
	ScratchFile file("abc\ndef\n");
	std::vector<int> subcodes;
	PluginPipelineContext context;
	String path = file.path();
	EXPECT_TRUE(PackingInfo(String(_T("le:toUpper(Line)"))).Unpacking(0, &subcodes, path, context));
	EXPECT_NE(file.path(), path);
	EXPECT_EQ("ABC\nDEF\n", ScratchFile::read(path));
	ScratchFile::remove(path);
	path = file.path();
	EXPECT_FALSE(PackingInfo(String(_T("le:toUpper(Line"))).Unpacking(0, &subcodes, path, context));
}

} // namespace
