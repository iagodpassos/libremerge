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
