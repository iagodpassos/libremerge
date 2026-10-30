// SPDX-License-Identifier: GPL-3.0-or-later
#include "DiffTextEdit.h"

#include "FindText.h"
#include "Theme.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextDocumentLayout>
#include <QTextBlock>
#include <QTextBoundaryFinder>
#include <QTimer>
#include <QUrl>

namespace
{

class GutterWidget : public QWidget
{
public:
	explicit GutterWidget(DiffTextEdit *editor)
		: QWidget(editor), m_editor(editor) {}

	QSize sizeHint() const override
	{
		return QSize(m_editor->gutterWidth(), 0);
	}

protected:
	void paintEvent(QPaintEvent *event) override
	{
		m_editor->paintGutter(event);
	}

private:
	DiffTextEdit *m_editor;
};

/** A key that puts text where the selection is, as QWidgetTextControl
    decides it: a new paragraph or line, or input that prints (a chord
    with Ctrl alone is a command, not input). */
bool insertsText(const QKeyEvent *event)
{
	if (event->matches(QKeySequence::InsertParagraphSeparator)
		|| event->matches(QKeySequence::InsertLineSeparator))
		return true;
	const QString text = event->text();
	if (text.isEmpty())
		return false;
	if (event->modifiers() == Qt::ControlModifier
		|| event->modifiers() == (Qt::ShiftModifier | Qt::ControlModifier))
		return false;
	const QChar first = text.at(0);
	return first.isPrint() || first == QLatin1Char('\t')
		|| first.category() == QChar::Other_Format
		|| first.category() == QChar::Other_PrivateUse;
}

} // namespace

DiffTextEdit::DiffTextEdit(QWidget *parent)
	: QPlainTextEdit(parent)
{
	m_gutter = new GutterWidget(this);
	connect(this, &QPlainTextEdit::blockCountChanged,
		this, &DiffTextEdit::updateGutterWidth);
	connect(this, &QPlainTextEdit::updateRequest,
		this, &DiffTextEdit::updateGutter);
	connect(this, &QPlainTextEdit::cursorPositionChanged,
		this, &DiffTextEdit::keepCursorOnShownLine);
	// the search marker follows the lines on screen, what they hold, the
	// text searched for and the theme's colors
	connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &, int dy) {
		if (dy != 0)
			refreshMarkers(false);
	});
	connect(document(), &QTextDocument::contentsChange,
		this, &DiffTextEdit::scheduleMarkerRefresh);
	connect(lm::TextMarkers::instance(), &lm::TextMarkers::changed,
		this, &DiffTextEdit::scheduleMarkerRefresh);
	connect(lm::Theme::instance(), &lm::Theme::changed,
		this, &DiffTextEdit::scheduleMarkerRefresh);
	updateGutterWidth();
	applyTabStops();
}

// --- hidden lines ---

void DiffTextEdit::setHiddenLines(const QList<bool> &hidden)
{
	QTextDocument *doc = document();
	bool any = false;
	bool changed = false;
	int line = 0;
	for (QTextBlock block = doc->begin(); block.isValid(); block = block.next(), ++line)
	{
		const bool hide = line < hidden.size() && hidden.at(line);
		any = any || hide;
		if (block.isVisible() == !hide)
			continue;
		block.setVisible(!hide);
		// no line of its own in the layout: the scroll range and the
		// positions of the lines below go by the lines that show
		block.setLineCount(hide ? 0 : 1);
		changed = true;
	}
	m_hasHiddenLines = any;
	if (!changed)
		return;
	if (auto *layout = qobject_cast<QPlainTextDocumentLayout *>(doc->documentLayout()))
	{
		layout->requestUpdate();
		emit layout->documentSizeChanged(layout->documentSize());
	}
	viewport()->update();
	m_gutter->update();
	keepCursorOnShownLine();
}

bool DiffTextEdit::isLineHidden(int viewLine) const
{
	const QTextBlock block = document()->findBlockByNumber(viewLine);
	return block.isValid() && !block.isVisible();
}

/** A shown line with a hidden one right after it. */
bool DiffTextEdit::endsShownStretch(const QTextBlock &block) const
{
	const QTextBlock next = block.next();
	return block.isVisible() && next.isValid() && !next.isVisible();
}

int DiffTextEdit::lineBottomForTest(int viewLine) const
{
	const QTextBlock block = document()->findBlockByNumber(viewLine);
	if (!block.isValid() || !block.isVisible())
		return -1;
	const QRectF geometry = blockBoundingGeometry(block).translated(contentOffset());
	return qRound(geometry.top()) + qRound(blockBoundingRect(block).height());
}

bool DiffTextEdit::spansHiddenLine(int from, int to) const
{
	const QTextBlock last = document()->findBlock(qMax(from, to));
	for (QTextBlock block = document()->findBlock(qMin(from, to)); block.isValid();
		block = block.next())
	{
		if (!block.isVisible())
			return true;
		if (block == last)
			break;
	}
	return false;
}

