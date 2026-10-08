// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

#include "FindText.h"

class PaneSearch;
class QCheckBox;
class QComboBox;
class QPushButton;
class QRadioButton;

/**
 * WinMerge's Find dialog (CFindTextDlg, IDD_EDIT_FIND): modeless, it
 * searches its pane while the panes stay in reach. Find Next and Find
 * Prev need a text; a regular expression leaves "Match whole word only"
 * out. What is not found is told in a message; what is, is selected, and
 * the dialog closes unless "Don't close this dialog" is ticked (as it is
 * at first). The text goes to the field's history, kept in the settings
 * once a search finds it.
 */
class FindTextDialog : public QDialog
{
	Q_OBJECT
public:
	FindTextDialog(PaneSearch *buddy, QWidget *parent);

	/** The settings the pane gives before showing it, and UseLastSearch,
	    which takes them in. */
	lm::LastSearchInfos *lastSearchInfos() { return &m_lastSearch; }
	void useLastSearch();
	/** UpdateData: the fields from the dialog's values (false: the
	    keyboard goes to the text), or the values from the fields. */
	void updateData(bool saveAndValidate);

	QString m_text; ///< m_sText, set by the pane before showing

	QComboBox *findTextForTest() const { return m_findText; }
	QCheckBox *wholeWordForTest() const { return m_wholeWordBox; }
	QCheckBox *matchCaseForTest() const { return m_matchCaseBox; }
	QCheckBox *regExpForTest() const { return m_regExpBox; }
	QCheckBox *noWrapForTest() const { return m_noWrapBox; }
	QCheckBox *noCloseForTest() const { return m_noCloseBox; }
	QPushButton *findNextForTest() const { return m_findNext; }
	QPushButton *findPrevForTest() const { return m_findPrev; }

	/** OnCancel: the values are kept, the keyboard goes back to the pane. */
	void reject() override;

private:
	void findText(int direction);
	void changeEditText();
	void changeSelected(int index);
	void regExpClicked();
	void updateControls();
	void updateRegExp();
	void updateLastSearch();

	PaneSearch *m_buddy;
	int m_direction = 1;
	bool m_matchCase = false;
	bool m_wholeWord = false;
	bool m_regExp = false;
	bool m_noWrap = false;
	bool m_noClose = true;
	lm::LastSearchInfos m_lastSearch;
	QComboBox *m_findText;
	QCheckBox *m_wholeWordBox;
	QCheckBox *m_matchCaseBox;
	QCheckBox *m_regExpBox;
	QCheckBox *m_noWrapBox;
	QCheckBox *m_noCloseBox;
	QPushButton *m_findNext;
	QPushButton *m_findPrev;
};

/**
 * WinMerge's Replace dialog (CEditReplaceDlg, IDD_EDIT_REPLACE), made
 * anew at each Edit > Replace... The first Find Next or Replace selects
 * what is found, and Replace becomes the button Enter presses; the next
 * Replace puts the new text in and selects the next one. Replace All
 * goes through the file, or through the selection it started with, from
 * there on and round to there, in one undo step, and tells how many it
 * replaced. A selection over more than one line is where to replace at
 * first; there, only Replace All works. Cancel brings back that selection
 * when something was replaced.
 */
class EditReplaceDialog : public QDialog
{
	Q_OBJECT
public:
	EditReplaceDialog(PaneSearch *buddy, QWidget *parent);

	lm::LastSearchInfos *lastSearchInfos() { return &m_lastSearch; }
	void useLastSearch();
	/** SetScope: the selection (true) or the whole file. */
	void setScope(bool withSelection);
	/** OnInitDialog, once the pane has set the values below. */
	void initDialog();
	void updateData(bool saveAndValidate);

	QString m_text;                       ///< m_sText
	lm::TextPoint m_currentPos;           ///< where Replace All starts
	bool m_enableScopeSelection = true;
	lm::TextPoint m_blockBegin;           ///< the selection to replace in
	lm::TextPoint m_blockEnd;

	QComboBox *findTextForTest() const { return m_findText; }
	QComboBox *replaceTextForTest() const { return m_replaceText; }
	QCheckBox *wholeWordForTest() const { return m_wholeWordBox; }
	QCheckBox *matchCaseForTest() const { return m_matchCaseBox; }
	QCheckBox *regExpForTest() const { return m_regExpBox; }
	QCheckBox *dontWrapForTest() const { return m_dontWrapBox; }
	QRadioButton *selectionScopeForTest() const { return m_scopeSelection; }
	QRadioButton *wholeFileScopeForTest() const { return m_scopeWholeFile; }
	QPushButton *findNextForTest() const { return m_skip; }
	QPushButton *findPrevForTest() const { return m_findPrev; }
	QPushButton *replaceForTest() const { return m_replace; }
	QPushButton *replaceAllForTest() const { return m_replaceAll; }

	/** OnCancel: the selection the dialog started with comes back when
	    something was replaced. */
	void reject() override;

private:
	unsigned searchFlags() const;
	bool doHighlightText(bool notifyIfNotFound, bool updateView = true);
	bool adjustSearchPos(lm::TextPoint &foundAt) const;
	void findNextPrev(bool next);
	void editReplace();
	void editReplaceAll();
	void changeEditText();
	void changeSelected(int index);
	void regExpClicked();
	void updateControls();
	void updateRegExp();
	void updateLastSearch();
	void setFoundButtons(bool found);

	PaneSearch *m_buddy;
	bool m_matchCase = false;
	bool m_wholeWord = false;
	bool m_regExp = false;
	int m_scope = -1; ///< 0 the selection, 1 the whole file
	bool m_dontWrap = false;
	int m_direction = 1;
	bool m_found = false;
	bool m_replaced = false;
	QString m_newText;
	lm::TextPoint m_foundAt;
	lm::LastSearchInfos m_lastSearch;
	QComboBox *m_findText;
	QComboBox *m_replaceText;
	QCheckBox *m_wholeWordBox;
	QCheckBox *m_matchCaseBox;
	QCheckBox *m_regExpBox;
	QCheckBox *m_dontWrapBox;
	QRadioButton *m_scopeSelection;
	QRadioButton *m_scopeWholeFile;
	QPushButton *m_skip;
	QPushButton *m_findPrev;
	QPushButton *m_replace;
	QPushButton *m_replaceAll;
};
