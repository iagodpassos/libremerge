// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "LineFilterMenu.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QFileInfo>

#include "FilterConditionDialog.h"
#include "FilterEngine/FilterExpression.h"
#include "LineFilterHelper.h"
#include "MatchInsideDialog.h"
#include "ReplaceLists.h"
#include "UnicodeString.h"

namespace
{

QString text(const char *source)
{
	return QCoreApplication::translate("LineFilterMenu", source);
}

QAction *addItem(QMenu *menu, const char *title, int command)
{
	QAction *action = menu->addAction(text(title));
	action->setData(command);
	return action;
}

// the comparisons of the difference conditions, as upstream's menu words them
const char *const kComparisonTexts[] = {
	QT_TRANSLATE_NOOP("LineFilterMenu", "Equal"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Not Equal"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Less Than"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Less Than or Equal to"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Greater Than"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Greater Than or Equal to"),
};

const char *const kColumnTexts[] = {
	QT_TRANSLATE_NOOP("LineFilterMenu", "Column: &1"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Column: &2"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Column: &3"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Column: &4"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Column: &5"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Column: &6"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Column: &7"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Column: &8"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Column: &9"),
	QT_TRANSLATE_NOOP("LineFilterMenu", "Column: 1&0"),
};

/** The ten "Column: n" items, which say what the column items around
    them are about. */
void addColumnItems(QMenu *menu)
{
	for (int i = 0; i < 10; ++i)
		addItem(menu, kColumnTexts[i], LineFilterMenu::Column1 + i)->setCheckable(true);
}

String toEngine(const QString &text)
{
	return text.toStdString();
}

QString fromEngine(const String &text)
{
	return QString::fromStdString(text);
}

String sidePrefix(int side)
{
	static const tchar_t *const sides[] = { _T(""), _T("Left"), _T("Middle"), _T("Right") };
	return side >= 0 && side < 4 ? sides[side] : _T("");
}

String diffSidePrefix(int diffSide, int index)
{
	static const tchar_t *const first[] = { _T("Left"), _T("Left"), _T("Middle") };
	static const tchar_t *const second[] = { _T("Right"), _T("Middle"), _T("Right") };
	if (diffSide < 0 || diffSide >= 3)
		return _T("");
	return index == 0 ? first[diffSide] : second[diffSide];
}

String allEqual(const String &content, bool negate)
{
	return (negate ? _T("not allequal(") : _T("allequal(")) + content + _T(")");
}

// --- upstream's helpers on the filter's text, as they are ---

/** A text to find as the expression it stands for, without "le:" and
    with its directives apart. */
FilterExpression::DirectivesAndExpr normalizeAndSplit(const String &expr)
{
	return FilterExpression::SplitDirectivesAndExpr(
		LineFilterHelper::RemoveLePrefix(LineFilterHelper::ConvertToLineContainsExpression(expr)));
}

/** The whole filter inside a function, its directives kept in front. */
String wrapWithFilterDirectives(const String &filterExpr, const String &wrapperFormat)
{
	auto [directives, expr] = normalizeAndSplit(filterExpr);
	const String wrapped = strutils::format_string1(wrapperFormat, expr);
	return LineFilterHelper::BuildLeFilter(directives, wrapped);
}

bool isIdentifierChar(tchar_t ch)
{
	return (ch >= _T('A') && ch <= _T('Z')) || (ch >= _T('a') && ch <= _T('z'))
		|| (ch >= _T('0') && ch <= _T('9')) || ch == _T('_');
}

/** "Line" and "Column<n>", of any side: the text a line condition looks at. */
bool isLineOrColumnAttribute(const String &identifier)
{
	static const String prefixes[] = { _T(""), _T("Left"), _T("Right"), _T("Middle") };
	for (const String &prefix : prefixes)
		if (identifier == prefix + _T("Line"))
			return true;

	const size_t colPos = identifier.rfind(_T("Column"));
	if (colPos != String::npos)
	{
		const bool validPrefix = colPos == 0
			|| (colPos == 4 && identifier.substr(0, 4) == _T("Left"))
			|| (colPos == 5 && identifier.substr(0, 5) == _T("Right"))
			|| (colPos == 6 && identifier.substr(0, 6) == _T("Middle"));
		if (validPrefix && colPos + 6 < identifier.length())
		{
			for (size_t i = colPos + 6; i < identifier.length(); ++i)
				if (identifier[i] < _T('0') || identifier[i] > _T('9'))
					return false;
			return true;
		}
	}
	return false;
}

/** The functions that give a line's or a column's text. */
bool isLineOrColumnFunction(const String &funcName)
{
	static const String baseFuncs[] = { _T("LineAt"), _T("LineOffsetAt"), _T("Column"),
		_T("ColumnAt"), _T("ColumnOffsetAt") };
	static const String prefixes[] = { _T(""), _T("Left"), _T("Right"), _T("Middle") };
	for (const String &prefix : prefixes)
		for (const String &baseFunc : baseFuncs)
			if (funcName == prefix + baseFunc)
				return true;
	return false;
}

/** An identifier at pos, with its arguments when it is a function call. */
String extractIdentifierOrFunction(const String &expr, size_t &pos)
{
	const size_t start = pos;
	while (pos < expr.length() && isIdentifierChar(expr[pos]))
		pos++;

	if (pos < expr.length() && expr[pos] == _T('('))
	{
		int parenDepth = 1;
		bool inQuotes = false;
		pos++; // skip '('
		while (pos < expr.length() && parenDepth > 0)
		{
			if (expr[pos] == _T('"'))
			{
				if (pos + 1 < expr.length() && expr[pos + 1] == _T('"'))
					pos += 2; // escaped quote
				else
				{
					inQuotes = !inQuotes;
					pos++;
				}
			}
			else if (!inQuotes && (expr[pos] == _T('(') || expr[pos] == _T(')')))
				parenDepth += (expr[pos++] == _T('(') ? 1 : -1);
			else
				pos++;
		}
	}
	return expr.substr(start, pos - start);
}

String wrapExpressionRecursive(const String &expr, const String &funcTemplate);

String processFunctionArguments(const String &args, const String &funcTemplate)
{
	String result;
	size_t pos = 0;
	int parenDepth = 0;
	bool inQuotes = false;
	size_t argStart = 0;

	while (pos < args.length())
	{
		if (args[pos] == _T('"'))
		{
			if (pos + 1 < args.length() && args[pos + 1] == _T('"'))
				pos += 2; // escaped quote
			else
			{
				inQuotes = !inQuotes;
				pos++;
			}
		}
		else if (!inQuotes && args[pos] == _T('('))
		{
			parenDepth++;
			pos++;
		}
		else if (!inQuotes && args[pos] == _T(')'))
		{
			parenDepth--;
			pos++;
		}
		else if (!inQuotes && parenDepth == 0 && args[pos] == _T(','))
		{
			result += wrapExpressionRecursive(args.substr(argStart, pos - argStart), funcTemplate);
			result += _T(',');
			pos++;
			argStart = pos;
		}
		else
			pos++;
	}
	if (argStart < args.length())
		result += wrapExpressionRecursive(args.substr(argStart, pos - argStart), funcTemplate);
	return result;
}

/** Put every line or column text of an expression inside a function,
    quoted strings left alone. */
String wrapExpressionRecursive(const String &expr, const String &funcTemplate)
{
	String wrappedExpr;
	size_t pos = 0;

	while (pos < expr.length())
	{
		if (expr[pos] == _T('"'))
		{
			// a quoted string, "" being a quote inside it
			wrappedExpr += expr[pos++];
			while (pos < expr.length())
			{
				wrappedExpr += expr[pos];
				if (expr[pos++] == _T('"'))
				{
					if (pos < expr.length() && expr[pos] == _T('"'))
						wrappedExpr += expr[pos++];
					else
						break;
				}
			}
		}
		else if (isIdentifierChar(expr[pos]) && (pos == 0 || !isIdentifierChar(expr[pos - 1])))
		{
			const String identOrFunc = extractIdentifierOrFunction(expr, pos);
			const size_t parenPos = identOrFunc.find(_T('('));
			if (parenPos != String::npos)
			{
				const String funcName = identOrFunc.substr(0, parenPos);
				const String funcArgs = identOrFunc.substr(parenPos + 1,
					identOrFunc.length() - parenPos - 2);
				const String processedArgs = processFunctionArguments(funcArgs, funcTemplate);
				const String processedFunc = funcName + _T("(") + processedArgs + _T(")");
				if (isLineOrColumnFunction(funcName))
					wrappedExpr += strutils::format_string1(funcTemplate, processedFunc);
				else
					wrappedExpr += processedFunc;
			}
			else if (isLineOrColumnAttribute(identOrFunc))
				wrappedExpr += strutils::format_string1(funcTemplate, identOrFunc);
			else
				wrappedExpr += identOrFunc;
		}
		else
			wrappedExpr += expr[pos++];
	}
	return wrappedExpr;
}

/** The transformation of what a filter looks at: the function around
    every line or column text of it, or around the whole of it when it
    names none. */
String wrapAttributesWithFunction(const String &filterExpr, const String &funcTemplate)
{
	auto [directives, expr] = FilterExpression::SplitDirectivesAndExpr(
		LineFilterHelper::RemoveLePrefix(filterExpr));
	String wrappedExpr = wrapExpressionRecursive(expr, funcTemplate);
	if (expr == wrappedExpr)
		wrappedExpr = strutils::format_string1(funcTemplate, !expr.empty() ? expr : _T("Line"));
	return LineFilterHelper::BuildLeFilter(directives, wrappedExpr);
}

} // namespace

LineFilterMenu::LineFilterMenu(QWidget *parent)
	: QMenu(parent)
{
	build();
	connect(this, &QMenu::triggered, this, &LineFilterMenu::picked);
	// (the replace lists are looked for when the menu opens, as upstream
	// does: looking makes their folders)
	connect(this, &QMenu::aboutToShow, this, [this]() { showState(true); });
	showState(false);
}

void LineFilterMenu::setTarget(int side, int combine, int column)
{
	m_targetSide = side;
	m_operator = combine;
	m_columnIndex = column;
}

void LineFilterMenu::build()
{
	addItem(this, QT_TRANSLATE_NOOP("LineFilterMenu", "&Clear All"), MaskClear);
	addSeparator();

	// --- conditions on the lines of one side ---
	QMenu *line = addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "Add L&ine Condition")));
	addItem(line, QT_TRANSLATE_NOOP("LineFilterMenu", "L&ine Text..."), LineRange);
	QMenu *column = line->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "&Column")));
	addItem(column, QT_TRANSLATE_NOOP("LineFilterMenu", "&Text..."), ColumnText);
	addItem(column, QT_TRANSLATE_NOOP("LineFilterMenu", "&Number..."), ColumnNumber);
	addItem(column, QT_TRANSLATE_NOOP("LineFilterMenu", "&Date/Time..."), ColumnDateTime);
	addColumnItems(column);
	addItem(line, QT_TRANSLATE_NOOP("LineFilterMenu", "Line L&ength..."), LineLengthRange);
	addItem(line, QT_TRANSLATE_NOOP("LineFilterMenu", "&Word Count..."), WordCountRange);
	QMenu *number = line->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "Line &Number")));
	addItem(number, QT_TRANSLATE_NOOP("LineFilterMenu", "&Odd Lines"), LineOdd);
	addItem(number, QT_TRANSLATE_NOOP("LineFilterMenu", "&Even Lines"), LineEven);
	addItem(number, QT_TRANSLATE_NOOP("LineFilterMenu", "&Custom Range..."), LineNumberRange);
	QMenu *status = line->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "Line &Status")));
	addItem(status, QT_TRANSLATE_NOOP("LineFilterMenu", "Different"), LineDifferent);
	addItem(status, QT_TRANSLATE_NOOP("LineFilterMenu", "Identical"), LineIdentical);
	addItem(status, QT_TRANSLATE_NOOP("LineFilterMenu", "Trivial"), LineTrivial);
	status->addSeparator();
	addItem(status, QT_TRANSLATE_NOOP("LineFilterMenu", "Exists"), LineExists);
	addItem(status, QT_TRANSLATE_NOOP("LineFilterMenu", "Missing"), LineMissing);
	addItem(status, QT_TRANSLATE_NOOP("LineFilterMenu", "Moved"), LineMoved);
	addItem(status, QT_TRANSLATE_NOOP("LineFilterMenu", "Bookmarked"), LineBookmarked);
	QMenu *eol = line->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "&EOL")));
	addItem(eol, QT_TRANSLATE_NOOP("LineFilterMenu", "&Windows (CRLF)"), EolCrLf);
	addItem(eol, QT_TRANSLATE_NOOP("LineFilterMenu", "&Unix (LF)"), EolLf);
	addItem(eol, QT_TRANSLATE_NOOP("LineFilterMenu", "&Mac (CR)"), EolCr);
	addItem(eol, QT_TRANSLATE_NOOP("LineFilterMenu", "None"), EolNone);

	// the side the conditions above are about
	auto *sides = new QActionGroup(this);
	const char *const sideTexts[] = {
		QT_TRANSLATE_NOOP("LineFilterMenu", "Target: &Any (Left/Middle/Right)"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "Target: &Left"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "Target: &Middle"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "Target: &Right"),
	};
	for (int i = 0; i < 4; ++i)
	{
		QAction *action = addItem(this, sideTexts[i], ConditionAny + i);
		action->setCheckable(true);
		sides->addAction(action);
	}
	addSeparator();

	// --- conditions on the difference between two sides ---
	QMenu *difference = addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu",
		"Add &Difference Condition")));
	QMenu *diffLine = difference->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "L&ine Text")));
	addItem(diffLine, kComparisonTexts[0], DiffLineEqual);
	addItem(diffLine, kComparisonTexts[1], DiffLineNotEqual);
	QMenu *diffColumn = difference->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "&Column")));
	QMenu *diffText = diffColumn->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "&Text")));
	addItem(diffText, kComparisonTexts[0], DiffColumnFirst);
	addItem(diffText, kComparisonTexts[1], DiffColumnFirst + 1);
	QMenu *diffNumber = diffColumn->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "&Number")));
	for (int i = 0; i < 6; ++i)
		addItem(diffNumber, kComparisonTexts[i], DiffColumnFirst + 2 + i);
	QMenu *diffDate = diffColumn->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "&Date/Time")));
	for (int i = 0; i < 6; ++i)
		addItem(diffDate, kComparisonTexts[i], DiffColumnFirst + 8 + i);
	addColumnItems(diffColumn);
	QMenu *diffLength = difference->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu",
		"Line L&ength")));
	for (int i = 0; i < 6; ++i)
		addItem(diffLength, kComparisonTexts[i], DiffLineLengthFirst + i);
	diffLength->addSeparator();
	const char *const lengthTexts[] = {
		QT_TRANSLATE_NOOP("LineFilterMenu", "Less than 10"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "10 or more"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "Less than 100"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "100 or more"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "Less than 1000"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "1000 or more"),
	};
	for (int i = 0; i < 6; ++i)
		addItem(diffLength, lengthTexts[i], DiffLineLengthFirst + 6 + i);
	addItem(diffLength, QT_TRANSLATE_NOOP("LineFilterMenu", "Custom Range..."),
		DiffLineLengthRange);
	QMenu *diffStatus = difference->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu",
		"Line &Status")));
	addItem(diffStatus, QT_TRANSLATE_NOOP("LineFilterMenu", "Different"), LineDifferent);
	addItem(diffStatus, QT_TRANSLATE_NOOP("LineFilterMenu", "Identical"), LineIdentical);
	addItem(diffStatus, QT_TRANSLATE_NOOP("LineFilterMenu", "Trivial"), LineTrivial);
	QMenu *diffEol = difference->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "&EOL")));
	addItem(diffEol, kComparisonTexts[0], DiffEolEqual);
	addItem(diffEol, kComparisonTexts[1], DiffEolNotEqual);

	// the sides a difference is taken between
	auto *pairs = new QActionGroup(this);
	const char *const pairTexts[] = {
		QT_TRANSLATE_NOOP("LineFilterMenu", "Target: &Left and Right"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "Target: Left and &Middle"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "Target: Middle and &Right"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "Target: &All"),
	};
	for (int i = 0; i < 4; ++i)
	{
		QAction *action = addItem(this, pairTexts[i], ConditionDiffLeftRight + i);
		action->setCheckable(true);
		pairs->addAction(action);
	}
	addSeparator();

	// --- what the conditions look at, transformed ---
	QMenu *transform = addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu",
		"&Transform Line/Column")));
	const char *const funcTexts[] = {
		QT_TRANSLATE_NOOP("LineFilterMenu", "&Trim"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "Normalize &Whitespace"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "&Replace"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "Reg&ex Replace"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "&Lowercase"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "&Uppercase"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "&Half-width"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "&Full-width"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "Normalize &Unicode"),
	};
	for (int i = 0; i < 9; ++i)
		addItem(transform, funcTexts[i], FuncFirst + i);
	QMenu *chinese = transform->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu",
		"&Chinese Conversion")));
	addItem(chinese, QT_TRANSLATE_NOOP("LineFilterMenu", "&Simplified Chinese"), FuncFirst + 9);
	addItem(chinese, QT_TRANSLATE_NOOP("LineFilterMenu", "&Traditional Chinese"), FuncFirst + 10);
	QMenu *japanese = transform->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu",
		"&Japanese Conversion")));
	addItem(japanese, QT_TRANSLATE_NOOP("LineFilterMenu", "&Hiragana"), FuncFirst + 11);
	addItem(japanese, QT_TRANSLATE_NOOP("LineFilterMenu", "&Katakana"), FuncFirst + 12);
	QMenu *lists = transform->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "Replace &Lists")));
	addItem(lists, QT_TRANSLATE_NOOP("LineFilterMenu", "&Create String Replace List and Insert..."),
		CreateReplaceList);
	addItem(lists, QT_TRANSLATE_NOOP("LineFilterMenu", "Create &Regex Replace List and Insert..."),
		CreateRegexReplaceList);
	lists->addSeparator();
	m_stringLists = lists->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu",
		"&String Replace Lists")));
	m_regexLists = lists->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu",
		"Re&gex Replace Lists")));
	lists->addSeparator();
	addItem(lists, QT_TRANSLATE_NOOP("LineFilterMenu", "Open String Replace Lists Folder..."),
		StringReplaceListsFolder);
	addItem(lists, QT_TRANSLATE_NOOP("LineFilterMenu", "Open Regex Replace Lists Folder..."),
		RegexReplaceListsFolder);

	// --- the filter as a whole, refined ---
	QMenu *refine = addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "&Refine Current Filter")));
	QMenu *context = refine->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu",
		"Add Con&text Lines")));
	const char *const contextTexts[] = {
		QT_TRANSLATE_NOOP("LineFilterMenu", "&0 Lines"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "&1 Line"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "&3 Lines"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "&5 Lines"),
		QT_TRANSLATE_NOOP("LineFilterMenu", "&7 Lines"),
	};
	for (int i = 0; i < 5; ++i)
		addItem(context, contextTexts[i], LineContextFirst + i);
	QMenu *occurrence = refine->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu",
		"Filter by &Occurrence")));
	addItem(occurrence, QT_TRANSLATE_NOOP("LineFilterMenu", "&First"), MatchNumberFirst);
	addItem(occurrence, QT_TRANSLATE_NOOP("LineFilterMenu", "&Last"), MatchNumberFirst + 1);
	addItem(occurrence, QT_TRANSLATE_NOOP("LineFilterMenu", "First &5 Matches"),
		MatchNumberFirst + 2);
	addItem(occurrence, QT_TRANSLATE_NOOP("LineFilterMenu", "After First &5"),
		MatchNumberFirst + 3);
	addItem(occurrence, QT_TRANSLATE_NOOP("LineFilterMenu", "&Custom Range..."), MatchNumberRange);
	occurrence->addSeparator();
	addItem(occurrence, QT_TRANSLATE_NOOP("LineFilterMenu", "By &Block"), OccurrenceByBlock)
		->setCheckable(true);
	QMenu *range = refine->addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "&Range")));
	addItem(range, QT_TRANSLATE_NOOP("LineFilterMenu", "&Inside..."), MatchInsideWrap);
	addItem(range, QT_TRANSLATE_NOOP("LineFilterMenu", "&Outside..."), MatchOutsideWrap);
	QMenu *createRange = addMenu(text(QT_TRANSLATE_NOOP("LineFilterMenu", "Create &Range")));
	addItem(createRange, QT_TRANSLATE_NOOP("LineFilterMenu", "&Inside..."), MatchInside);
	addItem(createRange, QT_TRANSLATE_NOOP("LineFilterMenu", "&Outside..."), MatchOutside);
	addSeparator();
	addItem(this, QT_TRANSLATE_NOOP("LineFilterMenu", "Match &case"), MatchCase)
		->setCheckable(true);
	addSeparator();

	// how a new condition joins the ones the filter has
	auto *operators = new QActionGroup(this);
	QAction *andAction = addItem(this, QT_TRANSLATE_NOOP("LineFilterMenu", "Combine: A&ND"),
		OperatorAnd);
	QAction *orAction = addItem(this, QT_TRANSLATE_NOOP("LineFilterMenu", "Combine: &OR"),
		OperatorOr);
	for (QAction *action : { andAction, orAction })
	{
		action->setCheckable(true);
		operators->addAction(action);
	}
}