bool DiffTextEdit::selectionSpansHiddenLine() const
{
	const QTextCursor cursor = textCursor();
	return cursor.hasSelection()
		&& spansHiddenLine(cursor.selectionStart(), cursor.selectionEnd());
}

/** The cursor never rests in a hidden line: what took it there goes on
    to the line that shows next in the same direction (upstream's editor
    moves by the lines on screen, which leaves the hidden ones out). The
    one exception is a selection that runs on into hidden lines the text
    ends with, as Select All makes it: it keeps its end there, so that what
    removes it takes the last line that shows whole, as upstream does. */
void DiffTextEdit::keepCursorOnShownLine()
{
	QTextCursor cursor = textCursor();
	const int previous = m_lastCursorPosition;
	m_lastCursorPosition = cursor.position();
	if (!m_hasHiddenLines || m_movingCursor || cursor.block().isVisible())
		return;
	const bool forward = cursor.position() >= previous;
	QTextBlock after = cursor.block().next();
	while (after.isValid() && !after.isVisible())
		after = after.next();
	QTextBlock before = cursor.block().previous();
	while (before.isValid() && !before.isVisible())
		before = before.previous();
	if (forward && !after.isValid() && cursor.hasSelection())
		return;
	int target = -1;
	if (forward ? after.isValid() : !before.isValid())
	{
		if (after.isValid())
			target = after.position();
	}
	else if (before.isValid())
		target = before.position() + before.length() - 1;
	if (target < 0)
		return; // no line shows at all
	cursor.setPosition(target, cursor.hasSelection()
		? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
	m_movingCursor = true;
	setTextCursor(cursor);
	m_movingCursor = false;
	m_lastCursorPosition = target;
}

void DiffTextEdit::removeKeepingHidden(int from, int to)
{
	QTextDocument *doc = document();
	const int startPos = qMin(from, to);
	const int endPos = qMax(from, to);
	const QTextBlock startBlock = doc->findBlock(startPos);
	const QTextBlock endBlock = doc->findBlock(endPos);
	const int startLine = startBlock.blockNumber();
	const int endLine = endBlock.blockNumber();
	const int startChar = startPos - startBlock.position();
	const int endChar = endPos - endBlock.position();
	const auto lineStart = [doc](int line) { return doc->findBlockByNumber(line).position(); };
	const auto lineEnd = [doc](int line) {
		const QTextBlock block = doc->findBlockByNumber(line);
		return block.position() + block.length() - 1;
	};

	QTextCursor cursor(doc);
	cursor.beginEditBlock();
	const auto remove = [&cursor](int a, int b) {
		if (b <= a)
			return;
		cursor.setPosition(a);
		cursor.setPosition(b, QTextCursor::KeepAnchor);
		cursor.removeSelectedText();
	};
	// from the last line to the first, a stretch of visible lines at a time
	for (int line = endLine; line >= startLine; --line)
	{
		if (!doc->findBlockByNumber(line).isVisible())
			continue;
		const int last = line;
		int first = line - 1;
		while (first >= startLine && doc->findBlockByNumber(first).isVisible())
			--first;
		++first;
		line = first;
		const int firstChar = first == startLine ? startChar : 0;
		if (last == endLine)
		{
			// the range ends in this stretch
			remove(lineStart(first) + firstChar, lineStart(last) + endChar);
		}
		else if (firstChar > 0)
		{
			// what is left of the stretch's first line joins the hidden
			// line that follows, which shows from then on (as upstream)
			remove(lineStart(first) + firstChar, lineStart(last + 1));
		}
		else if (first > 0)
		{
			// whole lines, with their line breaks: taken from the end of
			// the line before, so that the hidden line that follows stays
			// the block it is
			remove(lineEnd(first - 1), lineEnd(last));
		}
		else
		{
			// whole lines from the top of the text: their text first, which
			// leaves the first block empty, then its line break. An empty
			// block is the one that goes with its line break, so the hidden
			// line that follows stays the block it is, and an undo brings
			// the lines back without touching it
			remove(0, lineEnd(last));
			remove(0, 1);
		}
	}
	cursor.endEditBlock();

	if (auto *layout = qobject_cast<QPlainTextDocumentLayout *>(doc->documentLayout()))
	{
		layout->requestUpdate();
		emit layout->documentSizeChanged(layout->documentSize());
	}
	QTextCursor place = textCursor();
	place.setPosition(qMin(startPos, doc->characterCount() - 1));
	setTextCursor(place);
}

void DiffTextEdit::removeSelectionKeepingHidden()
{
	const QTextCursor cursor = textCursor();
	if (cursor.hasSelection())
		removeKeepingHidden(cursor.selectionStart(), cursor.selectionEnd());
}

// --- tabs ---

void DiffTextEdit::setTabSize(int tabSize)
{
	m_tabSize = qBound(1, tabSize, 64);
	applyTabStops();
}

/** A tab stop every tab size columns, a column being the font's space. */
void DiffTextEdit::applyTabStops()
{
	setTabStopDistance(m_tabSize * QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')));
}

void DiffTextEdit::changeEvent(QEvent *event)
{
	QPlainTextEdit::changeEvent(event);
	// another text size, other columns
	if (event->type() == QEvent::FontChange)
		applyTabStops();
}

int DiffTextEdit::cellCount(const QString &grapheme) const
{
	const char16_t ch = grapheme.at(0).unicode();
	if (ch <= 0x7F)
		return ch <= 0x1F ? 3 : 1; // (a tab is counted apart)
	// upstream measures the glyph against the font's character: one wider
	// than one and a half takes two columns
	const QFontMetricsF metrics(font());
	return metrics.horizontalAdvance(grapheme) * 10
		> metrics.horizontalAdvance(QLatin1Char(' ')) * 15 ? 2 : 1;
}

int DiffTextEdit::actualOffset(const QTextBlock &block, int positionInBlock) const
{
	const QString text = block.text();
	const int end = qBound(0, positionInBlock, static_cast<int>(text.size()));
	QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, text);
	int offset = 0;
	for (int i = 0; i < end;)
	{
		graphemes.setPosition(i);
		const int next = qMax(i + 1, static_cast<int>(graphemes.toNextBoundary()));
		if (text.at(i) == QLatin1Char('\t'))
			offset += m_tabSize - offset % m_tabSize;
		else
			offset += cellCount(text.mid(i, next - i));
		i = next;
	}
	return offset;
}

DiffTextEdit::TabifyRange DiffTextEdit::tabifyRange(const QTextCursor &cursor) const
{
	// the selection's lines, the last one left out when the selection
	// ends at its start; the selection then goes on to the start of the
	// line after the last one, or to the end of the text's last line
	const QTextDocument *doc = document();
	const QTextBlock last = doc->findBlock(cursor.selectionEnd());
	TabifyRange range;
	range.firstLine = doc->findBlock(cursor.selectionStart()).blockNumber();
	range.lastLine = last.blockNumber();
	range.endLine = range.lastLine;
	if (cursor.selectionEnd() > last.position())
	{
		if (range.lastLine == doc->blockCount() - 1)
			range.endsAtLineEnd = true;
		else
			++range.endLine;
	}
	else
		--range.lastLine;
	return range;
}

void DiffTextEdit::selectTabified(const TabifyRange &range)
{
	QTextDocument *doc = document();
	QTextCursor selection(doc);
	selection.setPosition(doc->findBlockByNumber(range.firstLine).position());
	const QTextBlock end = doc->findBlockByNumber(range.endLine);
	selection.setPosition(range.endsAtLineEnd ? end.position() + end.length() - 1
		: end.position(), QTextCursor::KeepAnchor);
	setTextCursor(selection);
	ensureCursorVisible();
}

void DiffTextEdit::editTab()
{
	QTextCursor cursor = textCursor();
	QTextDocument *doc = document();
	const bool tabify = cursor.hasSelection()
		&& doc->findBlock(cursor.selectionStart()) != doc->findBlock(cursor.selectionEnd());
	QString text = QStringLiteral("\t");
	if (!m_insertTabs)
	{
		// spaces to the next tab stop, a whole tab's worth over a
		// selection. Upstream counts from the cursor's character rather
		// than its column, which a tab before the cursor puts out of
		// step: the column here
		const int column = cursor.hasSelection()
			? 0 : actualOffset(cursor.block(), cursor.positionInBlock());
		text = QString(m_tabSize - column % m_tabSize, QLatin1Char(' '));
	}
	if (tabify)
	{
		// every line of the selection, as one undo step
		const TabifyRange range = tabifyRange(cursor);
		QTextCursor edit(doc);
		edit.beginEditBlock();
		for (int line = range.firstLine; line <= range.lastLine; ++line)
		{
			const QTextBlock block = doc->findBlockByNumber(line);
			// a ghost line stays one (CGhostTextBuffer::IsIndentableLine)
			if (isGhostBlock(block))
				continue;
			edit.setPosition(block.position());
			edit.insertText(text);
		}
		edit.endEditBlock();
		selectTabified(range);
		return;
	}
	// in place of the selection, or at the cursor
	cursor.beginEditBlock();
	cursor.insertText(text);
	cursor.endEditBlock();
	setTextCursor(cursor);
	ensureCursorVisible();
}

void DiffTextEdit::editUntab()
{
	QTextCursor cursor = textCursor();
	QTextDocument *doc = document();
	const bool tabify = cursor.hasSelection()
		&& doc->findBlock(cursor.selectionStart()) != doc->findBlock(cursor.selectionEnd());
	if (tabify)
	{
		// from every line of the selection, as one undo step: a tab's
		// worth of spaces, or the spaces up to a tab and the tab
		const TabifyRange range = tabifyRange(cursor);
		QTextCursor edit(doc);
		edit.beginEditBlock();
		for (int line = range.firstLine; line <= range.lastLine; ++line)
		{
			const QTextBlock block = doc->findBlockByNumber(line);
			const QString text = block.text();
			int pos = 0;
			int offset = 0;
			while (pos < text.size())
			{
				if (text.at(pos) == QLatin1Char(' '))
				{
					++pos;
					if (++offset >= m_tabSize)
						break;
				}
				else
				{
					if (text.at(pos) == QLatin1Char('\t'))
						++pos;
					break;
				}
			}
			if (pos == 0)
				continue;
			edit.setPosition(block.position());
			edit.setPosition(block.position() + pos, QTextCursor::KeepAnchor);
			edit.removeSelectedText();
		}
		edit.endEditBlock();
		selectTabified(range);
		return;
	}
	// without one, the cursor goes back to the tab stop before it
	const QTextBlock block = cursor.block();
	const int position = cursor.positionInBlock();
	if (position == 0)
		return;
	const int offset = actualOffset(block, position);
	int newOffset = offset / m_tabSize * m_tabSize;
	if (offset == newOffset && newOffset > 0)
		newOffset -= m_tabSize;
	// the character at that column, the columns counted as the cursor's
	// were (upstream counts one for each character here, which a wide
	// one puts out of step)
	const QString text = block.text();
	QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, text);
	int column = 0;
	int i = 0;
	while (column < newOffset && i < text.size())
	{
		graphemes.setPosition(i);
		const int next = qMax(i + 1, static_cast<int>(graphemes.toNextBoundary()));
		if (text.at(i) == QLatin1Char('\t'))
			column = column / m_tabSize * m_tabSize + m_tabSize;
		else
			column += cellCount(text.mid(i, next - i));
		i = next;
	}
	cursor.setPosition(block.position() + i);
	setTextCursor(cursor);
	ensureCursorVisible();
}

