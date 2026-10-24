// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <QString>
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

/** Turn every ignore option off except whitespace, which takes the
    given OPT_CMP_IGNORE_WHITESPACE value; in memory only, the saved
    settings stay as they are (for tests). */
void setCompareOptionsForTest(int ignoreWhitespace);
/** Switch one boolean compare option (an OPT_CMP_* name) in memory only
    (for tests). */
void setCompareFlagForTest(const std::string &option, bool on);

} // namespace lm
