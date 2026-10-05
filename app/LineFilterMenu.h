// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <QMenu>
#include <QString>

/**
 * WinMerge's line filter helper menu (CLineFilterHelperMenu over
 * IDR_POPUP_LINEFILTERMENU): what the "=" button of the file window's
 * filter bar opens. Each item makes a change to the bar's filter, a text
 * to find in the lines or a line expression behind "le:": a condition on
 * the lines of one side or on what differs between two, a transformation
 * of the text the conditions look at, or a refinement of the filter as a
 * whole (context lines, occurrences, ranges). Conditions join the filter
 * with AND or OR.
 */
class LineFilterMenu : public QMenu
{
	Q_OBJECT
public:
	/** The commands. Runs of them index a table of conditions, in the
	    order of upstream's resource identifiers. */
	enum Command
	{
		MaskClear,
		LineRange,       ///< "Line Text..."
		LineLengthRange,
		WordCountRange,
		ColumnText,      ///< 3: text, number, date and time
		ColumnNumber,
		ColumnDateTime,
		Column1,         ///< 10: the column the column items are about
		Column10 = Column1 + 9,
		LineOdd,
		LineEven,
		LineNumberRange,
		LineDifferent,   ///< 3: different, identical, trivial
		LineIdentical,
		LineTrivial,
		LineExists,      ///< 4: exists, missing, moved, bookmarked
		LineMissing,
		LineMoved,
		LineBookmarked,
		EolCrLf,         ///< 4: CRLF, LF, CR, none
		EolLf,
		EolCr,
		EolNone,
		ConditionAny,    ///< the side a line condition is about
		ConditionLeft,
		ConditionMiddle,
		ConditionRight,
		// difference conditions
		DiffLineEqual,
		DiffLineNotEqual,
		DiffColumnFirst, ///< 14: text (2), number (6), date and time (6)
		DiffColumnNumberLess = DiffColumnFirst + 4,
		DiffColumnNumberLast = DiffColumnFirst + 7,
		DiffColumnDateTimeLess = DiffColumnFirst + 10,
		DiffColumnLast = DiffColumnFirst + 13,
		DiffLineLengthFirst, ///< 12: the six comparisons, then by how much
		DiffLineLengthLast = DiffLineLengthFirst + 11,
		DiffLineLengthRange,
		DiffEolEqual,
		DiffEolNotEqual,
		ConditionDiffLeftRight, ///< the sides a difference is taken between
		ConditionDiffLeftMiddle,
		ConditionDiffMiddleRight,
		ConditionDiffAll,
		// transformations
		FuncFirst,       ///< 13, from Trim to Katakana
		FuncLast = FuncFirst + 12,
		CreateReplaceList,
		CreateRegexReplaceList,
		StringReplaceListsFolder,
		RegexReplaceListsFolder,
		StringReplaceListFirst, ///< the lists found, 20 at most of each kind
		StringReplaceListLast = StringReplaceListFirst + 19,
		RegexReplaceListFirst,
		RegexReplaceListLast = RegexReplaceListFirst + 19,
		// refinements of the whole filter
		LineContextFirst, ///< 5: 0, 1, 3, 5 and 7 lines
		LineContextLast = LineContextFirst + 4,
		MatchNumberFirst, ///< 4: first, last, first 5, after first 5
		MatchNumberLast = MatchNumberFirst + 3,
		MatchNumberRange,
		OccurrenceByBlock,
		MatchInsideWrap,
		MatchOutsideWrap,
		MatchInside,
		MatchOutside,
		MatchCase,
		OperatorAnd,
		OperatorOr,
	};

	explicit LineFilterMenu(QWidget *parent = nullptr);

	/** Where the filter to change is read from when an item is picked. */
	void setFilterSource(std::function<QString()> source) { m_source = std::move(source); }
	/** The window the dialogs of the items belong to, the menu's parent
	    unless told otherwise. */
	void setDialogParent(QWidget *parent) { m_dialogParent = parent; }
	/** The side the conditions are about, how they join the filter (0 AND,
	    1 OR) and the column of the column items, 0 for the first:
	    upstream's constructor for a menu that is asked without being
	    shown. */
	void setTarget(int side, int combine, int column);

	/** CLineFilterHelperMenu::OnCommand: what a command makes of a filter.
	    Nothing when its dialog was cancelled, or when it changes no
	    filter (the folders of the replace lists). */
	std::optional<QString> apply(int command, const QString &filter);

	/** Pick an item as a click on it would (for tests). */
	void pickForTest(int command);
	QAction *actionForTest(int command) const;

signals:
	/** An item changed the filter. */
	void filterChosen(const QString &filter);
	/** A target, a column, the operator or "By Block" was picked: upstream
	    shows the menu again at once, for the condition to follow. */
	void reopenRequested();

private:
	void build();
	void showState(bool lookForLists);
	void fillReplaceLists(QMenu *menu, bool regex, int first, bool look);
	void picked(QAction *action);
	void pick(int command);
	QWidget *dialogParent() const;
	QString combineOperator() const;

	std::function<QString()> m_source;
	QWidget *m_dialogParent = nullptr;
	QMenu *m_stringLists = nullptr;
	QMenu *m_regexLists = nullptr;
	int m_targetSide = 0;     ///< 0 any, 1 left, 2 middle, 3 right
	int m_targetDiffSide = 0; ///< 0 left and right, 1 left and middle, 2 middle and right, 3 all
	int m_operator = 0;       ///< 0 AND, 1 OR
	int m_columnIndex = 0;
	bool m_byBlock = false;   ///< occurrences are counted by block, not by line
};
