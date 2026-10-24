// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent options manager wired into the engine's GetOptionsMgr().
// Values live in QSettings (plist on macOS, INI elsewhere); option names
// like "Settings/IgnoreSpace" map naturally onto QSettings groups.
#include "pch.h"
#include "EngineOptions.h"
#include "Settings.h"

#include <algorithm>
#include <mutex>
#include <vector>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QVariant>

#include "DiffWrapper.h"
#include "FilterList.h"
#include "LineFiltersList.h"
#include "SubstitutionFiltersList.h"
#include "SubstitutionList.h"
#include "OptionsMgr.h"
#include "OptionsDef.h"
#include "options_global.h"
#include "UnicodeString.h"
#include "stringdiffs.h"
#include "SyntaxParserRegistry.h"
#include "CrystalLineSyntaxParser.h"
#include "TreeSitterParser.h"

namespace
{

QString toKey(const String &name)
{
	return QString::fromUtf8(name.data(), static_cast<int>(name.size()));
}

class QSettingsOptionsMgr : public COptionsMgr
{
public:
	// the one with a range too, which resets a saved value out of it
	using COptionsMgr::InitOption;
	int InitOption(const String& name, const varprop::VariantValue& defaultValue) override
	{
		const int result = AddOption(name, defaultValue);
		if (result != COption::OPT_OK)
			return result;
		loadSaved(name, defaultValue);
		return result;
	}
	int InitOption(const String& name, const String& defaultValue) override
	{
		varprop::VariantValue val;
		val.SetString(defaultValue);
		return InitOption(name, val);
	}
	int InitOption(const String& name, const tchar_t *defaultValue) override
	{
		return InitOption(name, String(defaultValue));
	}
	int InitOption(const String& name, int defaultValue, bool serializable = true) override
	{
		varprop::VariantValue val;
		val.SetInt(defaultValue);
		if (!serializable)
			return AddOption(name, val);
		return InitOption(name, val);
	}
	int InitOption(const String& name, bool defaultValue) override
	{
		varprop::VariantValue val;
		val.SetBool(defaultValue);
		return InitOption(name, val);
	}
	int SaveOption(const String& name) override
	{
		const varprop::VariantValue value = Get(name);
		switch (value.GetType())
		{
		case varprop::VT_STRING:
			m_settings.setValue(toKey(name), QString::fromStdString(value.GetString()));
			break;
		case varprop::VT_INT:
			m_settings.setValue(toKey(name), value.GetInt());
			break;
		case varprop::VT_BOOL:
			m_settings.setValue(toKey(name), value.GetBool());
			break;
		default:
			return COption::OPT_UNKNOWN_TYPE;
		}
		return COption::OPT_OK;
	}
	int SaveOption(const String& name, const varprop::VariantValue& value) override
	{
		const int result = Set(name, value);
		return result == COption::OPT_OK ? SaveOption(name) : result;
	}
	int SaveOption(const String& name, const String& value) override
	{
		varprop::VariantValue val;
		val.SetString(value);
		return SaveOption(name, val);
	}
	int SaveOption(const String& name, const tchar_t *value) override
	{
		return SaveOption(name, String(value));
	}
	int SaveOption(const String& name, int value) override
	{
		varprop::VariantValue val;
		val.SetInt(value);
		return SaveOption(name, val);
	}
	int SaveOption(const String& name, bool value) override
	{
		varprop::VariantValue val;
		val.SetBool(value);
		return SaveOption(name, val);
	}
	int RemoveOption(const String& name) override
	{
		// like CRegOptionsMgr: the saved value goes too
		const int result = COptionsMgr::RemoveOption(name);
		m_settings.remove(toKey(name));
		return result;
	}
	int FlushOptions() override
	{
		m_settings.sync();
		return COption::OPT_OK;
	}
	/** CIniOptionsMgr::ReadStoredSection: the string values the storage
	    has under a section, after this process's own writes. */
	std::map<String, String> ReadStoredSection(const String& section) override
	{
		m_settings.sync();
		std::map<String, String> values;
		m_settings.beginGroup(toKey(section));
		for (const QString &key : m_settings.childKeys())
		{
			const QVariant value = m_settings.value(key);
			if (value.typeId() == QMetaType::QString)
				values.emplace(key.toStdString(), value.toString().toStdString());
		}
		m_settings.endGroup();
		return values;
	}
	void SetSerializing(bool serializing = true) override { (void)serializing; }

private:
	void loadSaved(const String &name, const varprop::VariantValue &defaultValue)
	{
		const QString key = toKey(name);
		if (!m_settings.contains(key))
			return;
		const QVariant saved = m_settings.value(key);
		varprop::VariantValue value(defaultValue);
		switch (defaultValue.GetType())
		{
		case varprop::VT_STRING:
			value.SetString(saved.toString().toStdString());
			break;
		case varprop::VT_INT:
			value.SetInt(saved.toInt());
			break;
		case varprop::VT_BOOL:
			value.SetBool(saved.toBool());
			break;
		default:
			return;
		}
		Set(name, value);
	}

