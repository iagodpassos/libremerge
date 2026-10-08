// SPDX-License-Identifier: GPL-3.0-or-later
#include "PaneSearch.h"

#include <QScrollBar>
#include <QSettings>
#include <QTextBlock>
#include <QTextBoundaryFinder>
#include <QTextDocument>

#include "DiffTextEdit.h"
#include "FindDialogs.h"
#include "MessageBoxes.h"
#include "TextMarkerDialog.h"

namespace
{

const QString kFindFlags = QStringLiteral("Editor/FindFlags");
const QString kReplaceFlags = QStringLiteral("Editor/ReplaceFlags");
const QString kMarkerFlags = QStringLiteral("Editor/MarkerFlags");

} // namespace

PaneSearch::PaneSearch(DiffTextEdit *pane, Context context, QWidget *dialogParent)
	: QObject(pane), m_pane(pane), m_context(std::move(context)),
	  m_dialogParent(dialogParent)
{
}

PaneSearch::~PaneSearch()
{
	delete m_findDialog;
	delete m_replaceDialog;
}

// --- the text ---

int PaneSearch::lineCount() const
{
	return m_pane->document()->blockCount();
}

QString PaneSearch::lineText(int line) const
{
	return m_pane->document()->findBlockByNumber(line).text();
}

int PaneSearch::lineLength(int line) const
{
	const QTextBlock block = m_pane->document()->findBlockByNumber(line);
	return block.isValid() ? block.length() - 1 : 0;
}

bool PaneSearch::lineHidden(int line) const
{
	const QTextBlock block = m_pane->document()->findBlockByNumber(line);
	return block.isValid() && !block.isVisible();
}

QString PaneSearch::eol() const
{
	const QString lineBreak = m_context.eol ? m_context.eol() : QString();
	return lineBreak.isEmpty() ? QStringLiteral("\n") : lineBreak;
}

int PaneSearch::positionOf(lm::TextPoint point) const
{
	const QTextDocument *doc = m_pane->document();
	const QTextBlock block = doc->findBlockByNumber(qBound(0, point.y, doc->blockCount() - 1));
	return block.position() + qBound(0, point.x, block.length() - 1);
}

lm::TextPoint PaneSearch::pointAt(int position) const
{
	const QTextBlock block = m_pane->document()->findBlock(position);
	return { position - block.position(), block.blockNumber() };
}

lm::TextPoint PaneSearch::cursorPos() const
{
	return pointAt(m_pane->textCursor().position());
}

bool PaneSearch::isSelection() const
{
	return m_pane->textCursor().hasSelection();
}

std::pair<lm::TextPoint, lm::TextPoint> PaneSearch::selection() const
{
	const QTextCursor cursor = m_pane->textCursor();
	return { pointAt(cursor.selectionStart()), pointAt(cursor.selectionEnd()) };
}

void PaneSearch::setSelection(lm::TextPoint start, lm::TextPoint end)
{
	QTextCursor cursor(m_pane->document());
	cursor.setPosition(positionOf(start));
	cursor.setPosition(positionOf(end), QTextCursor::KeepAnchor);
	m_pane->setTextCursor(cursor);
}

lm::TextPoint PaneSearch::searchPos(unsigned flags) const
{
	if (!isSelection())
		return cursorPos();
	const auto [start, end] = selection();
	return (flags & lm::FindDirectionUp) ? start : end;
}

QString PaneSearch::text(lm::TextPoint start, lm::TextPoint end) const
{
	QTextCursor cursor(m_pane->document());
	cursor.setPosition(positionOf(start));
	cursor.setPosition(positionOf(end), QTextCursor::KeepAnchor);
	QString selected = cursor.selectedText();
	selected.replace(QChar::ParagraphSeparator, eol());
	return selected;
}

lm::TextPoint PaneSearch::wordToRight(lm::TextPoint point) const
{
	const QString line = lineText(point.y);
	if (point.x < line.size())
	{
		QTextBoundaryFinder finder(QTextBoundaryFinder::Word, line);
		finder.setPosition(point.x);
		const int next = static_cast<int>(finder.toNextBoundary());
		point.x = next < 0 ? static_cast<int>(line.size()) : next;
	}
	return point;
}