/** What a key is about to remove stops at the hidden lines: the
    selection it types over, cuts or deletes, or the stretch a Backspace
    or a Delete takes without one. Tab and Shift+Tab are upstream's own
    commands. */
void DiffTextEdit::keyPressEvent(QKeyEvent *event)
{
	if (!isReadOnly())
	{
		const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
		if (event->key() == Qt::Key_Tab && modifiers == Qt::NoModifier)
		{
			editTab();
			return;
		}
		if ((event->key() == Qt::Key_Backtab
				&& (modifiers == Qt::ShiftModifier || modifiers == Qt::NoModifier))
			|| (event->key() == Qt::Key_Tab && modifiers == Qt::ShiftModifier))
		{
			editUntab();
			return;
		}
	}
	if (!m_hasHiddenLines || isReadOnly())
	{
		QPlainTextEdit::keyPressEvent(event);
		return;
	}
	const QTextCursor cursor = textCursor();
	const bool backspace = event->key() == Qt::Key_Backspace
		&& !(event->modifiers() & ~Qt::ShiftModifier);
	const bool cut = event->matches(QKeySequence::Cut);
	const bool types = insertsText(event);
	int from = -1;
	int to = -1;
	if (cursor.hasSelection())
	{
		if (cut || backspace || types || event->matches(QKeySequence::Delete)
			|| event->matches(QKeySequence::DeleteStartOfWord)
			|| event->matches(QKeySequence::DeleteEndOfWord)
			|| event->matches(QKeySequence::DeleteEndOfLine))
		{
			from = cursor.selectionStart();
			to = cursor.selectionEnd();
		}
	}
	else
	{
		QTextCursor probe = cursor;
		if (backspace)
			probe.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
		else if (event->matches(QKeySequence::Delete))
			probe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
		else if (event->matches(QKeySequence::DeleteStartOfWord))
			probe.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
		else if (event->matches(QKeySequence::DeleteEndOfWord))
			probe.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
		if (probe.hasSelection())
		{
			from = probe.selectionStart();
			to = probe.selectionEnd();
		}
	}
	if (from < 0 || !spansHiddenLine(from, to))
	{
		QPlainTextEdit::keyPressEvent(event);
		return;
	}
	if (cut)
		copy();
	// one undo step with what the key then types
	QTextCursor group(document());
	group.beginEditBlock();
	removeKeepingHidden(from, to);
	if (types)
		QPlainTextEdit::keyPressEvent(event);
	group.endEditBlock();
	if (!types)
		event->accept();
}