	QSettings m_settings;
};

/** theApp.m_pLineFilters and theApp.m_pSubstitutionFiltersList, with the
    line filters' switch. A folder comparison reads them from its worker
    thread, hence the lock. */
struct FilterStore
{
	std::mutex mutex;
	bool lineFiltersEnabled = false;
	LineFiltersList lineFilters;
	SubstitutionFiltersList substitutionFilters;
};

FilterStore &filterStore()
{
	static FilterStore store;
	return store;
}

/** LibreMerge up to 0.9.7 kept its line filters in one "LineFilters/List"
    value, each entry "1\t<expression>" or "0\t<expression>", with no
    switch for them all: the ticked ones applied. Move them to WinMerge's
    layout once, switched on so that they keep applying. */
void migrateLineFilters()
{
	QSettings settings;
	const QString oldKey = QStringLiteral("LineFilters/List");
	if (!settings.contains(oldKey))
		return;
	const QStringList entries = settings.value(oldKey).toStringList();
	settings.remove(oldKey);
	if (entries.isEmpty() || settings.contains(QStringLiteral("LineFilters/Values")))
		return;
	settings.setValue(QStringLiteral("LineFilters/Values"), static_cast<int>(entries.size()));
	for (int i = 0; i < entries.size(); ++i)
	{
		const QString number = QString::number(i).rightJustified(2, QLatin1Char('0'));
		settings.setValue(QStringLiteral("LineFilters/Filter") + number, entries.at(i).mid(2));
		settings.setValue(QStringLiteral("LineFilters/Enabled") + number,
			entries.at(i).startsWith(QStringLiteral("1\t")) ? 1 : 0);
	}
	settings.setValue(toKey(OPT_LINEFILTER_ENABLED), true);
}

void loadFilters(COptionsMgr *options)
{
	migrateLineFilters();
	options->InitOption(OPT_LINEFILTER_ENABLED, false);
	options->InitOption(OPT_SUBSTITUTION_FILTERS_ENABLED, false);
	FilterStore &store = filterStore();
	const std::lock_guard<std::mutex> lock(store.mutex);
	store.lineFiltersEnabled = options->GetBool(OPT_LINEFILTER_ENABLED);
	store.lineFilters.Initialize(options);
	store.substitutionFilters.Initialize(options);
}

} // namespace