lm::TextPoint PaneSearch::wordToLeft(lm::TextPoint point) const
{
	const QString line = lineText(point.y);
	if (point.x > 0)
	{
		// the start of the stretch that ends at the next break
		QTextBoundaryFinder finder(QTextBoundaryFinder::Word, line);
		finder.setPosition(qMin(point.x, static_cast<int>(line.size())));
		int next = static_cast<int>(finder.toNextBoundary());
		if (next < 0)
			next = static_cast<int>(line.size());
		finder.setPosition(next);
		const int previous = static_cast<int>(finder.toPreviousBoundary());
		point.x = previous < 0 ? 0 : previous;
	}
	return point;
}

/** The word at the cursor, as upstream's dialogs and Ctrl+F3 take it. */
QString PaneSearch::wordAtCursor() const
{
	const lm::TextPoint cursor = cursorPos();
	const lm::TextPoint start = wordToLeft(cursor);
	const lm::TextPoint end = wordToRight(cursor);
	return start != end ? text(start, end) : QString();
}

// --- the search ---

/** Where an offset into lines joined by line breaks is, the first of them
    being the given line. */
lm::TextPoint PaneSearch::pointInJoined(const QString &joined, int offset, int firstLine) const
{
	const QStringView before = QStringView(joined).left(offset);
	const int lf = static_cast<int>(before.lastIndexOf(QLatin1Char('\n')));
	const int cr = static_cast<int>(before.lastIndexOf(QLatin1Char('\r')));
	const int lineStart = lf >= 0 ? lf + 1 : cr >= 0 ? cr + 1 : 0;
	return { qMax(0, offset - lineStart), firstLine + lm::howManyEols(before) };
}

bool PaneSearch::findText(const QString &what, lm::TextPoint start, unsigned flags,
	bool wrap, lm::TextPoint *found)
{
	lm::TextMarkers::instance()->setMarker(lm::TextMarkers::searchKey(), what, flags,
		lm::TextMarkers::SearchColor, false);
	const int lines = lineCount();
	return findTextInBlock(what, start, { 0, 0 },
		{ lineLength(lines - 1), lines - 1 }, flags, wrap, found);
}

bool PaneSearch::findTextInBlock(const QString &what, lm::TextPoint start,
	lm::TextPoint blockBegin, lm::TextPoint blockEnd, unsigned flags, bool wrap,
	lm::TextPoint *found)
{
	if (what.isEmpty() || blockBegin == blockEnd)
		return false;
	lm::TextPoint current = start;
	if (current < blockBegin)
		current = blockBegin;
	const QString lineBreak = eol();
	const int lines = lineCount();
	// a regular expression with line breaks in it is matched against as
	// many lines more, joined by the file's line break
	int eolns = 0;
	if (flags & lm::FindRegExp)
	{
		QString what2 = what;
		what2.replace(QStringLiteral("\\r"), QStringLiteral("\r"));
		what2.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
		eolns = lm::howManyEols(what2);
	}

	if (flags & lm::FindDirectionUp)
	{
		// up, upstream searches the whole text, whatever the block
		for (;;)
		{
			while (current.y >= 0)
			{
				if (lineHidden(current.y))
				{
					if (--current.y >= 0)
						current.x = lineLength(current.y);
					continue;
				}
				// the line up to the place, after the lines before it that
				// the line breaks take in; the last match there
				const int first = qMax(0, current.y - eolns);
				QString joined;
				int limit = 0;
				for (int y = first; y <= current.y; ++y)
				{
					if (y > first)
						joined += lineBreak;
					if (y == current.y)
						limit = static_cast<int>(joined.size()) + qBound(0, current.x, lineLength(y));
					joined += lineText(y);
				}
				int foundAt = -1;
				QRegularExpressionMatch foundMatch;
				for (int from = 0; from <= limit;)
				{
					QRegularExpressionMatch match;
					const int at = lm::findStringHelper(joined, from, what, flags,
						&m_lastFindWhatLen, &match, limit);
					if (at < 0)
						break;
					foundAt = at;
					foundMatch = match;
					from = at + (m_lastFindWhatLen == 0 ? 1 : m_lastFindWhatLen);
				}
				if (foundAt >= 0)
				{
					m_match = foundMatch;
					m_hasMatch = (flags & lm::FindRegExp) != 0;
					*found = pointInJoined(joined, foundAt, first);
					return true;
				}
				if (--current.y >= 0)
					current.x = lineLength(current.y);
			}
			if (!wrap)
				return false;
			// on from the end of the text
			wrap = false;
			current = { lineLength(lines - 1), lines - 1 };
		}
	}

	for (;;)
	{
		while (current.y <= blockEnd.y)
		{
			if (lineHidden(current.y))
			{
				current.x = 0;
				++current.y;
				continue;
			}
			QString line;
			if (flags & lm::FindRegExp)
			{
				for (int i = 0; i <= eolns && current.y + i < lines; ++i)
				{
					if (i > 0)
						line += lineBreak;
					line += lineText(current.y + i);
				}
			}
			else
			{
				if (lineLength(current.y) - current.x <= 0)
				{
					current.x = 0;
					++current.y;
					continue;
				}
				line = lineText(current.y);
			}
			QRegularExpressionMatch match;
			const int pos = lm::findStringHelper(line, current.x, what, flags,
				&m_lastFindWhatLen, &match);
			if (pos >= 0)
			{
				m_match = match;
				m_hasMatch = (flags & lm::FindRegExp) != 0;
				const lm::TextPoint at = eolns > 0 ? pointInJoined(line, pos, current.y)
					: lm::TextPoint{ pos, current.y };
				// found past the block's end: not in it
				if (!(at < blockEnd))
					break;
				*found = at;
				return true;
			}
			m_hasMatch = false;
			current.x = 0;
			++current.y;
		}
		if (!wrap)
			return false;
		// on from the block's start
		wrap = false;
		current = blockBegin;
	}
}

