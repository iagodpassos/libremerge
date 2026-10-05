// SPDX-License-Identifier: GPL-2.0-or-later
// LibreMerge: the line filter of the file window's filter bar, a text to
// find or an expression behind "le:" (LineFilterHelper), over a handful of
// lines. Upstream has no test of its own for it.
#include "pch.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "LineFilterHelper.h"
#include "FilterEngine/FilterExpression.h"
#include "FilterEngine/ILineDataProvider.h"
#include "DiffContext.h"
#include "DiffItem.h"
#include "DiffWrapper.h"
#include "PathContext.h"

namespace
{

/** Two panes of lines; an empty one stands for a line that is missing. */
struct Lines : public ILineDataProvider
{
	std::vector<std::string> pane[2];

	int GetLineCount() const override { return static_cast<int>(pane[0].size()); }
	std::string GetLine(int side, int lineIndex) const override
	{
		return side < 2 ? pane[side][lineIndex] : std::string();
	}
	int GetColumnCount(int, int) const override { return 1; }
	std::string GetColumn(int side, int lineIndex, int) const override
	{
		return GetLine(side, lineIndex);
	}
	int GetRealLineNumber(int, int lineIndex) const override { return lineIndex; }
	unsigned GetLineFlags(int side, int lineIndex) const override
	{
		return side < 2 && pane[side][lineIndex].empty() ? LF_GHOST : 0;
	}
	unsigned GetLineEol(int, int) const override { return EOL_LF; }
};

/** The lines a filter holds for, as the file window asks about them. */
std::vector<int> shown(const String &filter, const Lines &lines)
{
	LineFilterHelper helper;
	EXPECT_TRUE(helper.SetStringOrExpression(filter));
	FilterExpression &expression = helper.GetFilterExpression();
	PathContext paths(_T("left"), _T("right"));
	CDiffContext context(paths, CMP_CONTENT);
	expression.SetDiffContext(&context);
	DIFFITEM item;
	item.diffcode.setSideFlag(0);
	item.diffcode.setSideFlag(1);
	FilterSharedContext shared;
	FilterEvalContext evaluation{ &expression, &item, &lines, &shared };
	std::vector<int> found;
	for (int line = 0; line < lines.GetLineCount(); ++line)
	{
		evaluation.lineIndex = line;
		if (expression.Evaluate(evaluation))
			found.push_back(line);
	}
	return found;
}

Lines sample()
{
	Lines lines;
	lines.pane[0] = { "alpha", "ERROR one", "beta", "", "an error at the end" };
	lines.pane[1] = { "alpha", "ERROR one", "", "extra error", "Error: mixed" };
	return lines;
}

} // namespace

TEST(LineFilterHelper, TextIsFoundWhateverItsCase)
{
	const Lines lines = sample();
	const std::vector<int> expected{ 1, 3, 4 };
	// the text sought and the line's may each be in either case
	EXPECT_EQ(expected, shown(_T("error"), lines));
	EXPECT_EQ(expected, shown(_T("ERROR"), lines));
	EXPECT_EQ(expected, shown(_T("Error"), lines));
	EXPECT_EQ(expected, shown(_T("le:Line contains \"ERROR\""), lines));
	EXPECT_EQ((std::vector<int>{ 1 }), shown(_T("le:@cs Line contains \"ERROR\""), lines));
	EXPECT_EQ((std::vector<int>{ 3, 4 }), shown(_T("le:@cs Line contains \"error\""), lines));
	EXPECT_EQ((std::vector<int>{ 4 }), shown(_T("AT THE END"), lines));
	EXPECT_EQ((std::vector<int>{}), shown(_T("nowhere"), lines));
}

TEST(LineFilterHelper, SidesAndMissingLines)
{
	const Lines lines = sample();
	EXPECT_EQ((std::vector<int>{ 1, 4 }), shown(_T("le:LeftLine contains \"error\""), lines));
	EXPECT_EQ((std::vector<int>{ 1, 3, 4 }), shown(_T("le:RightLine contains \"error\""), lines));
	EXPECT_EQ((std::vector<int>{ 3 }), shown(_T("le:LeftMissing"), lines));
	EXPECT_EQ((std::vector<int>{ 0, 1, 3, 4 }), shown(_T("le:RightExists"), lines));
	EXPECT_EQ((std::vector<int>{ 0, 1 }), shown(_T("le:LeftLine = RightLine"), lines));
}

TEST(LineFilterHelper, TextAndExpression)
{
	LineFilterHelper helper;
	EXPECT_TRUE(helper.SetStringOrExpression(_T("a \"quoted\" text")));
	EXPECT_EQ(String(_T("a \"quoted\" text")), helper.GetStringOrExpression());
	EXPECT_EQ(0, helper.GetFilterExpression().errorCode);
	EXPECT_TRUE(helper.SetStringOrExpression(_T("")));
	EXPECT_FALSE(helper.SetStringOrExpression(_T("le:Line contains")));
	EXPECT_NE(0, helper.GetFilterExpression().errorCode);

	EXPECT_EQ(String(_T("\"say \"\"hi\"\"\"")), LineFilterHelper::Quote(_T("say \"hi\"")));
	EXPECT_EQ(String(_T("le:Line contains \"x\"")),
		LineFilterHelper::ConvertToLineContainsExpression(_T("x")));
	EXPECT_EQ(String(_T("le:Different")),
		LineFilterHelper::ConvertToLineContainsExpression(_T("le:Different")));
	EXPECT_EQ(String(), LineFilterHelper::ConvertToLineContainsExpression(_T("")));
	EXPECT_EQ(String(_T("Different")), LineFilterHelper::RemoveLePrefix(_T("le:Different")));
	EXPECT_EQ(String(_T("le:@cs Different")),
		LineFilterHelper::BuildLeFilter(_T(" @cs "), _T("Different")));
}

TEST(LineFilterHelper, AddToExpression)
{
	// to nothing, to a text and to an expression; the directives go in front
	EXPECT_EQ(String(_T("le:Different")),
		LineFilterHelper::AddToExpression(_T(""), _T("Different"), _T("AND")));
	EXPECT_EQ(String(_T("le:Line contains \"x\" AND Different")),
		LineFilterHelper::AddToExpression(_T("x"), _T("Different"), _T("AND")));
	EXPECT_EQ(String(_T("le:Identical OR Different")),
		LineFilterHelper::AddToExpression(_T("le:Identical"), _T("Different"), _T("OR")));
	EXPECT_EQ(String(_T("le:@cs Identical AND Line contains \"x\"")),
		LineFilterHelper::AddToExpression(_T("le:Identical"), _T("@cs Line contains \"x\""),
			_T("AND")));

	LineFilterHelper helper;
	helper.SetStringOrExpression(_T("first"));
	helper.AddToExpression(_T("Line contains \"second\""), _T("AND"));
	EXPECT_EQ(String(_T("le:Line contains \"first\" AND Line contains \"second\"")),
		helper.GetStringOrExpression());
	Lines lines;
	lines.pane[0] = { "first", "first and second", "SECOND" };
	lines.pane[1] = lines.pane[0];
	EXPECT_EQ((std::vector<int>{ 1 }), shown(helper.GetStringOrExpression(), lines));
}