namespace lm
{

void installEngineOptions()
{
	static QSettingsOptionsMgr options;
	SetOptionsMgr(&options);
	strdiff::Init(); // word-diff break characters

	// comparison option defaults (persisted values override these)
	options.InitOption(OPT_CMP_IGNORE_WHITESPACE, 0);
	options.InitOption(OPT_CMP_IGNORE_BLANKLINES, false);
	options.InitOption(OPT_CMP_IGNORE_CASE, false);
	options.InitOption(OPT_CMP_IGNORE_NUMBERS, false);
	options.InitOption(OPT_CMP_IGNORE_EOL, false);
	options.InitOption(OPT_CMP_IGNORE_CODEPAGE, false);
	options.InitOption(OPT_CMP_FILTER_COMMENTLINES, false);
	options.InitOption(OPT_CMP_IGNORE_MISSING_TRAILING_EOL, false);
	options.InitOption(OPT_CMP_IGNORE_LINE_BREAKS, false);
	options.InitOption(OPT_CMP_COMPLETELY_BLANK_OUT_IGNORED_CHANGES, false);
	options.InitOption(OPT_CMP_DIFF_ALGORITHM, 0);
	options.InitOption(OPT_CMP_INDENT_HEURISTIC, true); // upstream default
	options.InitOption(OPT_CMP_MOVED_BLOCKS, false);    // upstream default
	options.InitOption(OPT_CMP_METHOD, static_cast<int>(CMP_CONTENT));
	// how a file's encoding is guessed, WinMerge's default outside the CJK
	// locales: valid UTF-8 reads as UTF-8, HTML/XML/.rc files by their
	// declared charset. Not an option here (it is not saved), but the
	// engine asks for it by name: a folder comparison's context, and the
	// filter expressions that read a file's content
	options.InitOption(OPT_CP_DETECT, (50001 << 16) | 1, false);
	// plugins are on, WinMerge's default: there is no setting for them
	// here, and the line expressions of a prediffer (a table's "Ignore
	// This Column") run as upstream runs them, when they are
	options.InitOption(OPT_PLUGINS_ENABLED, true);
	loadFilters(&options);

	// WinMerge's Editor page: the highlighting, and the parsers it takes
	options.InitOption(OPT_SYNTAX_HIGHLIGHT, true);
	options.InitOption(OPT_SYNTAX_HIGHLIGHT_MODE, 1);
	// its original EOL, and its tabs
	options.InitOption(OPT_ALLOW_MIXED_EOL, false);
	options.InitOption(OPT_TAB_SIZE, 4, 0, 64);
	options.InitOption(OPT_TAB_TYPE, 0, 0, 1); // 0 means tabs inserted
	TreeSitterRegistry::Instance().Initialize(treeSitterGrammarsDir().toStdString());
	initSyntaxParserFactories();
}

void initSyntaxParserFactories()
{
	// CMergeApp::InitSyntaxParserFactories: "Ignore comment differences"
	// and the highlighting ask this registry for a parser of the file's
	// language, in file and folder comparisons alike; it holds the
	// built-in parsers and Tree-sitter's in the order of the highlight
	// mode ("Built-in first" by default: the built-in parsers, then
	// Tree-sitter's for the languages they lack)
	auto &registry = LangServices::SyntaxParserRegistry::GetInstance();
	LangServices::ISyntaxParserFactory *crystal = &CrystalLineSyntaxParserFactory::GetInstance();
	LangServices::ISyntaxParserFactory *treeSitter = &TreeSitterSyntaxParserFactory::GetInstance();
	registry.UnregisterFactory(crystal);
	registry.UnregisterFactory(treeSitter);

	const std::vector<LangServices::ISyntaxParserFactory *> modes[4] = {
		{ crystal },              // Built-in only
		{ crystal, treeSitter },  // Built-in first
		{ treeSitter, crystal },  // Tree-sitter first
		{ treeSitter },           // Tree-sitter only
	};
	int mode = GetOptionsMgr()->GetInt(OPT_SYNTAX_HIGHLIGHT_MODE);
	if (mode < 0 || mode >= 4)
		mode = 3;
	for (LangServices::ISyntaxParserFactory *factory : modes[mode])
		registry.RegisterFactory(factory);
}

void setSyntaxHighlightForTest(bool on, int mode)
{
	if (COptionsMgr *mgr = GetOptionsMgr())
	{
		mgr->Set(OPT_SYNTAX_HIGHLIGHT, on);
		mgr->Set(OPT_SYNTAX_HIGHLIGHT_MODE, mode);
	}
	initSyntaxParserFactories();
}

int tabSize()
{
	// the page keeps it within 1 to 64 (PropEditor); a 0 left in the
	// settings would divide by zero where the tab stops are worked out
	COptionsMgr *mgr = GetOptionsMgr();
	return mgr != nullptr ? std::clamp(mgr->GetInt(OPT_TAB_SIZE), 1, 64) : 4;
}

bool insertTabs()
{
	COptionsMgr *mgr = GetOptionsMgr();
	return mgr == nullptr || mgr->GetInt(OPT_TAB_TYPE) == 0;
}

bool allowMixedEol()
{
	COptionsMgr *mgr = GetOptionsMgr();
	return mgr != nullptr && mgr->GetBool(OPT_ALLOW_MIXED_EOL);
}

void setAllowMixedEolForTest(bool on)
{
	if (COptionsMgr *mgr = GetOptionsMgr())
		mgr->Set(OPT_ALLOW_MIXED_EOL, on);
}

void setTabsForTest(int tabSize, bool insertTabs)
{
	if (COptionsMgr *mgr = GetOptionsMgr())
	{
		mgr->Set(OPT_TAB_SIZE, tabSize);
		mgr->Set(OPT_TAB_TYPE, insertTabs ? 0 : 1);
	}
}

QString treeSitterGrammarsDir()
{
	// the binary's own folder (see sharedFiltersDir)
	const QDir appDir(QFileInfo(QCoreApplication::applicationFilePath()).canonicalPath());
#ifdef Q_OS_MACOS
	// the bundle's Contents/Resources/TreeSitterGrammars
	const QStringList candidates = {
		appDir.filePath(QStringLiteral("../Resources/TreeSitterGrammars")) };
#else
	// installed (or inside the AppImage), then next to a build's binary
	const QStringList candidates = {
		appDir.filePath(QStringLiteral(LM_TREE_SITTER_RELDIR)),
		appDir.filePath(QStringLiteral("TreeSitterGrammars")),
	};
#endif
	for (const QString &candidate : candidates)
		if (QFileInfo(candidate).isDir())
			return QDir::cleanPath(candidate);
	return QDir::cleanPath(candidates.first());
}

int currentCompareMethod()
{
	COptionsMgr *mgr = GetOptionsMgr();
	const int method = mgr != nullptr ? mgr->GetInt(OPT_CMP_METHOD) : CMP_CONTENT;
	return method >= CMP_CONTENT && method <= CMP_EXISTENCE ? method : CMP_CONTENT;
}

void saveCompareMethod(int method)
{
	if (COptionsMgr *mgr = GetOptionsMgr())
	{
		mgr->SaveOption(OPT_CMP_METHOD, method);
		mgr->FlushOptions();
	}
}

QString compareMethodName(int method)
{
	switch (method)
	{
	case CMP_QUICK_CONTENT: return QObject::tr("Quick Contents");
	case CMP_BINARY_CONTENT: return QObject::tr("Binary Contents");
	case CMP_DATE: return QObject::tr("Modified Date");
	case CMP_DATE_SIZE: return QObject::tr("Modified Date and Size");
	case CMP_SIZE: return QObject::tr("Size");
	case CMP_EXISTENCE: return QObject::tr("Existence");
	default: return QObject::tr("Full Contents");
	}
}

void setCompareMethodForTest(int method)
{
	useCompareMethod(method);
}

void useCompareMethod(int method)
{
	if (COptionsMgr *mgr = GetOptionsMgr())
		mgr->Set(OPT_CMP_METHOD, method);
}

QStringList useOptions(const QList<std::pair<QString, QString>> &options)
{
	QStringList unknown;
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr)
		return unknown;
	for (const auto &[name, value] : options)
	{
		const String key = name.toStdString();
		if (mgr->Set(key, value.toStdString()) != COption::OPT_NOTFOUND)
			continue;
		const String longName = mgr->ExpandShortName(key);
		if (!longName.empty())
			mgr->Set(longName, value.toStdString());
		// the options LibreMerge keeps itself, by WinMerge's names
		else if (!useSetting(name, value))
			unknown.append(name);
	}
	return unknown;
}