bool PaneSearch::findText(const lm::LastSearchInfos &lastSearch)
{
	const unsigned flags = lm::searchInfosToFlags(lastSearch);
	lm::TextPoint found;
	if (!findText(lastSearch.text, searchPos(flags), flags, !lastSearch.noWrap, &found))
		return false;
	highlightText(found, m_lastFindWhatLen, lastSearch.direction == 0);
	// for F3, and for the next session's Find dialog
	m_lastSearch = true;
	m_lastFindWhat = lastSearch.text;
	m_lastSearchFlags = flags;
	QSettings().setValue(kFindFlags, flags);
	return true;
}

void PaneSearch::highlightText(lm::TextPoint start, int length, bool cursorToLeft,
	bool updateView)
{
	// the end, past the line breaks the length takes in
	const int eolLength = static_cast<int>(eol().size());
	lm::TextPoint end = start;
	int count = lineLength(end.y) - end.x;
	if (length <= count)
		end.x += length;
	else
	{
		while (length > count && end.y + 1 < lineCount())
		{
			length -= count + eolLength;
			count = lineLength(++end.y);
		}
		end.x = qBound(0, length, count);
	}

	QScrollBar *bar = m_pane->verticalScrollBar();
	const int top = bar->value();
	QTextCursor cursor(m_pane->document());
	cursor.setPosition(positionOf(cursorToLeft ? end : start));
	cursor.setPosition(positionOf(cursorToLeft ? start : end), QTextCursor::KeepAnchor);
	m_pane->setTextCursor(cursor);
	if (!updateView)
		return;
	// out of the pane, what was found comes to its middle
	const int screen = m_pane->visibleLineCount();
	const auto shown = [this](int line) {
		return m_context.shownLine ? m_context.shownLine(line) : line;
	};
	const int startLine = shown(start.y);
	if (startLine < top || shown(end.y) > top + screen)
		bar->setValue(startLine > screen / 2 ? startLine - screen / 2 : startLine);
	m_pane->ensureCursorVisible();
}

void PaneSearch::ensureSelectionVisible()
{
	m_pane->ensureCursorVisible();
}

void PaneSearch::focusPane()
{
	m_pane->activateWindow();
	m_pane->setFocus(Qt::OtherFocusReason);
}

void PaneSearch::tellNotFound(QWidget *parent, const QString &text)
{
	lm::showInformation(parent, tr("Cannot find string \"%1\".").arg(text));
}

// --- the commands ---

