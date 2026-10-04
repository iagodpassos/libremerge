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
#include "FileFilterHelper.h"
#include <filesystem>
#include <fstream>

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

// A mask names a folder by ending in a separator; upstream takes the
// backslash only, here the forward slash counts too.
TEST(FiltersList, FileFilter_FolderMaskTakesEitherSlash)
{
	FileFilterHelper helper;
	helper.SetMaskOrExpression(_T("*.*;!build\\"));
	EXPECT_FALSE(helper.includeDir(_T("build")));
	EXPECT_TRUE(helper.includeDir(_T("src")));
	EXPECT_TRUE(helper.includeFile(_T("a.txt")));

	helper.SetMaskOrExpression(_T("*.*;!build/"));
	EXPECT_FALSE(helper.includeDir(_T("build")));
	EXPECT_TRUE(helper.includeDir(_T("src")));
	EXPECT_TRUE(helper.includeFile(_T("a.txt")));
	EXPECT_TRUE(helper.includeFile(_T("build"))); // a file of that name stays

	// inside the mask too
	helper.SetMaskOrExpression(_T("*.*;!src/gen/"));
	EXPECT_FALSE(helper.includeDir(_T("src\\gen")));
	EXPECT_TRUE(helper.includeDir(_T("src\\lib")));
	helper.SetMaskOrExpression(_T("*.*;!src\\gen\\"));
	EXPECT_FALSE(helper.includeDir(_T("src\\gen")));
	EXPECT_TRUE(helper.includeDir(_T("src\\lib")));

	// only these folders: files anywhere, folders by name
	helper.SetMaskOrExpression(_T("*.*;docs/"));
	EXPECT_TRUE(helper.includeDir(_T("docs")));
	EXPECT_FALSE(helper.includeDir(_T("build")));
}

// Upstream's reload of one filter looked for it in the list without ever
// stepping forward: fine while every filter reloads in list order, an
// endless loop once a file that went away breaks that order.
TEST(FiltersList, FileFilter_ReloadSurvivesAFilterFileThatWentAway)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "libremerge-filters-reload-test";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const auto write = [&dir](const char *file, const char *name)
	{
		std::ofstream out(dir / file);
		out << "name: " << name << "\ndesc: test\ndef: include\nf: \\.tmp$\n";
	};
	write("a.flt", "First");
	write("b.flt", "Second");

	FileFilterHelper helper;
	helper.LoadFileFilterDirPattern(dir.string(), _T("*.flt"));
	ASSERT_EQ(2u, helper.GetFileFilters().size());

	fs::remove(dir / "a.flt");
	write("b.flt", "Second, edited");
	helper.ReloadUpdatedFilters(); // used to never return

	std::vector<String> names;
	for (const FileFilterInfo& info : helper.GetFileFilters())
		names.push_back(info.name);
	EXPECT_EQ(2u, names.size());
	EXPECT_NE(names.end(), std::find(names.begin(), names.end(), String(_T("First"))));
	EXPECT_NE(names.end(), std::find(names.begin(), names.end(), String(_T("Second, edited"))));
	fs::remove_all(dir);
}

} // namespace