/** PopulateReplaceListMenu: the lists of a kind, or "<None>". */
void LineFilterMenu::fillReplaceLists(QMenu *menu, bool regex, int first, bool look)
{
	menu->clear();
	const QStringList lists = look ? lm::replaceLists(regex) : QStringList();
	if (lists.isEmpty())
	{
		QAction *none = menu->addAction(text(QT_TRANSLATE_NOOP("LineFilterMenu", "<None>")));
		none->setEnabled(false);
		return;
	}
	for (int i = 0; i < lists.size() && i < lm::kMaxReplaceLists; ++i)
	{
		QAction *action = menu->addAction(QFileInfo(lists.at(i)).fileName());
		action->setData(first + i);
	}
}

/** The ticks follow the targets, the column, the operator and "By
    Block"; "Match case" the filter itself. With every side as the target
    of a difference, a column's number or date can only be equal or not. */
void LineFilterMenu::showState(bool lookForLists)
{
	fillReplaceLists(m_stringLists, false, StringReplaceListFirst, lookForLists);
	fillReplaceLists(m_regexLists, true, RegexReplaceListFirst, lookForLists);

	const String filter = toEngine(m_source ? m_source() : QString());
	const bool matchCase = FilterExpression::HasCaseSensitiveDirective(
		LineFilterHelper::RemoveLePrefix(filter));
	const bool allSides = m_targetDiffSide == 3;
	const QList<QAction *> all = findChildren<QAction *>();
	for (QAction *action : all)
	{
		if (action->menu() != nullptr || action->isSeparator() || !action->data().isValid())
			continue;
		const int command = action->data().toInt();
		if (command >= ConditionAny && command <= ConditionRight)
			action->setChecked(command - ConditionAny == m_targetSide);
		else if (command >= ConditionDiffLeftRight && command <= ConditionDiffAll)
			action->setChecked(command - ConditionDiffLeftRight == m_targetDiffSide);
		else if (command >= OperatorAnd && command <= OperatorOr)
			action->setChecked(command - OperatorAnd == m_operator);
		else if (command >= Column1 && command <= Column10)
			action->setChecked(command - Column1 == m_columnIndex);
		else if (command == MatchCase)
			action->setChecked(matchCase);
		else if (command == OccurrenceByBlock)
			action->setChecked(m_byBlock);
		else if ((command >= DiffColumnNumberLess && command <= DiffColumnNumberLast)
			|| (command >= DiffColumnDateTimeLess && command <= DiffColumnLast))
			action->setVisible(!allSides);
	}
}

