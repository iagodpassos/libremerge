// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QObject>
#include <QPointer>
#include <QRegularExpressionMatch>
#include <QTextCursor>
#include <utility>

#include "FindDialogs.h"
#include "FindText.h"

class DiffTextEdit;
class QWidget;

/**
 * The search of one pane of the file window: what upstream's editor view
 * does for it. CCrystalTextView's FindText, FindTextInBlock and
 * HighlightText, Edit > Find... (OnEditFind) and F3 (OnEditRepeat);
 * CCrystalEditView's Edit > Replace... (OnEditReplace) and
 * ReplaceSelection. Each pane has its own Find dialog, kept from one use
 * to the next, its own Replace dialog, made anew each time, and its own
 * last search, as upstream's views do.
 *
 * Lines here are the view's lines, filler ones included; the hidden lines
 * of the display filter are passed over, as upstream passes over its
 * LF_INVISIBLE lines.
 */
class PaneSearch : public QObject
{
	Q_OBJECT
public:
	/** What the pane's file window tells. */
	struct Context
	{
		std::function<QString()> eol;               ///< the file's line break
		std::function<bool()> editable;             ///< QueryEditable
		std::function<int(int viewLine)> shownLine; ///< its line on screen
	};

	PaneSearch(DiffTextEdit *pane, Context context, QWidget *dialogParent);
	~PaneSearch() override;

	DiffTextEdit *pane() const { return m_pane; }

	// --- the text ---
	int lineCount() const;
	int lineLength(int line) const;
	lm::TextPoint cursorPos() const;
	bool isSelection() const;
	/** The selection's start and end, in that order. */
	std::pair<lm::TextPoint, lm::TextPoint> selection() const;
	void setSelection(lm::TextPoint start, lm::TextPoint end);
	/** GetSearchPos: where a search starts, the selection's end (its start
	    going up), or the cursor. */
	lm::TextPoint searchPos(unsigned flags) const;
	/** GetText: the text between two places, the file's line breaks in it. */
	QString text(lm::TextPoint start, lm::TextPoint end) const;
	/** WordToLeft and WordToRight: the edges of the word at a place, by
	    the word breaks of Unicode, as upstream's ICU ones. */
	lm::TextPoint wordToLeft(lm::TextPoint point) const;
	lm::TextPoint wordToRight(lm::TextPoint point) const;

	// --- the search ---
	/** FindText over the whole text; it sets the search marker first. */
	bool findText(const QString &what, lm::TextPoint start, unsigned flags,
		bool wrap, lm::TextPoint *found);
	bool findTextInBlock(const QString &what, lm::TextPoint start,
		lm::TextPoint blockBegin, lm::TextPoint blockEnd, unsigned flags,
		bool wrap, lm::TextPoint *found);
	/** FindText(LastSearchInfos *): the Find dialog's search, from the
	    selection or the cursor; what it finds is selected and kept for F3,
	    and its flags in the settings. */
	bool findText(const lm::LastSearchInfos &lastSearch);
	/** HighlightText: select what was found, scrolled to the middle of
	    the pane when it is out of it. */
	void highlightText(lm::TextPoint start, int length, bool cursorToLeft = false,
		bool updateView = true);
	void ensureSelectionVisible();
	int lastFindWhatLen() const { return m_lastFindWhatLen; }
	/** Give the keyboard back to the pane, as upstream's dialogs do. */
	void focusPane();

	// --- the commands ---
	/** Edit > Find... (OnEditFind) */
	void editFind();
	/** F3 (OnEditRepeat): the last search again, the other way with
	    Shift; with Ctrl, the selection or the word at the cursor. With
	    nothing to search for, the Find dialog. */
	void editRepeat(bool control, bool shift);
	/** Edit > Replace... (OnEditReplace), for an editable pane only. */
	void editReplace();
	/** Edit > Marker... (OnEditMark): the Marker dialog, the selection
	    within a line or the word at the cursor in it as a new marker. OK
	    keeps the markers and the flags in the settings. */
	void editMark();

	// --- replacing ---
	/** SaveLastSearch: the Replace dialog's settings, for its next use and
	    for F3. */
	void saveLastSearch(const lm::LastSearchInfos &lastSearch);
	/** ReplaceSelection: the selection becomes the text (a regular
	    expression's replacement for its match), which ends up selected. */
	bool replaceSelection(const QString &newText, unsigned flags);
	int lastReplaceLen() const { return m_lastReplaceLen; }
	bool selectionPushed() const { return m_selectionPushed; }
	void setSelectionPushed(bool pushed) { m_selectionPushed = pushed; }
	/** The selection the Replace dialog started with, kept in step with
	    the edits (m_ptSavedSelStart and m_ptSavedSelEnd). */
	lm::TextPoint savedSelStart() const { return pointOf(m_savedSelStart); }
	lm::TextPoint savedSelEnd() const { return pointOf(m_savedSelEnd); }
	/** A place kept in step with the edits as upstream's RecalcPoint keeps
	    one: text put in right at it goes after it. */
	QTextCursor trackPoint(lm::TextPoint point) const;
	lm::TextPoint pointOf(const QTextCursor &tracker) const;

	FindTextDialog *findDialog() const { return m_findDialog; }
	EditReplaceDialog *replaceDialog() const { return m_replaceDialog; }

	/** IDS_EDIT_TEXT_NOT_FOUND */
	static void tellNotFound(QWidget *parent, const QString &text);

private:
	int positionOf(lm::TextPoint point) const;
	lm::TextPoint pointAt(int position) const;
	QString lineText(int line) const;
	bool lineHidden(int line) const;
	QString eol() const;
	lm::TextPoint pointInJoined(const QString &joined, int offset, int firstLine) const;
	void deleteCurrentSelection();
	void removeRange(int from, int to);
	QString wordAtCursor() const;

	DiffTextEdit *m_pane;
	Context m_context;
	QWidget *m_dialogParent;
	// F3's search (m_bLastSearch, m_pszLastFindWhat, m_dwLastSearchFlags)
	bool m_lastSearch = false;
	QString m_lastFindWhat;
	unsigned m_lastSearchFlags = 0;
	// the Replace dialog's (m_bLastReplace, m_dwLastReplaceFlags)
	bool m_lastReplace = false;
	unsigned m_lastReplaceFlags = 0;
	// the last match and the last replacement
	int m_lastFindWhatLen = 0;
	int m_lastReplaceLen = 0;
	QRegularExpressionMatch m_match;
	bool m_hasMatch = false;
	// the selection the Replace dialog started with
	bool m_selectionPushed = false;
	QTextCursor m_savedSelStart;
	QTextCursor m_savedSelEnd;
	QPointer<FindTextDialog> m_findDialog;
	QPointer<EditReplaceDialog> m_replaceDialog;
};
