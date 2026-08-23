// SPDX-License-Identifier: GPL-3.0-or-later
#include "DiffTextEdit.h"

#include <QKeyEvent>
#include <QPainter>
#include <QTextBlock>

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

} // namespace

DiffTextEdit::DiffTextEdit(QWidget *parent)
	: QPlainTextEdit(parent)
{
	m_gutter = new GutterWidget(this);
	connect(this, &QPlainTextEdit::blockCountChanged,
		this, &DiffTextEdit::updateGutterWidth);
	connect(this, &QPlainTextEdit::updateRequest,
		this, &DiffTextEdit::updateGutter);
	updateGutterWidth();
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

int DiffTextEdit::firstVisibleLine() const
{
	return firstVisibleBlock().blockNumber();
}

int DiffTextEdit::visibleLineCount() const
{
	const int lineHeight = qMax(1, qRound(blockBoundingRect(firstVisibleBlock()).height()));
	return viewport()->height() / lineHeight;
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
	QPlainTextEdit::resizeEvent(event);
	const QRect cr = contentsRect();
	m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
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
		}
		block = block.next();
		top = bottom;
		bottom = top + qRound(blockBoundingRect(block).height());
		++blockNumber;
	}
}
