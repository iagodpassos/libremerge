// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <QList>
#include <QString>
#include <QStringList>
#include "CompareOptions.h"

class FilterList;
class LineFiltersList;
class SubstitutionFiltersList;
class SubstitutionList;

namespace lm
{

/** Install the application-wide options manager the engine expects
    (GetOptionsMgr()), backed by QSettings for persistence, and register
    the comparison option defaults. */
void installEngineOptions();

/** Fill the engine's syntax parser registry in the order of the highlight
    mode (WinMerge's OPT_SYNTAX_HIGHLIGHT_MODE), as at startup and after
    the Options dialog. */
void initSyntaxParserFactories();
/** Set the highlighting and its mode in memory only, the registry
    filled again (for tests). */
void setSyntaxHighlightForTest(bool on, int mode);

/** The columns a tab takes in the editor (WinMerge's OPT_TAB_SIZE), 1 to
    64, and whether the Tab key inserts a tab rather than spaces
    (OPT_TAB_TYPE 0). */
int tabSize();
bool insertTabs();
/** Set both in memory only (for tests). */
void setTabsForTest(int tabSize, bool insertTabs);
/** WinMerge's "Preserve original EOL" (OPT_ALLOW_MIXED_EOL): each line is
    compared and saved with its own line ending, and set in memory only
    (for tests). */
bool allowMixedEol();
void setAllowMixedEolForTest(bool on);

/** The language the interface is in, as a locale's name ("pt_BR"): the
    codepage of "According to LibreMerge User Interface" follows it. */
void setInterfaceLanguage(const QString &language);
/** WinMerge's UpdateDefaultCodepage: how the files that are not Unicode
    read, mode 0 in the system's codepage (UTF-8), 1 in the interface
    language's Windows codepage, 2 in the custom one (the command line's
    -cp too). */
void updateDefaultCodepage(int mode, int customCodepage);
/** The same from the saved options (OPT_CP_DEFAULT_MODE and
    OPT_CP_DEFAULT_CUSTOM), at startup and after the Options dialog. */
void updateCodepageModule();
/** WinMerge's OPT_CP_DETECT, the guessing codepage_detect::Guess does:
    bit 1 for HTML/XML/.rc files by their declared charset, bit 2 for the
    detector, its auto-select type in the high word; valid UTF-8 reads
    as UTF-8 with either. */
int codepageDetection();

/** The folder of the Tree-sitter grammars (WinMerge's TreeSitterGrammars):
    the bundle's Resources on macOS, the installed one or the one next to
    the binary elsewhere. */
QString treeSitterGrammarsDir();

/** Build the diffutils options from the current global option values. */
DIFFOPTIONS currentDiffOptions();

/** WinMerge's OPT_CMP_IGNORE_CODEPAGE: files with the same text in
    different encodings count as identical. Not part of DIFFOPTIONS: the
    folder comparison takes it in its context, the file comparison when
    it reports identical files. */
bool ignoreCodepageDifferences();

/** The line filters (Tools > Filters) as the engine's filter list:
    differences whose lines all match an expression become ignored. Null
    when "Enable Line Filters" is off or no expression is enabled. */
std::shared_ptr<FilterList> currentLineFilters();
/** The substitution filters as the engine's list: a difference that is
    only a listed pair becomes ignored. Null when they are switched off or
    the list is empty. */
std::shared_ptr<SubstitutionList> currentSubstitutionFilters();

/** WinMerge's OPT_LINEFILTER_ENABLED with theApp.m_pLineFilters, and
    theApp.m_pSubstitutionFiltersList (which carries its own switch):
    copies for the Filters dialog to edit, and saving what it was left
    with. */
bool lineFiltersEnabled();
void copyLineFilters(LineFiltersList *into);
void copySubstitutionFilters(SubstitutionFiltersList *into);
void saveLineFilters(bool enabled, const LineFiltersList &list);
void saveSubstitutionFilters(const SubstitutionFiltersList &list);
/** Read the filters from the saved settings again, the old
    "LineFilters/List" layout included (for tests). */
void reloadFiltersForTest();

/** How a folder comparison decides two files are equal: a COMPARE_TYPE
    from CMP_CONTENT (Full Contents) to CMP_EXISTENCE, WinMerge's
    OPT_CMP_METHOD. */
int currentCompareMethod();
void saveCompareMethod(int method);
/** The methods offered, CMP_CONTENT through CMP_EXISTENCE. */
constexpr int kCompareMethodCount = 7;
/** Display name of a method, WinMerge's IDS_COMPMETHOD_* texts. */
QString compareMethodName(int method);
/** Set the method in memory only, like setCompareOptionsForTest. */
void setCompareMethodForTest(int method);
/** Set it for this run only, unsaved (the command line's -m). */
void useCompareMethod(int method);

/** WinMerge's OPT_CMP_INCLUDE_SUBDIRS ("Include subfolders" on the
    selection screen, on by default): a folder comparison takes the
    subfolders in. */
bool includeSubfolders();
void saveIncludeSubfolders(bool include);
/** Set options by name for this run only, unsaved (the command line's
    -ignorews... and -cfg, CMergeApp::ApplyCommandLineConfigOptions),
    the engine's and those LibreMerge keeps itself (lm::useSetting): a
    name may be a part of one, when only one holds it. What names no
    option comes back. */
QStringList useOptions(const QList<std::pair<QString, QString>> &options);

/** Turn every ignore option off except whitespace, which takes the
    given OPT_CMP_IGNORE_WHITESPACE value; in memory only, the saved
    settings stay as they are (for tests). */
void setCompareOptionsForTest(int ignoreWhitespace);
/** Switch one boolean compare option (an OPT_CMP_* name) in memory only
    (for tests). */
void setCompareFlagForTest(const std::string &option, bool on);

} // namespace lm
