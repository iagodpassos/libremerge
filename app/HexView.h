// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QAbstractScrollArea>
#include <QByteArray>
#include <QColor>
#include <QList>
#include <QObject>

class QMenu;
class QTimer;
class HexView;

/**
 * Frhed's settings that WinMerge's Options > Compare > Binary opens (the
 * editor's View Settings, Binary Mode and Character Set dialogs), shared
 * by every binary comparison.
 */
struct HexSettings
{
	bool automaticBytesPerLine = true;
	int bytesPerLine = 16;
	bool automaticOffsetLength = true;
	int offsetLength = 6;
	bool unsignedValues = true;
	bool bigEndian = false;
	bool oemCharacterSet = false;
	/** The font's size in points; 0 for the system's fixed font size. */
	qreal fontSize = 0;

	static HexSettings load();
	/** Keep them, and have every binary comparison take them. */
	void save() const;
	/** The size the font has before any zoom. */
	qreal basePointSize() const;
};

/** Tells the binary comparisons that the settings changed. */
class HexSettingsWatch : public QObject
{
	Q_OBJECT
public:
	static HexSettingsWatch *instance();

signals:
	void changed();
};

/**
 * The edits of every pane of a binary comparison in one history, as the
 * hex editor WinMerge embeds shares its undo records between the panes:
 * Undo takes back the last edit, whichever pane it was made in, and
 * focuses that pane.
 */
class HexUndoHistory : public QObject
{
	Q_OBJECT
public:
	explicit HexUndoHistory(QObject *parent = nullptr) : QObject(parent) {}

	/** At offset, the bytes "before" became "after" in the view. */
	void push(HexView *view, qsizetype offset, const QByteArray &before, const QByteArray &after);
	bool canUndo() const { return m_next > 0; }
	bool canRedo() const { return m_next < m_records.size(); }
	void undo();
	void redo();
	/** Forget the records of one view (its file was loaded anew), or all. */
	void clear(HexView *view = nullptr);

signals:
	void changed();

private:
	struct Record
	{
		HexView *view;
		qsizetype offset;
		QByteArray before;
		QByteArray after;
		int id;
		int previousState; // the view's state before this record
	};
	QList<Record> m_records;
	qsizetype m_next = 0;
	int m_lastId = 0;
};

/**
 * One pane of the binary comparison: the hex editor WinMerge embeds
 * (heksedit, from Frhed), written anew for LibreMerge after its behavior;
 * Frhed's code is GPL-2.0-only and none of it is taken.
 *
 * A line shows the offset of its first byte, the bytes in hex with a
 * separator every four, and the bytes as characters; the end of the data
 * is marked "__". The bytes per line follow the width. A byte that differs
 * from the sibling panes' byte at the same offset (or that they lack) is
 * colored as a difference: there is no alignment, byte n is compared with
 * byte n. The caret stands on a nibble of the hex column or on a character
 * of the text column (Tab switches), and moves and selects as Frhed's.
 */
class HexView : public QAbstractScrollArea
{
	Q_OBJECT
public:
	enum class Area
	{
		Bytes,
		Chars,
	};

	explicit HexView(QWidget *parent = nullptr);
	~HexView() override;

	const QByteArray &data() const { return m_data; }
	qsizetype length() const { return m_data.size(); }
	/** New content (a file loaded): the caret at the start, nothing
	    selected, as loaded (not modified). */
	void setData(const QByteArray &data);

	/** The panes this one is compared with: one in a 2-way comparison
	    (and the middle pane's two in a 3-way). */
	void setSiblings(HexView *first, HexView *second = nullptr);
	void setUndoHistory(HexUndoHistory *history) { m_history = history; }

	bool isReadOnly() const { return m_readOnly; }
	void setReadOnly(bool readOnly);
	bool isModified() const { return m_state != m_savedState; }
	/** Upstream's set_savepoint: what is shown now is what the file has. */
	void setSavePoint();
	bool insertMode() const { return m_insertMode; }
	void toggleInsertMode();

	/** Whether the byte at the offset differs from the siblings'. */
	bool isDiff(qsizetype offset) const;

	qsizetype caretByte() const { return m_curByte; }
	int caretNibble() const { return m_curNibble; }
	Area area() const { return m_area; }
	bool hasSelection() const { return m_selected; }
	qsizetype selectionLower() const { return qMin(m_selStart, m_selEnd); }
	qsizetype selectionUpper() const { return qMax(m_selStart, m_selEnd); }
	/** Select from start to end (inclusive, either order) and show it. */
	void setSelection(qsizetype start, qsizetype end);
	void selectAll();

	/** Upstream's select_next_diff/select_prev_diff: select the next run
	    of differing bytes after the selection (from the start of the
	    data with fromStart), or the previous one before it, within the
	    length the panes have in common, and select it in the siblings
	    too. False when there is none. */
	bool selectNextDiff(bool fromStart);
	bool selectPrevDiff(bool fromEnd);

	/** Upstream's copy_sel_from: the bytes the other pane has selected,
	    at the same offsets here, growing the data to hold them. */
	void copySelectionFrom(const HexView *source);
	/** Upstream's copy_all_from: all of the other pane's bytes over these
	    (bytes past the end of the source stay). */
	void copyAllFrom(const HexView *source);