DIFFOPTIONS currentDiffOptions()
{
	COptionsMgr *mgr = GetOptionsMgr();
	DIFFOPTIONS options{};
	if (mgr == nullptr)
		return options;
	options.nIgnoreWhitespace = mgr->GetInt(OPT_CMP_IGNORE_WHITESPACE);
	options.bIgnoreBlankLines = mgr->GetBool(OPT_CMP_IGNORE_BLANKLINES);
	options.bIgnoreCase = mgr->GetBool(OPT_CMP_IGNORE_CASE);
	options.bIgnoreNumbers = mgr->GetBool(OPT_CMP_IGNORE_NUMBERS);
	options.bIgnoreEol = mgr->GetBool(OPT_CMP_IGNORE_EOL);
	options.nDiffAlgorithm = mgr->GetInt(OPT_CMP_DIFF_ALGORITHM);
	options.bIndentHeuristic = mgr->GetBool(OPT_CMP_INDENT_HEURISTIC);
	// the engine's post-filter turns these differences into ignored
	// (trivial) ones, or drops them when asked to unhighlight completely
	options.bFilterCommentsLines = mgr->GetBool(OPT_CMP_FILTER_COMMENTLINES);
	options.bIgnoreMissingTrailingEol = mgr->GetBool(OPT_CMP_IGNORE_MISSING_TRAILING_EOL);
	options.bIgnoreLineBreaks = mgr->GetBool(OPT_CMP_IGNORE_LINE_BREAKS);
	options.bCompletelyBlankOutIgnoredChanges =
		mgr->GetBool(OPT_CMP_COMPLETELY_BLANK_OUT_IGNORED_CHANGES);
	return options;
}

