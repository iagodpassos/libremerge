// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
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

	int gutterWidth() const;
	void paintGutter(QPaintEvent *event);

	int firstVisibleLine() const;
	int visibleLineCount() const;

protected:
	bool event(QEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	QMimeData *createMimeDataFromSelection() const override;
	bool canInsertFromMimeData(const QMimeData *source) const override;
	void insertFromMimeData(const QMimeData *source) override;

private slots:
	void updateGutterWidth();
	void updateGutter(const QRect &rect, int dy);

private:
	QWidget *m_gutter;
	QHash<int, QColor> m_lineColors;
	QList<int> m_lineNumbers;
	std::function<void(int)> m_doubleClickHook;
	std::function<void(const QString &)> m_fileDropHook;
};
