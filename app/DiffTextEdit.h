// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QColor>
#include <QHash>
#include <QList>
#include <QPlainTextEdit>
#include <QTextBlock>

/**
 * Marks a visual filler ("ghost") line inserted to keep the compare panes
 * aligned line-by-line, like WinMerge's empty lines. A flagged block
 * counts as a ghost only while it stays empty: typing on it turns it back
 * into real content until the next recompare rebuilds the alignment.
 */
class GhostBlockData : public QTextBlockUserData
{
};

inline bool isGhostBlock(const QTextBlock &block)
{
	return block.userData() != nullptr && block.length() <= 1;
}

/**
 * QPlainTextEdit with a line-number gutter. The gutter background can be
 * tinted per line to mirror the diff highlighting, and the numbering can
 * be remapped so ghost lines show no number.
 *
 * View lines can be hidden, WinMerge's LF_INVISIBLE lines of a view with
 * "hide lines" on (the display filter of the file window). As in its
 * editor, a hidden line is left out of what is copied and stays when a
 * selection that spans it is typed over, cut or deleted. The cursor does
 * not rest in one: only a selection's end does, in the hidden lines the
 * text ends with.
 */
class DiffTextEdit : public QPlainTextEdit
{
	Q_OBJECT
public:
	explicit DiffTextEdit(QWidget *parent = nullptr);

	void setGutterLineColors(const QHash<int, QColor> &colors);

	/** numbers[viewLine] = 1-based real line number, or -1 for ghost
	    lines (no number drawn). Empty list falls back to 1:1 numbering. */
	void setLineNumbers(const QList<int> &numbers);

	/** A thin vertical mark at a text position: the insertion point of
	    text that exists only on the other side, WinMerge's zero-width
	    "deleted" block (issue #6). column is UTF-16, within the line. */
	struct InsertionMarker
	{
		int viewLine = 0;
		int column = 0;
		QColor color;
	};
	void setInsertionMarkers(const QList<InsertionMarker> &markers);

	/** Called with the view line on double-click, before the default
	    word selection (the compare view selects the difference there,
	    like WinMerge's OnLButtonDblClk). */
	void setDoubleClickHook(std::function<void(int viewLine)> hook)
	{
		m_doubleClickHook = std::move(hook);
	}

	/** Called when a local file is dropped or pasted onto the pane; the
	    compare view loads it into this side instead of inserting the
	    URL as text, like WinMerge. */
	void setFileDropHook(std::function<void(const QString &path)> hook)
	{
		m_fileDropHook = std::move(hook);
	}

	/** Called with the pane's context menu before it is shown, for the
	    compare view to put its own items in. */
	void setContextMenuHook(std::function<void(QMenu *menu)> hook)
	{
		m_contextMenuHook = std::move(hook);
	}

	/** What is handed the pane's context menu in place of opening it (for
	    tests: a menu on screen lasts only while the application is the
	    one in front). */
	static void setContextMenuPresenterForTest(std::function<void(QMenu *menu)> presenter);

	/** Hide the view lines whose entry is true and show the others; an
	    empty list shows every line. */
	void setHiddenLines(const QList<bool> &hidden);
	bool hasHiddenLines() const { return m_hasHiddenLines; }
	bool isLineHidden(int viewLine) const;
	/** Where a shown line ends in the viewport, -1 for a hidden one (for
	    tests of what is drawn under it). */
	int lineBottomForTest(int viewLine) const;
	/** Remove the text between two positions the way upstream's editor
	    does around hidden lines (CCrystalTextBuffer::DeleteText): every
	    stretch of visible lines goes on its own and the hidden lines
	    between them stay. One undo step. */
	void removeKeepingHidden(int from, int to);

	int gutterWidth() const;
	void paintGutter(QPaintEvent *event);

	int firstVisibleLine() const;
	int visibleLineCount() const;

protected:
	bool event(QEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void inputMethodEvent(QInputMethodEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	QMimeData *createMimeDataFromSelection() const override;
	bool canInsertFromMimeData(const QMimeData *source) const override;
	void insertFromMimeData(const QMimeData *source) override;

private slots:
	void updateGutterWidth();
	void updateGutter(const QRect &rect, int dy);
	void keepCursorOnShownLine();

private:
	bool spansHiddenLine(int from, int to) const;
	bool endsShownStretch(const QTextBlock &block) const;
	bool selectionSpansHiddenLine() const;
	void removeSelectionKeepingHidden();

	QWidget *m_gutter;
	bool m_hasHiddenLines = false;
	bool m_movingCursor = false;
	int m_lastCursorPosition = 0;
	std::function<void(QMenu *)> m_contextMenuHook;
	QHash<int, QColor> m_lineColors;
	QList<int> m_lineNumbers;
	QList<InsertionMarker> m_markers;
	std::function<void(int)> m_doubleClickHook;
	std::function<void(const QString &)> m_fileDropHook;
};
