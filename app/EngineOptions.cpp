// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent options manager wired into the engine's GetOptionsMgr().
// Values live in QSettings (plist on macOS, INI elsewhere); option names
// like "Settings/IgnoreSpace" map naturally onto QSettings groups.
#include "pch.h"
#include "EngineOptions.h"

#include <mutex>
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

namespace
{

QString toKey(const String &name)
{
	return QString::fromUtf8(name.data(), static_cast<int>(name.size()));
}

class QSettingsOptionsMgr : public COptionsMgr
{
public:
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
	loadFilters(&options);

	// "Ignore comment differences" asks this registry for a parser of the
	// file's language, in file and folder comparisons alike: WinMerge fills
	// it at startup (CMergeApp::InitSyntaxParserFactories). The built-in
	// parsers are the ones carried here, the same the highlighting uses
	LangServices::SyntaxParserRegistry::GetInstance().RegisterFactory(
		&CrystalLineSyntaxParserFactory::GetInstance());
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
	if (COptionsMgr *mgr = GetOptionsMgr())
		mgr->Set(OPT_CMP_METHOD, method);
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