void DiffTextEdit::inputMethodEvent(QInputMethodEvent *event)
{
	// composed input replaces the selection as typing does
	if (m_hasHiddenLines && !isReadOnly() && selectionSpansHiddenLine()
		&& (!event->commitString().isEmpty() || !event->preeditString().isEmpty()
			|| event->replacementLength() > 0))
		removeSelectionKeepingHidden();
	QPlainTextEdit::inputMethodEvent(event);
}

namespace
{
std::function<void(QMenu *)> &contextMenuPresenter()
{
	static std::function<void(QMenu *)> presenter;
	return presenter;
}
} // namespace

void DiffTextEdit::setContextMenuPresenterForTest(std::function<void(QMenu *menu)> presenter)
{
	contextMenuPresenter() = std::move(presenter);
}

void DiffTextEdit::presentMenu(QMenu *menu, const QPoint &globalPos)
{
	if (contextMenuPresenter())
		contextMenuPresenter()(menu);
	else
		menu->exec(globalPos);
}

void DiffTextEdit::setViewWhitespace(bool on)
{
	m_viewWhitespace = on;
	viewport()->update();
}

void DiffTextEdit::setEolGlyphs(std::function<QString(const QTextBlock &block)> glyphOf)
{
	m_eolGlyphOf = std::move(glyphOf);
	viewport()->update();
}