bool ignoreCodepageDifferences()
{
	COptionsMgr *mgr = GetOptionsMgr();
	return mgr != nullptr && mgr->GetBool(OPT_CMP_IGNORE_CODEPAGE);
}

std::shared_ptr<FilterList> currentLineFilters()
{
	// CDirDoc::LoadLineFilterList; the same as CMergeDoc::Rescan's
	// "enabled ? MakeFilterList() : nullptr" to the engine
	FilterStore &store = filterStore();
	const std::lock_guard<std::mutex> lock(store.mutex);
	if (!store.lineFiltersEnabled)
		return nullptr;
	std::shared_ptr<FilterList> filters = store.lineFilters.MakeFilterList(false);
	return filters != nullptr && filters->HasRegExps() ? filters : nullptr;
}

std::shared_ptr<SubstitutionList> currentSubstitutionFilters()
{
	// CDirDoc::LoadSubstitutionFiltersList
	FilterStore &store = filterStore();
	const std::lock_guard<std::mutex> lock(store.mutex);
	if (!store.substitutionFilters.GetEnabled() || store.substitutionFilters.GetCount() == 0)
		return nullptr;
	return store.substitutionFilters.MakeSubstitutionList();
}

bool lineFiltersEnabled()
{
	FilterStore &store = filterStore();
	const std::lock_guard<std::mutex> lock(store.mutex);
	return store.lineFiltersEnabled;
}

void copyLineFilters(LineFiltersList *into)
{
	FilterStore &store = filterStore();
	const std::lock_guard<std::mutex> lock(store.mutex);
	into->CloneFrom(&store.lineFilters);
}

void copySubstitutionFilters(SubstitutionFiltersList *into)
{
	FilterStore &store = filterStore();
	const std::lock_guard<std::mutex> lock(store.mutex);
	into->CloneFrom(&store.substitutionFilters);
}

void saveLineFilters(bool enabled, const LineFiltersList &list)
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr)
		return;
	FilterStore &store = filterStore();
	const std::lock_guard<std::mutex> lock(store.mutex);
	mgr->SaveOption(OPT_LINEFILTER_ENABLED, enabled);
	store.lineFiltersEnabled = enabled;
	store.lineFilters.CloneFrom(&list);
	store.lineFilters.SaveFilters();
	mgr->FlushOptions();
}

void saveSubstitutionFilters(const SubstitutionFiltersList &list)
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr)
		return;
	FilterStore &store = filterStore();
	const std::lock_guard<std::mutex> lock(store.mutex);
	store.substitutionFilters.CloneFrom(&list);
	store.substitutionFilters.SaveFilters();
	mgr->FlushOptions();
}

void reloadFiltersForTest()
{
	if (COptionsMgr *mgr = GetOptionsMgr())
		loadFilters(mgr);
}

void setCompareOptionsForTest(int ignoreWhitespace)
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr)
		return;
	// Set, not SaveOption: nothing reaches QSettings
	mgr->Set(OPT_CMP_IGNORE_WHITESPACE, ignoreWhitespace);
	mgr->Set(OPT_CMP_IGNORE_BLANKLINES, false);
	mgr->Set(OPT_CMP_IGNORE_CASE, false);
	mgr->Set(OPT_CMP_IGNORE_NUMBERS, false);
	mgr->Set(OPT_CMP_IGNORE_EOL, false);
	mgr->Set(OPT_CMP_IGNORE_CODEPAGE, false);
	mgr->Set(OPT_CMP_FILTER_COMMENTLINES, false);
	mgr->Set(OPT_CMP_IGNORE_MISSING_TRAILING_EOL, false);
	mgr->Set(OPT_CMP_IGNORE_LINE_BREAKS, false);
	mgr->Set(OPT_CMP_COMPLETELY_BLANK_OUT_IGNORED_CHANGES, false);
}

void setCompareFlagForTest(const std::string &option, bool on)
{
	if (COptionsMgr *mgr = GetOptionsMgr())
		mgr->Set(option, on);
}

} // namespace lm