QAction *LineFilterMenu::actionForTest(int command) const
{
	const QList<QAction *> all = findChildren<QAction *>();
	for (QAction *action : all)
		if (action->menu() == nullptr && !action->isSeparator() && action->data().isValid()
			&& action->data().toInt() == command)
			return action;
	return nullptr;
}

void LineFilterMenu::pickForTest(int command)
{
	if (QAction *action = actionForTest(command))
		action->trigger();
	else
		pick(command);
}

void LineFilterMenu::picked(QAction *action)
{
	if (action == nullptr || action->isSeparator() || !action->data().isValid())
		return;
	const int command = action->data().toInt();
	if (command >= ConditionAny && command <= ConditionRight)
		m_targetSide = command - ConditionAny;
	else if (command >= ConditionDiffLeftRight && command <= ConditionDiffAll)
		m_targetDiffSide = command - ConditionDiffLeftRight;
	else if (command >= OperatorAnd && command <= OperatorOr)
		m_operator = command - OperatorAnd;
	else if (command >= Column1 && command <= Column10)
		m_columnIndex = command - Column1;
	else if (command == OccurrenceByBlock)
		m_byBlock = !m_byBlock;
	else
	{
		pick(command);
		return;
	}
	showState(true);
	emit reopenRequested();
}

void LineFilterMenu::pick(int command)
{
	const std::optional<QString> result = apply(command, m_source ? m_source() : QString());
	if (result.has_value())
		emit filterChosen(*result);
}