void PaneSearch::editFind()
{
	if (m_findDialog == nullptr)
		m_findDialog = new FindTextDialog(this, m_dialogParent);
	lm::LastSearchInfos *lastSearch = m_findDialog->lastSearchInfos();
	if (m_lastSearch)
	{
		lm::flagsToSearchInfos(lastSearch, m_lastSearchFlags);
		lastSearch->text = m_lastFindWhat;
	}
	else
	{
		lm::flagsToSearchInfos(lastSearch, QSettings().value(kFindFlags,
			static_cast<unsigned>(lm::FindNoClose)).toUInt());
	}
	m_findDialog->useLastSearch();
	// the selection, when it is on one line, or the word at the cursor
	if (isSelection())
	{
		const auto [start, end] = selection();
		if (start.y == end.y)
			m_findDialog->m_text = text(start, end);
	}
	else
	{
		const QString word = wordAtCursor();
		if (!word.isEmpty())
			m_findDialog->m_text = word;
	}
	m_findDialog->updateData(false);
	m_findDialog->show();
	m_findDialog->raise();
	m_findDialog->activateWindow();
}

void PaneSearch::editRepeat(bool control, bool shift)
{
	bool enable = m_lastSearch && !m_lastFindWhat.isEmpty();
	QString what = enable ? m_lastFindWhat : QString();
	if (control)
	{
		if (isSelection())
		{
			const auto [start, end] = selection();
			what = text(start, end);
		}
		else
		{
			const QString word = wordAtCursor();
			if (!word.isEmpty())
				what = word;
		}
		if (!what.isEmpty())
		{
			enable = true;
			m_lastFindWhat = what;
			m_lastSearch = true;
		}
	}
	if (shift)
		m_lastSearchFlags |= lm::FindDirectionUp;
	else
		m_lastSearchFlags &= ~static_cast<unsigned>(lm::FindDirectionUp);
	if (!enable)
	{
		// nothing searched for yet: the Find dialog
		editFind();
		return;
	}
	lm::TextPoint found;
	if (!findText(what, searchPos(m_lastSearchFlags), m_lastSearchFlags,
		(m_lastSearchFlags & lm::FindNoWrap) == 0, &found))
	{
		tellNotFound(m_pane, what);
		return;
	}
	highlightText(found, m_lastFindWhatLen, (m_lastSearchFlags & lm::FindDirectionUp) != 0);
}

void PaneSearch::editReplace()
{
	if (m_context.editable && !m_context.editable())
		return;
	delete m_replaceDialog;
	m_replaceDialog = new EditReplaceDialog(this, m_dialogParent);
	lm::LastSearchInfos *lastSearch = m_replaceDialog->lastSearchInfos();
	const unsigned flags = m_lastReplace ? m_lastReplaceFlags
		: QSettings().value(kReplaceFlags, 0u).toUInt();
	lastSearch->matchCase = (flags & lm::FindMatchCase) != 0;
	lastSearch->wholeWord = (flags & lm::FindWholeWord) != 0;
	lastSearch->regExp = (flags & lm::FindRegExp) != 0;
	lastSearch->noWrap = (flags & lm::FindNoWrap) != 0;
	if (m_lastReplace)
		lastSearch->text = m_lastFindWhat;
	lastSearch->direction = 1;
	m_replaceDialog->useLastSearch();

	if (isSelection())
	{
		const auto [start, end] = selection();
		m_savedSelStart = trackPoint(start);
		m_savedSelEnd = trackPoint(end);
		m_selectionPushed = true;
		// a selection over more than one line is where to replace
		m_replaceDialog->setScope(start.y != end.y);
		m_replaceDialog->m_currentPos = start;
		m_replaceDialog->m_enableScopeSelection = true;
		m_replaceDialog->m_blockBegin = start;
		m_replaceDialog->m_blockEnd = end;
		// a selection within a line is the text to replace
		if (start.y == end.y)
			m_replaceDialog->m_text = text(start, end);
	}
	else
	{
		m_selectionPushed = false;
		m_replaceDialog->setScope(false);
		m_replaceDialog->m_currentPos = cursorPos();
		m_replaceDialog->m_enableScopeSelection = false;
		const QString word = wordAtCursor();
		if (!word.isEmpty())
			m_replaceDialog->m_text = word;
	}
	m_replaceDialog->initDialog();
	m_replaceDialog->show();
	m_replaceDialog->raise();
	m_replaceDialog->activateWindow();
}