void DiffTextEdit::contextMenuEvent(QContextMenuEvent *event)
{
	// CCrystalTextView::OnRButtonDown: outside the selection, the cursor
	// goes where the click is and nothing stays selected; and
	// CMergeEditView::OnRButtonUp as the left button's release
	if (event->reason() == QContextMenuEvent::Mouse)
	{
		const QTextCursor clicked = cursorForPosition(event->pos());
		const QTextCursor cursor = textCursor();
		if (!cursor.hasSelection() || clicked.position() < cursor.selectionStart()
			|| clicked.position() >= cursor.selectionEnd())
			setTextCursor(clicked);
		if (m_mouseReleaseHook)
			m_mouseReleaseHook();
	}
	QMenu *menu = createStandardContextMenu(event->pos());
	if (m_hasHiddenLines && !isReadOnly() && selectionSpansHiddenLine())
	{
		// Cut and Delete would take the hidden lines of the selection
		// along (Paste comes through insertFromMimeData)
		for (QAction *action : menu->actions())
		{
			const bool cut = action->objectName() == QStringLiteral("edit-cut");
			if (!cut && action->objectName() != QStringLiteral("edit-delete"))
				continue;
			QObject::disconnect(action, &QAction::triggered, nullptr, nullptr);
			connect(action, &QAction::triggered, this, [this, cut]() {
				if (cut)
					copy();
				removeSelectionKeepingHidden();
			});
		}
	}
	if (m_contextMenuHook)
		m_contextMenuHook(menu);
	presentMenu(menu, event->globalPos());
	delete menu;
}

/** A selection dragged within the pane is moved by taking it out where
    it was: with hidden lines in it, it is copied instead. */
void DiffTextEdit::dragMoveEvent(QDragMoveEvent *event)
{
	QPlainTextEdit::dragMoveEvent(event);
	if (m_hasHiddenLines && (event->source() == this || event->source() == viewport())
		&& selectionSpansHiddenLine())
		event->setDropAction(Qt::CopyAction);
}

void DiffTextEdit::dropEvent(QDropEvent *event)
{
	if (m_hasHiddenLines && (event->source() == this || event->source() == viewport())
		&& selectionSpansHiddenLine())
		event->setDropAction(Qt::CopyAction);
	m_inTextDrop = true;
	QPlainTextEdit::dropEvent(event);
	m_inTextDrop = false;
}

bool DiffTextEdit::event(QEvent *event)
{
	// QPlainTextEdit claims Alt+arrows for word/paragraph navigation
	// (via ShortcutOverride), which starves the WinMerge-style merge
	// shortcuts (Alt+Up/Down navigate, Alt+Left/Right copy) that live
	// on the main window's menu. Let those reach the shortcut system.
	if (event->type() == QEvent::ShortcutOverride)
	{
		const auto *keyEvent = static_cast<QKeyEvent *>(event);
		const int key = keyEvent->key();
		if ((keyEvent->modifiers() & Qt::AltModifier)
			&& (key == Qt::Key_Left || key == Qt::Key_Right
				|| key == Qt::Key_Up || key == Qt::Key_Down
				|| key == Qt::Key_Home || key == Qt::Key_End))
		{
			event->ignore();
			return false;
		}
		// undo/redo go through the menu too: the compare view keeps a
		// unified undo order across the panes, so Cmd+Z must not stop
		// at this pane's own stack
		if (keyEvent->matches(QKeySequence::Undo)
			|| keyEvent->matches(QKeySequence::Redo))
		{
			event->ignore();
			return false;
		}
	}
	return QPlainTextEdit::event(event);
}