	/** Replace removed bytes at offset with inserted ones, as one undo
	    step (recorded unless record is false: undo and redo themselves). */
	void replaceBytes(qsizetype offset, qsizetype removed, const QByteArray &inserted,
		bool record = true);
	/** After an undo or a redo here: the caret at the offset, nothing
	    selected, and the focus. */
	void showUndone(qsizetype offset);
	/** Select bytes found, the caret at their start, and show them; the
	    siblings take the caret (upstream's find). */
	void selectFound(qsizetype start, qsizetype length);
	/** The caret on a byte, nothing selected. */
	void placeCaret(qsizetype offset);

	// the editor's commands, with Frhed's dialogs
	void editCopy();
	void editCut();
	void editPaste();
	void editDelete();
	void showFind();
	void findNext();
	void findPrevious();
	void showReplace();
	void showGoTo();
	QString findTextForTest() const { return m_findText; }
	/** What is handed the context menu in place of opening it (tests). */
	static void setContextMenuPresenterForTest(std::function<void(QMenu *menu)> presenter);

	/** Take the settings anew (the Options changed them). */
	void applySettings(const HexSettings &settings);
	const HexSettings &settings() const { return m_settings; }

	int bytesPerLine() const { return m_bytesPerLine; }
	int offsetLength() const { return m_offsetLength; }
	int topLine() const;
	/** Text size, shared with the text comparison like WinMerge's zoom:
	    one point more or less, or the default with 0. */
	void zoom(int amount);
	void setFontPointSize(qreal pointSize);

	/** The status bar's three parts, as Frhed's: the caret's offset and
	    the values there (or the selection), the character set and the
	    editing mode, and the size. */
	QString statusPositionText() const;
	QString statusBitsText() const;
	QString statusValuesText() const;
	QString statusModeText() const;
	QString statusSizeText() const;

	/** The text of one shown line, as painted (for tests). */
	QString lineTextForTest(int line) const;
	/** The middle of a byte's nibble in the hex column, or of its
	    character, in the viewport (for tests). */
	QPoint pointForTest(qsizetype byte, int nibble, bool chars) const;

signals:
	/** The caret, the selection or the data changed: the status bar and
	    the header follow. */
	void statusChanged();
	/** The data changed by an edit (or an undo). */
	void dataEdited();
	/** The view scrolled: the siblings follow. */
	void scrolled();
	void focused();

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void focusInEvent(QFocusEvent *event) override;
	void focusOutEvent(QFocusEvent *event) override;
	void scrollContentsBy(int dx, int dy) override;
	bool focusNextPrevChild(bool next) override;
	bool event(QEvent *event) override;

private:
	friend class HexUndoHistory;

	struct Hit
	{
		enum Where { Offsets, Bytes, Chars, After } where = After;
		qsizetype pos = 0;
		int nibble = 0; // 2: between two bytes
	};

	void relayout();
	void updateScrollBars();
	int charsPerLine() const;
	int lineCount() const;
	int screenLines() const;
	int screenColumns() const;
	qsizetype siblingLength(int i) const;
	Hit hitTest(const QPoint &point) const;
	void moveCaret(int key, bool shift, bool ctrl);
	void typeCharacter(const QString &text);
	void backspace();
	void snapCaret();
	void showSelection();
	void syncSiblings(bool selection);
	void beginDragSelection();
	void autoScroll();
	void changed();
	QColor separatorColor() const;
	bool canCopy() const;
	bool canEdit() const;
	/** The selection, or the caret's byte, in the special syntax: what a
	    Find, Find next or Replace looks for. */
	QString selectionAsSpecialSyntax() const;
	void findFromCaret(const QByteArray &pattern, int direction, bool showDialogMessages);
	void editRange(int mode);

	QByteArray m_data;
	HexSettings m_settings;
	HexView *m_siblings[2] = { this, this };
	HexUndoHistory *m_history = nullptr;
	int m_state = 0;      // the undo record the data stands at
	int m_savedState = 0; // the one the file has

	int m_bytesPerLine = 16;
	int m_offsetLength = 6;
	int m_cx = 8;
	int m_cy = 16;
	int m_ascent = 12;

	qsizetype m_curByte = 0;
	int m_curNibble = 0;
	Area m_area = Area::Bytes;
	bool m_selected = false;
	qsizetype m_selStart = 0;
	qsizetype m_selEnd = 0;
	bool m_insertMode = false;
	bool m_readOnly = false;

	// the mouse
	bool m_selecting = false;
	bool m_pressed = false;
	QPoint m_pressPoint;
	QPoint m_mousePoint;
	qsizetype m_pressPos = 0;
	qsizetype m_lastPos = 0;
	QTimer *m_dragDelay;
	QTimer *m_scrollTimer;

	QTimer *m_blink;
	bool m_caretShown = true;

	// upstream's find context, a pane's own
	QString m_findText;
	int m_findDirection = 1;
	bool m_findMatchCase = false;
	bool m_findUnicode = false;
};