void PaneSearch::editMark()
{
	const unsigned flags = QSettings().value(kMarkerFlags, 0u).toUInt();
	QString pattern;
	if (isSelection())
	{
		const auto [start, end] = selection();
		if (start.y == end.y)
			pattern = text(start, end);
	}
	else
		pattern = wordAtCursor();
	TextMarkerDialog dialog(pattern, flags, m_pane->window());
	if (!TextMarkerDialog::run(&dialog))
		return;
	QSettings().setValue(kMarkerFlags, dialog.lastSearchFlags());
	lm::TextMarkers::instance()->save();
}

// --- replacing ---

void PaneSearch::saveLastSearch(const lm::LastSearchInfos &lastSearch)
{
	m_lastReplace = true;
	m_lastFindWhat = lastSearch.text;
	m_lastReplaceFlags = 0;
	if (lastSearch.matchCase)
		m_lastReplaceFlags |= lm::FindMatchCase;
	if (lastSearch.wholeWord)
		m_lastReplaceFlags |= lm::FindWholeWord;
	if (lastSearch.regExp)
		m_lastReplaceFlags |= lm::FindRegExp;
	if (lastSearch.noWrap)
		m_lastReplaceFlags |= lm::FindNoWrap;
	QSettings().setValue(kReplaceFlags, m_lastReplaceFlags);
}

QTextCursor PaneSearch::trackPoint(lm::TextPoint point) const
{
	QTextCursor tracker(m_pane->document());
	tracker.setPosition(positionOf(point));
	tracker.setKeepPositionOnInsert(true);
	return tracker;
}

lm::TextPoint PaneSearch::pointOf(const QTextCursor &tracker) const
{
	return tracker.isNull() ? lm::TextPoint() : pointAt(tracker.position());
}

/** Out goes the text between two positions; the hidden lines in between
    stay, as upstream's DeleteText keeps them. */
void PaneSearch::removeRange(int from, int to)
{
	if (to <= from)
		return;
	if (m_pane->rangeSpansHiddenLine(from, to))
	{
		m_pane->removeKeepingHidden(from, to);
		return;
	}
	QTextCursor cursor(m_pane->document());
	cursor.setPosition(from);
	cursor.setPosition(to, QTextCursor::KeepAnchor);
	cursor.removeSelectedText();
}

void PaneSearch::deleteCurrentSelection()
{
	if (!isSelection())
		return;
	const auto [start, end] = selection();
	QTextCursor edit(m_pane->document());
	edit.beginEditBlock();
	removeRange(positionOf(start), positionOf(end));
	edit.endEditBlock();
	QTextCursor cursor(m_pane->document());
	cursor.setPosition(positionOf(start));
	m_pane->setTextCursor(cursor);
}

bool PaneSearch::replaceSelection(const QString &newText, unsigned flags)
{
	if (newText.isEmpty())
	{
		deleteCurrentSelection();
		m_lastReplaceLen = 0;
		return true;
	}
	QTextDocument *doc = m_pane->document();
	QTextCursor edit(doc);
	edit.beginEditBlock();
	lm::TextPoint at;
	if (isSelection())
	{
		const auto [start, end] = selection();
		at = start;
		removeRange(positionOf(start), positionOf(end));
	}
	else
		at = cursorPos();

	QString inserted = newText;
	if (flags & lm::FindRegExp)
	{
		// the regular expression's replacement for its match; none when it
		// cannot be made, as upstream's
		if (!m_hasMatch || !lm::rxReplace(newText, m_match, &inserted))
			inserted.clear();
	}
	m_lastReplaceLen = static_cast<int>(inserted.size());
	// a line break of any kind is one in the pane
	inserted.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	inserted.replace(QLatin1Char('\r'), QLatin1Char('\n'));
	QTextCursor cursor(doc);
	cursor.setPosition(positionOf(at));
	cursor.insertText(inserted);
	lm::TextPoint endOfBlock = pointAt(cursor.position());
	edit.endEditBlock();
	// at a line's end, the selection takes its line break in
	if (endOfBlock.x == lineLength(endOfBlock.y) && endOfBlock.y < lineCount() - 1)
	{
		endOfBlock.x = 0;
		++endOfBlock.y;
	}
	setSelection(at, endOfBlock);
	return true;
}