/** Copy leaves the alignment ghost lines out, like WinMerge's
    GetTextWithoutEmptys: they are visual filler, not file content. The
    hidden lines stay out as well (its bExcludeInvisibleLines). */
QMimeData *DiffTextEdit::createMimeDataFromSelection() const
{
	const QTextCursor cursor = textCursor();
	if (!cursor.hasSelection())
		return QPlainTextEdit::createMimeDataFromSelection();

	const int selStart = cursor.selectionStart();
	const int selEnd = cursor.selectionEnd();
	QStringList parts;
	for (QTextBlock block = document()->findBlock(selStart);
		block.isValid() && block.position() <= selEnd; block = block.next())
	{
		if (isGhostBlock(block) || !block.isVisible())
			continue;
		const int from = qMax(selStart, block.position()) - block.position();
		const int to = qMin(selEnd, block.position() + block.length() - 1)
			- block.position();
		parts.append(block.text().mid(from, to - from));
	}
	auto *mime = new QMimeData;
	mime->setText(parts.join(QChar('\n')));
	return mime;
}

bool DiffTextEdit::canInsertFromMimeData(const QMimeData *source) const
{
	if (source->hasUrls())
		return true;
	return QPlainTextEdit::canInsertFromMimeData(source);
}

void DiffTextEdit::insertFromMimeData(const QMimeData *source)
{
	// dropping a file loads it into this pane, like WinMerge, instead
	// of inserting the file:// URL as text. The hook is deferred to the
	// next event-loop cycle: it replaces this widget's document, and
	// doing that while QWidgetTextControl is still processing the drop
	// crashed inside centerCursor (the fresh document had no layout yet)
	if (source->hasUrls())
	{
		for (const QUrl &url : source->urls())
		{
			if (url.isLocalFile() && m_fileDropHook)
			{
				QTimer::singleShot(0, this,
					[hook = m_fileDropHook, path = url.toLocalFile()]() {
						hook(path);
					});
				return;
			}
		}
	}
	if (m_hasHiddenLines && !isReadOnly() && selectionSpansHiddenLine())
	{
		// what is pasted takes the place of the visible part of the
		// selection, in one undo step
		QTextCursor group(document());
		group.beginEditBlock();
		removeSelectionKeepingHidden();
		QPlainTextEdit::insertFromMimeData(source);
		group.endEditBlock();
		return;
	}
	QPlainTextEdit::insertFromMimeData(source);
}

void DiffTextEdit::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (m_doubleClickHook)
		m_doubleClickHook(
			cursorForPosition(event->position().toPoint()).blockNumber());
	// the default word selection still applies, like upstream
	QPlainTextEdit::mouseDoubleClickEvent(event);
}

void DiffTextEdit::mouseReleaseEvent(QMouseEvent *event)
{
	QPlainTextEdit::mouseReleaseEvent(event);
	if (event->button() == Qt::LeftButton && m_mouseReleaseHook)
		m_mouseReleaseHook();
}

// --- the search marker ---

void DiffTextEdit::setHighlightSelections(const QList<QTextEdit::ExtraSelection> &selections)
{
	m_baseSelections = selections;
	m_markerSelections = markerSelections();
	QPlainTextEdit::setExtraSelections(m_baseSelections + m_markerSelections);
}

/** GetMarkerTextBlocks for every line on screen that shows. */
QList<QTextEdit::ExtraSelection> DiffTextEdit::markerSelections()
{
	QList<QTextEdit::ExtraSelection> selections;
	QTextBlock block = firstVisibleBlock();
	m_markerFirst = block.isValid() ? block.blockNumber() : -1;
	const lm::TextMarkers *markers = lm::TextMarkers::instance();
	if (!markers->enabled())
		return selections;
	const qreal bottom = viewport()->height();
	qreal top = block.isValid()
		? blockBoundingGeometry(block).translated(contentOffset()).top() : 0;
	for (; block.isValid() && top <= bottom; block = block.next())
	{
		if (!block.isVisible())
			continue;
		top += blockBoundingRect(block).height();
		for (const lm::TextMarkers::Stretch &stretch : markers->stretches(block.text()))
		{
			QTextEdit::ExtraSelection selection;
			selection.format.setBackground(lm::markerColor(stretch.color));
			selection.cursor = QTextCursor(block);
			selection.cursor.setPosition(block.position() + stretch.start);
			selection.cursor.setPosition(block.position() + stretch.start + stretch.length,
				QTextCursor::KeepAnchor);
			selections.append(selection);
		}
	}
	return selections;
}