QWidget *LineFilterMenu::dialogParent() const
{
	return m_dialogParent != nullptr ? m_dialogParent : parentWidget();
}

QString LineFilterMenu::combineOperator() const
{
	return m_operator == 1 ? QStringLiteral("OR") : QStringLiteral("AND");
}

std::optional<QString> LineFilterMenu::apply(int command, const QString &filter)
{
	const String filterExpr = toEngine(filter);
	const String op = toEngine(combineOperator());
	// a condition joins the filter by the operator in use
	const auto added = [&filterExpr, &op](const String &expr) {
		return std::optional<QString>(fromEngine(
			LineFilterHelper::AddToExpression(filterExpr, expr, op)));
	};
	// the same for a condition asked for in the Filter Condition dialog
	const auto asked = [this, &added](bool difference, const QString &field,
		const char *defaultOperator, const QString &transform) -> std::optional<QString> {
		FilterConditionDialog dialog(difference, difference ? m_targetDiffSide : m_targetSide,
			field, QString(), QLatin1String(defaultOperator), transform, false, dialogParent());
		if (dialog.exec() != QDialog::Accepted)
			return std::nullopt;
		return added(toEngine(dialog.expression()));
	};
	const auto lineProperty = [this](const tchar_t *name) { return sidePrefix(m_targetSide) + name; };
	const auto diffProperty = [this](const String &name, int index) {
		return diffSidePrefix(m_targetDiffSide, index) + name;
	};
	const QString same = QStringLiteral("%1");

	switch (command)
	{
	case MaskClear:
		return QString();
	case LineRange:
		return asked(false, QStringLiteral("Line"), "%1 contains %2", same);
	case LineLengthRange:
		return asked(false, QStringLiteral("LineLength"), "%1 = %2", same);
	case WordCountRange:
		return asked(false, QStringLiteral("Line"), "%1 = %2",
			QStringLiteral("regexCount(%1, \"\\S+\")"));
	case ColumnText:
	case ColumnNumber:
	case ColumnDateTime:
	{
		static const char *const conversions[] = { "%1", "toNumber(%1)", "toDateTime(%1)" };
		static const char *const operators[] = { "%1 contains %2", "%1 = %2", "%1 < %2" };
		const int dataType = command - ColumnText;
		return asked(false, QStringLiteral("Column%1").arg(m_columnIndex + 1),
			operators[dataType], QLatin1String(conversions[dataType]));
	}
	case LineOdd:
	case LineEven:
	{
		static const tchar_t *const conditions[] = { _T("(%1 %% 2) = 1"), _T("(%1 %% 2) = 0") };
		return added(strutils::format_string1(conditions[command - LineOdd],
			lineProperty(_T("LineNumber"))));
	}
	case LineNumberRange:
		return asked(false, QStringLiteral("LineNumber"), "%1 > %2", same);
	case LineDifferent:
	case LineIdentical:
	case LineTrivial:
	{
		static const tchar_t *const identifiers[] = { _T("Different"), _T("Identical"),
			_T("Trivial") };
		return added(identifiers[command - LineDifferent]);
	}
	case LineExists:
	case LineMissing:
	case LineMoved:
	case LineBookmarked:
	{
		static const tchar_t *const identifiers[] = { _T("Exists"), _T("Missing"), _T("Moved"),
			_T("Bookmarked") };
		return added(lineProperty(identifiers[command - LineExists]));
	}
	case EolCrLf:
	case EolLf:
	case EolCr:
	case EolNone:
	{
		static const tchar_t *const values[] = { _T("\"CRLF\""), _T("\"LF\""), _T("\"CR\""),
			_T("\"None\"") };
		return added(lineProperty(_T("EOLStr")) + _T(" = ") + values[command - EolCrLf]);
	}
	case MatchNumberRange:
	{
		// the dialog's left-hand side is the filter itself, numbered
		auto [filterDirectives, filterBody] = normalizeAndSplit(filterExpr);
		// (it stands where the dialog expects a template: a "%" of the
		// filter is doubled to come out of it as it went in, which
		// upstream leaves undone)
		String body = filterBody;
		strutils::replace(body, _T("%"), _T("%%"));
		const String propName = (m_byBlock ? _T("matchBlockNumber(") : _T("matchNumber("))
			+ body + _T(")");
		FilterConditionDialog dialog(false, m_targetSide, fromEngine(filterBody), QString(),
			QStringLiteral("%1 > %2"), fromEngine(propName), false, dialogParent());
		if (dialog.exec() != QDialog::Accepted)
			return std::nullopt;
		auto [dialogDirectives, expr] = FilterExpression::SplitDirectivesAndExpr(
			toEngine(dialog.expression()));
		const String mergedDirectives = FilterExpression::MergeDirectives(filterDirectives,
			dialogDirectives);
		return fromEngine(LineFilterHelper::BuildLeFilter(mergedDirectives, expr));
	}
	case MatchInsideWrap:
	case MatchOutsideWrap:
	case MatchInside:
	case MatchOutside:
	{
		// the lines from one filter's match to another's, or all but them
		const bool wrap = command == MatchInsideWrap || command == MatchOutsideWrap;
		const bool outside = command == MatchOutsideWrap || command == MatchOutside;
		MatchInsideDialog dialog(wrap ? filter : QString(), wrap ? filter : QString(),
			dialogParent());
		if (dialog.exec() != QDialog::Accepted)
			return std::nullopt;
		auto [directives1, expr1] = normalizeAndSplit(toEngine(dialog.filter1()));
		auto [directives2, expr2] = normalizeAndSplit(toEngine(dialog.filter2()));
		if (expr1.empty())
			expr1 = _T("Line contains \"BEGIN\"");
		if (expr2.empty())
			expr2 = _T("Line contains \"END\"");
		const String mergedDirectives = FilterExpression::MergeDirectives(directives1, directives2);
		const String function = (outside ? _T("not matchInside(") : _T("matchInside("))
			+ expr1 + _T(", ") + expr2 + _T(")");
		if (wrap)
			return fromEngine(LineFilterHelper::BuildLeFilter(mergedDirectives, function));
		return added((mergedDirectives.empty() ? _T("") : mergedDirectives + _T(" ")) + function);
	}
	case DiffLineEqual:
	case DiffLineNotEqual:
		if (m_targetDiffSide == 3)
			return added(allEqual(_T("Line"), command == DiffLineNotEqual));
		return added(strutils::format_string2(command == DiffLineEqual ? _T("%1 = %2")
			: _T("%1 != %2"), diffProperty(_T("Line"), 0), diffProperty(_T("Line"), 1)));
	case DiffEolEqual:
	case DiffEolNotEqual:
		if (m_targetDiffSide == 3)
			return added(allEqual(_T("EOL"), command == DiffEolNotEqual));
		return added(strutils::format_string2(command == DiffEolEqual ? _T("%1 = %2")
			: _T("%1 != %2"), diffProperty(_T("EOL"), 0), diffProperty(_T("EOL"), 1)));
	case DiffLineLengthRange:
		return asked(true, QStringLiteral("LineLength"), "%1 = %2",
			QStringLiteral("abs(%1 - %2)"));
	case CreateReplaceList:
	case CreateRegexReplaceList:
	{
		const bool regex = command == CreateRegexReplaceList;
		const std::optional<QString> created = lm::createAndSelectReplaceList(dialogParent(),
			regex);
		if (!created.has_value())
			return std::nullopt;
		// (a "%" of the path is doubled: the path goes into a template.
		// Upstream forgets it here, and its %APPDATA% loses the signs)
		String path = toEngine(lm::replaceListPathForExpression(*created));
		strutils::replace(path, _T("%"), _T("%%"));
		return fromEngine(wrapAttributesWithFunction(filterExpr,
			(regex ? _T("regexReplaceWithList(%1, \"") : _T("replaceWithList(%1, \""))
			+ path + _T("\")")));
	}
	case StringReplaceListsFolder:
		lm::openReplaceListFolder(false);
		return std::nullopt;
	case RegexReplaceListsFolder:
		lm::openReplaceListFolder(true);
		return std::nullopt;
	case MatchCase:
	{
		auto [directives, filterBody] = normalizeAndSplit(filterExpr);
		directives = FilterExpression::HasCaseSensitiveDirective(directives)
			? FilterExpression::RemoveCaseSensitiveDirective(directives)
			: FilterExpression::AddCaseSensitiveDirective(directives);
		return fromEngine(LineFilterHelper::BuildLeFilter(directives, filterBody));
	}
	default:
		break;
	}

	if (command >= LineContextFirst && command <= LineContextLast)
	{
		static const tchar_t *const contexts[] = { _T("0"), _T("1"), _T("3"), _T("5"), _T("7") };
		const String lines = contexts[command - LineContextFirst];
		return fromEngine(wrapWithFilterDirectives(filterExpr,
			_T("matchContext(%1, ") + lines + _T(", ") + lines + _T(")")));
	}
	if (command >= MatchNumberFirst && command <= MatchNumberLast)
	{
		const String number = m_byBlock ? _T("matchBlockNumber") : _T("matchNumber");
		const String count = m_byBlock ? _T("matchBlockCount") : _T("matchCount");
		const String expressions[] = {
			number + _T("(%1) = 1"), number + _T("(%1) = ") + count + _T("(%1)"),
			number + _T("(%1) <= 5"), number + _T("(%1) > 5"),
		};
		return fromEngine(wrapWithFilterDirectives(filterExpr,
			expressions[command - MatchNumberFirst]));
	}
	if (command >= FuncFirst && command <= FuncLast)
	{
		static const tchar_t *const functions[] = {
			_T("trim(%1)"),
			_T("regexReplace(%1, \"[ \\t]+\", \" \")"),
			_T("replace(%1, \"from\", \"to\")"),
			_T("regexReplace(%1, \"pattern\", \"replacement\")"),
			_T("toLower(%1)"),
			_T("toUpper(%1)"),
			_T("toHalfWidth(%1)"),
			_T("toFullWidth(%1)"),
			_T("normalizeUnicode(%1, \"NFC\")"),
			_T("toSimplifiedChinese(%1)"),
			_T("toTraditionalChinese(%1)"),
			_T("toHiragana(%1)"),
			_T("toKatakana(%1)"),
		};
		const String function = functions[command - FuncFirst];
		String result = wrapAttributesWithFunction(filterExpr, function);
		// a filter without a text to transform compares the two sides' lines
		if (result == filterExpr)
			result = wrapAttributesWithFunction(LineFilterHelper::AddToExpression(filterExpr,
				_T("LeftLine = RightLine"), op), function);
		return fromEngine(result);
	}
	if (command >= DiffLineLengthFirst && command <= DiffLineLengthLast)
	{
		if (m_targetDiffSide == 3)
			return added(allEqual(_T("LineLength"), command == DiffLineLengthFirst + 1));
		static const tchar_t *const conditions[] = {
			_T("%1 = %2"), _T("%1 != %2"), _T("%1 < %2"), _T("%1 <= %2"), _T("%1 > %2"),
			_T("%1 >= %2"),
			_T("abs(%1 - %2) < 10"), _T("abs(%1 - %2) >= 10"),
			_T("abs(%1 - %2) < 100"), _T("abs(%1 - %2) >= 100"),
			_T("abs(%1 - %2) < 1000"), _T("abs(%1 - %2) >= 1000"),
		};
		return added(strutils::format_string2(conditions[command - DiffLineLengthFirst],
			diffProperty(_T("LineLength"), 0), diffProperty(_T("LineLength"), 1)));
	}
	if (command >= DiffColumnFirst && command <= DiffColumnLast)
	{
		const int index = command - DiffColumnFirst;
		const String columnName = _T("Column") + strutils::to_str(m_columnIndex + 1);
		if (m_targetDiffSide == 3)
		{
			static const tchar_t *const conversions[] = {
				_T("%1"), _T("%1"),
				_T("toNumber(%1)"), _T("toNumber(%1)"), _T(""), _T(""), _T(""), _T(""),
				_T("toDateTime(%1)"), _T("toDateTime(%1)"), _T(""), _T(""), _T(""), _T(""),
			};
			static const bool negated[] = {
				false, true,
				false, true, false, false, false, false,
				false, true, false, false, false, false,
			};
			return added(allEqual(strutils::format_string1(conversions[index], columnName),
				negated[index]));
		}
		static const tchar_t *const conditions[] = {
			_T("%1 = %2"), _T("%1 != %2"),
			_T("toNumber(%1) = toNumber(%2)"), _T("toNumber(%1) != toNumber(%2)"),
			_T("toNumber(%1) < toNumber(%2)"), _T("toNumber(%1) <= toNumber(%2)"),
			_T("toNumber(%1) > toNumber(%2)"), _T("toNumber(%1) >= toNumber(%2)"),
			_T("toDateTime(%1) = toDateTime(%2)"), _T("toDateTime(%1) != toDateTime(%2)"),
			_T("toDateTime(%1) < toDateTime(%2)"), _T("toDateTime(%1) <= toDateTime(%2)"),
			_T("toDateTime(%1) > toDateTime(%2)"), _T("toDateTime(%1) >= toDateTime(%2)"),
		};
		return added(strutils::format_string2(conditions[index], diffProperty(columnName, 0),
			diffProperty(columnName, 1)));
	}
	if ((command >= StringReplaceListFirst && command <= StringReplaceListLast)
		|| (command >= RegexReplaceListFirst && command <= RegexReplaceListLast))
	{
		const bool regex = command >= RegexReplaceListFirst;
		const QStringList lists = lm::replaceLists(regex);
		const int index = command - (regex ? RegexReplaceListFirst : StringReplaceListFirst);
		if (index >= lists.size())
			return std::nullopt;
		// (a "%" of the path is doubled: the path goes into a template)
		String path = toEngine(lm::replaceListPathForExpression(lists.at(index)));
		strutils::replace(path, _T("%"), _T("%%"));
		return fromEngine(wrapAttributesWithFunction(filterExpr,
			(regex ? _T("regexReplaceWithList(%1, \"") : _T("replaceWithList(%1, \""))
			+ path + _T("\")")));
	}
	return std::nullopt;
}
