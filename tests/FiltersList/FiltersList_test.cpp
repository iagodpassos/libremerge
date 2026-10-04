// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge: not part of the upstream suite. The lists behind the Filters
// dialog name an expression that does not compile by its number, in a
// message that has to format on every platform (upstream builds it with a
// wide-build-only "%S").
#include "pch.h"
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include "FilterList.h"
#include "LineFiltersList.h"
#include "SubstitutionFiltersList.h"
#include "SubstitutionList.h"

namespace
{

TEST(FiltersList, LineFilters_BadExpressionIsReportedByNumber)
{
	LineFiltersList list;
	list.AddFilter(_T("^fine$"), true);
	list.AddFilter(_T("("), false); // not enabled: not tested
	list.AddFilter(_T("["), true);

	std::string message;
	try
	{
		list.MakeFilterList(true);
	}
	catch (const std::runtime_error& e)
	{
		message = e.what();
	}
	EXPECT_EQ(0u, message.find("#3: "));
	EXPECT_GT(message.size(), 4u);

	// without the test the bad one is left out
	std::shared_ptr<FilterList> filters = list.MakeFilterList(false);
	ASSERT_NE(nullptr, filters);
	EXPECT_TRUE(filters->HasRegExps());
	EXPECT_TRUE(filters->Match("fine"));
	EXPECT_FALSE(filters->Match("not fine"));
}

TEST(FiltersList, LineFilters_EmptyAndDisabledAreLeftOut)
{
	LineFiltersList list;
	list.AddFilter(_T(""), true);
	list.AddFilter(_T("x"), false);
	std::shared_ptr<FilterList> filters = list.MakeFilterList(true);
	ASSERT_NE(nullptr, filters);
	EXPECT_FALSE(filters->HasRegExps());
}

TEST(FiltersList, SubstitutionFilters_BadExpressionIsReportedByNumber)
{
	SubstitutionFiltersList list;
	list.Add(_T("a"), _T("b"), false, true, false, true);
	list.Add(_T("("), _T("x"), true, true, false, true);

	std::string message;
	try
	{
		list.MakeSubstitutionList(true);
	}
	catch (const std::runtime_error& e)
	{
		message = e.what();
	}
	EXPECT_EQ(0u, message.find("#2: "));
	EXPECT_GT(message.size(), 4u);

	// as plain text the same pattern is fine
	SubstitutionFiltersList plain;
	plain.Add(_T("("), _T("["), false, true, false, true);
	std::shared_ptr<SubstitutionList> substitutions = plain.MakeSubstitutionList(true);
	ASSERT_NE(nullptr, substitutions);
	EXPECT_EQ("f[x)", substitutions->Subst("f(x)"));
}

} // namespace