void DiffTextEdit::refreshMarkers(bool force)
{
	if (force)
		m_markerRefreshPending = false;
	const QTextBlock first = firstVisibleBlock();
	if (!force && first.isValid() && first.blockNumber() == m_markerFirst)
		return;
	const QList<QTextEdit::ExtraSelection> selections = markerSelections();
	if (selections.isEmpty() && m_markerSelections.isEmpty())
		return;
	m_markerSelections = selections;
	QPlainTextEdit::setExtraSelections(m_baseSelections + m_markerSelections);
}

/** After the event at hand: a Replace All edits the text many times over,
    the marker follows once. */
void DiffTextEdit::scheduleMarkerRefresh()
{
	if (m_markerRefreshPending)
		return;
	m_markerRefreshPending = true;
	QTimer::singleShot(0, this, [this]() {
		if (m_markerRefreshPending)
			refreshMarkers(true);
	});
}

QStringList DiffTextEdit::markedTextsForTest() const
{
	QStringList texts;
	for (const QTextEdit::ExtraSelection &selection : m_markerSelections)
		texts.append(selection.cursor.selectedText());
	return texts;
}

QList<QColor> DiffTextEdit::markedColorsForTest() const
{
	QList<QColor> colors;
	for (const QTextEdit::ExtraSelection &selection : m_markerSelections)
		colors.append(selection.format.background().color());
	return colors;
}

int DiffTextEdit::firstVisibleLine() const
{
	return firstVisibleBlock().blockNumber();
}

int DiffTextEdit::visibleLineCount() const
{
	int lineHeight = qRound(blockBoundingRect(firstVisibleBlock()).height());
	// a document not laid out yet (a comparison just opened) has no
	// line height to measure: the font's
	if (lineHeight <= 0)
		lineHeight = fontMetrics().lineSpacing();
	return viewport()->height() / qMax(1, lineHeight);
}

bool DiffTextEdit::linesOnScreen(int first, int last) const
{
	// the first and the last of them that show (hidden lines take no room)
	QTextBlock top = document()->findBlockByNumber(first);
	while (top.isValid() && top.blockNumber() <= last && !top.isVisible())
		top = top.next();
	QTextBlock bottom = document()->findBlockByNumber(last);
	while (bottom.isValid() && bottom.blockNumber() >= first && !bottom.isVisible())
		bottom = bottom.previous();
	if (!top.isValid() || !bottom.isValid() || top.blockNumber() > bottom.blockNumber())
		return false;
	const qreal begin = blockBoundingGeometry(top).translated(contentOffset()).top();
	const qreal end = blockBoundingGeometry(bottom).translated(contentOffset()).top()
		+ blockBoundingRect(bottom).height();
	const qreal screen = viewport()->height();
	return begin >= 0 && end <= screen && end - begin < screen;
}

void DiffTextEdit::setGutterLineColors(const QHash<int, QColor> &colors)
{
	m_lineColors = colors;
	m_gutter->update();
}

void DiffTextEdit::setLineNumbers(const QList<int> &numbers)
{
	m_lineNumbers = numbers;
	m_gutter->update();
}

void DiffTextEdit::setInsertionMarkers(const QList<InsertionMarker> &markers)
{
	m_markers = markers;
	viewport()->update();
}

/** Extra selections cannot paint a zero-length range, so the insertion
    markers are drawn by hand on top of the text. */
/** CCrystalTextView's viewable whitespace: U+BB for a tab, U+B7 for a
    space, each where the character is, and a line's ending past its text;
    in COLORINDEX_INTERMEDIATECOLOR, a third of the way from the background
    to the text, which the text's color a third opaque comes to over any
    background. */
void DiffTextEdit::paintWhitespace(QPaintEvent *event)
{
	QColor color = palette().color(QPalette::Text);
	color.setAlphaF(0.333f);
	QPainter painter(viewport());
	painter.setFont(font());
	painter.setPen(color);
	const QRect area = event->rect();
	for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next())
	{
		const QRectF geometry = blockBoundingGeometry(block).translated(contentOffset());
		if (geometry.top() > area.bottom())
			break;
		QTextLayout *layout = block.layout();
		if (!block.isVisible() || geometry.bottom() < area.top() || layout == nullptr
			|| layout->lineCount() == 0)
			continue;
		const QString text = block.text();
		if (m_viewWhitespace)
			for (int l = 0; l < layout->lineCount(); ++l)
			{
				const QTextLine line = layout->lineAt(l);
				const qreal baseline = geometry.y() + line.y() + line.ascent();
				// (only what is in the area: a long line costs nothing more)
				const int end = line.textStart() + line.textLength();
				const int from = qMax(line.textStart(), line.xToCursor(area.left() - geometry.x()));
				const int to = qMin(end, line.xToCursor(area.right() - geometry.x()) + 1);
				for (int pos = from; pos < to; ++pos)
				{
					const QChar c = text.at(pos);
					if (c == QLatin1Char(' ') || c == QLatin1Char('\t'))
						painter.drawText(QPointF(geometry.x() + line.cursorToX(pos), baseline),
							c == QLatin1Char(' ') ? QString(QChar(0xB7)) : QString(QChar(0xBB)));
				}
			}
		if (m_eolGlyphOf)
		{
			const QString glyph = m_eolGlyphOf(block);
			if (!glyph.isEmpty())
			{
				const QTextLine line = layout->lineAt(layout->lineCount() - 1);
				painter.drawText(QPointF(geometry.x() + line.cursorToX(block.length() - 1),
					geometry.y() + line.y() + line.ascent()), glyph);
			}
		}
	}
}

void DiffTextEdit::paintEvent(QPaintEvent *event)
{
	QPlainTextEdit::paintEvent(event);
	if (m_viewWhitespace || m_eolGlyphOf)
		paintWhitespace(event);
	if (m_hasHiddenLines)
	{
		// CCrystalTextView draws a boundary under each shown line that
		// hidden lines follow, in the colour of the text
		QPainter painter(viewport());
		painter.setPen(palette().color(QPalette::Text));
		const int right = viewport()->width();
		QTextBlock block = firstVisibleBlock();
		int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
		while (block.isValid() && top <= event->rect().bottom())
		{
			const int bottom = top + qRound(blockBoundingRect(block).height());
			if (block.isVisible() && endsShownStretch(block)
				&& bottom > event->rect().top())
				painter.drawLine(0, bottom - 1, right, bottom - 1);
			block = block.next();
			top = bottom;
		}
	}
	if (m_markers.isEmpty())
		return;
	QPainter painter(viewport());
	for (const InsertionMarker &marker : m_markers)
	{
		const QTextBlock block =
			document()->findBlockByNumber(marker.viewLine);
		if (!block.isValid() || !block.isVisible())
			continue;
		QTextLayout *layout = block.layout();
		if (layout == nullptr || layout->lineCount() == 0)
			continue;
		const int column = qBound(0, marker.column, block.length() - 1);
		const QTextLine line = layout->lineForTextPosition(column);
		if (!line.isValid())
			continue;
		const QRectF geometry =
			blockBoundingGeometry(block).translated(contentOffset());
		const qreal x = geometry.x() + line.cursorToX(column);
		painter.fillRect(QRectF(x - 1.0, geometry.y() + line.y(),
			2.0, line.height()), marker.color);
	}
}

int DiffTextEdit::gutterWidth() const
{
	int digits = 1;
	for (int max = qMax(1, blockCount()); max >= 10; max /= 10)
		++digits;
	return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void DiffTextEdit::updateGutterWidth()
{
	setViewportMargins(gutterWidth(), 0, 0, 0);
}

void DiffTextEdit::updateGutter(const QRect &rect, int dy)
{
	if (dy != 0)
		m_gutter->scroll(0, dy);
	else
		m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
	if (rect.contains(viewport()->rect()))
		updateGutterWidth();
}

void DiffTextEdit::resizeEvent(QResizeEvent *event)
{
	scheduleMarkerRefresh();
	QPlainTextEdit::resizeEvent(event);
	const QRect cr = contentsRect();
	m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
	emit resized();
}

void DiffTextEdit::paintGutter(QPaintEvent *event)
{
	QPainter painter(m_gutter);
	painter.fillRect(event->rect(), palette().color(QPalette::Window));

	QTextBlock block = firstVisibleBlock();
	int blockNumber = block.blockNumber();
	int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
	int bottom = top + qRound(blockBoundingRect(block).height());

	const int width = m_gutter->width();
	painter.setFont(font());
	while (block.isValid() && top <= event->rect().bottom())
	{
		if (block.isVisible() && bottom >= event->rect().top())
		{
			const auto colorIt = m_lineColors.constFind(blockNumber);
			if (colorIt != m_lineColors.constEnd())
				painter.fillRect(0, top, width, bottom - top, colorIt.value());
			int number = blockNumber + 1;
			if (!m_lineNumbers.isEmpty())
				number = blockNumber < m_lineNumbers.size()
					? m_lineNumbers.at(blockNumber) : -1;
			if (number > 0)
			{
				painter.setPen(palette().color(QPalette::PlaceholderText));
				painter.drawText(0, top, width - 6, bottom - top,
					Qt::AlignRight | Qt::AlignVCenter, QString::number(number));
			}
			// the boundary over hidden lines starts in the margin
			if (m_hasHiddenLines && endsShownStretch(block))
			{
				painter.setPen(palette().color(QPalette::Text));
				painter.drawLine(0, bottom - 1, width, bottom - 1);
			}
		}
		block = block.next();
		top = bottom;
		bottom = top + qRound(blockBoundingRect(block).height());
		++blockNumber;
	}
}
