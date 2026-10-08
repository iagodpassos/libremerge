// SPDX-License-Identifier: GPL-3.0-or-later
#include "FindDialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include "DiffTextEdit.h"
#include "MessageBoxes.h"
#include "PaneSearch.h"

namespace
{

const QString kFindGroup = QStringLiteral("FindText");
const QString kReplaceGroup = QStringLiteral("ReplaceText");

/** A search field: an editable list of what was typed in it, in the
    pane's font at the dialog's size, as upstream's OnInitDialog sets it. */
QComboBox *historyCombo(QWidget *parent, const QFont &paneFont)
{
	auto *combo = new QComboBox(parent);
	combo->setEditable(true);
	combo->setInsertPolicy(QComboBox::NoInsert);
	combo->setCompleter(nullptr);
	combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	combo->setMinimumContentsLength(28);
	QFont font = paneFont;
	if (combo->font().pointSizeF() > 0)
		font.setPointSizeF(combo->font().pointSizeF());
	else
		font.setPixelSize(combo->font().pixelSize());
	combo->setFont(font);
	return combo;
}

/** The field's list from its history, its text left as it is. */
void loadHistory(QComboBox *combo, const QString &group)
{
	const QSignalBlocker blocker(combo);
	const QString text = combo->currentText();
	combo->clear();
	combo->addItems(lm::searchHistory(group));
	combo->setCurrentIndex(-1);
	combo->setEditText(text);
}

/** DDX_CBStringExact, as it sets a field. */
void setComboText(QComboBox *combo, const QString &text)
{
	const QSignalBlocker blocker(combo);
	combo->setEditText(text);
}

/** FillCurrent: the field's text at the top of its history. */
void fillCurrent(QComboBox *combo, const QString &group)
{
	const QString text = combo->currentText();
	if (text.isEmpty())
		return;
	lm::fillSearchHistory(group, text);
	loadHistory(combo, group);
}

} // namespace

// --- Find ---

FindTextDialog::FindTextDialog(PaneSearch *buddy, QWidget *parent)
	: QDialog(parent), m_buddy(buddy)
{
	setWindowTitle(tr("Find"));
	auto *outer = new QHBoxLayout(this);
	auto *left = new QVBoxLayout;
	auto *row = new QHBoxLayout;
	auto *label = new QLabel(tr("Fi&nd what:"), this);
	m_findText = historyCombo(this, buddy->pane()->font());
	label->setBuddy(m_findText);
	row->addWidget(label);
	row->addWidget(m_findText, 1);
	left->addLayout(row);
	m_wholeWordBox = new QCheckBox(tr("Match &whole word only"), this);
	m_matchCaseBox = new QCheckBox(tr("Match &case"), this);
	m_regExpBox = new QCheckBox(tr("Regular &expression"), this);
	m_noWrapBox = new QCheckBox(tr("D&on't wrap end of file"), this);
	m_noCloseBox = new QCheckBox(tr("&Don't close this dialog"), this);
	for (QCheckBox *box : { m_wholeWordBox, m_matchCaseBox, m_regExpBox, m_noWrapBox, m_noCloseBox })
		left->addWidget(box);
	left->addStretch(1);

	auto *right = new QVBoxLayout;
	m_findNext = new QPushButton(tr("&Find Next"), this);
	m_findNext->setObjectName(QStringLiteral("findNext"));
	m_findNext->setDefault(true);
	m_findPrev = new QPushButton(tr("Find &Prev"), this);
	m_findPrev->setObjectName(QStringLiteral("findPrev"));
	auto *cancel = new QPushButton(tr("Cancel"), this);
	right->addWidget(m_findNext);
	right->addWidget(m_findPrev);
	right->addWidget(cancel);
	right->addStretch(1);
	outer->addLayout(left, 1);
	outer->addLayout(right);

	// OnOK, OnFindPrev, OnCancel; the field's own changes, typed or picked
	connect(m_findNext, &QPushButton::clicked, this, [this]() { findText(1); });
	connect(m_findPrev, &QPushButton::clicked, this, [this]() { findText(0); });
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_findText->lineEdit(), &QLineEdit::textEdited,
		this, &FindTextDialog::changeEditText);
	connect(m_findText, &QComboBox::activated, this, &FindTextDialog::changeSelected);
	connect(m_regExpBox, &QCheckBox::clicked, this, &FindTextDialog::regExpClicked);
}

void FindTextDialog::updateData(bool saveAndValidate)
{
	if (saveAndValidate)
	{
		m_matchCase = m_matchCaseBox->isChecked();
		m_text = m_findText->currentText();
		m_wholeWord = m_wholeWordBox->isChecked();
		m_regExp = m_regExpBox->isChecked();
		m_noWrap = m_noWrapBox->isChecked();
		m_noClose = m_noCloseBox->isChecked();
		return;
	}
	m_matchCaseBox->setChecked(m_matchCase);
	loadHistory(m_findText, kFindGroup);
	setComboText(m_findText, m_text);
	m_wholeWordBox->setChecked(m_wholeWord);
	m_regExpBox->setChecked(m_regExp);
	m_noWrapBox->setChecked(m_noWrap);
	m_noCloseBox->setChecked(m_noClose);
	m_findText->setFocus(Qt::OtherFocusReason);
	m_findText->lineEdit()->selectAll();
	updateControls();
}

void FindTextDialog::updateRegExp()
{
	if (m_regExp)
	{
		m_wholeWordBox->setEnabled(false);
		m_wholeWord = false;
		m_wholeWordBox->setChecked(false);
	}
	else
		m_wholeWordBox->setEnabled(true);
}

void FindTextDialog::updateControls()
{
	m_findNext->setEnabled(!m_text.isEmpty());
	m_findPrev->setEnabled(!m_text.isEmpty());
	updateRegExp();
}

void FindTextDialog::findText(int direction)
{
	updateData(true);
	fillCurrent(m_findText, kFindGroup);
	m_direction = direction;
	updateLastSearch();
	if (!m_buddy->findText(m_lastSearch))
	{
		PaneSearch::tellNotFound(this, m_text);
		return;
	}
	lm::saveSearchHistory();
	if (!m_noClose)
	{
		QDialog::accept();
		m_buddy->focusPane();
	}
}

void FindTextDialog::changeEditText()
{
	updateData(true);
	updateControls();
}

void FindTextDialog::changeSelected(int index)
{
	if (index < 0)
		return;
	m_text = m_findText->itemText(index);
	setComboText(m_findText, m_text);
	updateControls();
}

void FindTextDialog::regExpClicked()
{
	updateData(true);
	updateRegExp();
	updateData(false);
}

void FindTextDialog::reject()
{
	updateData(true);
	QDialog::reject();
	m_buddy->focusPane();
}

void FindTextDialog::updateLastSearch()
{
	m_lastSearch.matchCase = m_matchCase;
	m_lastSearch.wholeWord = m_wholeWord;
	m_lastSearch.regExp = m_regExp;
	m_lastSearch.direction = m_direction;
	m_lastSearch.text = m_text;
	m_lastSearch.noWrap = m_noWrap;
	m_lastSearch.noClose = m_noClose;
}

void FindTextDialog::useLastSearch()
{
	m_matchCase = m_lastSearch.matchCase;
	m_wholeWord = m_lastSearch.wholeWord;
	m_regExp = m_lastSearch.regExp;
	m_direction = m_lastSearch.direction;
	m_text = m_lastSearch.text;
	m_noWrap = m_lastSearch.noWrap;
	m_noClose = m_lastSearch.noClose;
}

// --- Replace ---

EditReplaceDialog::EditReplaceDialog(PaneSearch *buddy, QWidget *parent)
	: QDialog(parent), m_buddy(buddy)
{
	setWindowTitle(tr("Replace"));
	auto *outer = new QHBoxLayout(this);
	auto *left = new QVBoxLayout;
	auto *fields = new QGridLayout;
	auto *findLabel = new QLabel(tr("Fi&nd what:"), this);
	m_findText = historyCombo(this, buddy->pane()->font());
	findLabel->setBuddy(m_findText);
	auto *replaceLabel = new QLabel(tr("Re&place with:"), this);
	m_replaceText = historyCombo(this, buddy->pane()->font());
	replaceLabel->setBuddy(m_replaceText);
	fields->addWidget(findLabel, 0, 0);
	fields->addWidget(m_findText, 0, 1);
	fields->addWidget(replaceLabel, 1, 0);
	fields->addWidget(m_replaceText, 1, 1);
	fields->setColumnStretch(1, 1);
	left->addLayout(fields);

	m_wholeWordBox = new QCheckBox(tr("Match &whole word only"), this);
	m_matchCaseBox = new QCheckBox(tr("Match &case"), this);
	m_regExpBox = new QCheckBox(tr("Regular &expression"), this);
	m_dontWrapBox = new QCheckBox(tr("&Don't wrap at end of file"), this);
	auto *scope = new QGroupBox(tr("Replace in"), this);
	auto *scopeColumn = new QVBoxLayout(scope);
	m_scopeSelection = new QRadioButton(tr("&Selection"), scope);
	m_scopeWholeFile = new QRadioButton(tr("Wh&ole file"), scope);
	scopeColumn->addWidget(m_scopeSelection);
	scopeColumn->addWidget(m_scopeWholeFile);
	auto *checks = new QVBoxLayout;
	checks->addWidget(m_wholeWordBox);
	checks->addWidget(m_matchCaseBox);
	checks->addWidget(m_regExpBox);
	checks->addStretch(1);
	auto *middle = new QHBoxLayout;
	middle->addLayout(checks, 1);
	middle->addWidget(scope);
	left->addLayout(middle);
	left->addWidget(m_dontWrapBox);
	left->addStretch(1);

	// Enter presses Find Next or Replace, whichever is the default button
	// (upstream's PreTranslateMessage), wherever the keyboard is
	auto *right = new QVBoxLayout;
	const auto button = [this, right](const QString &text, const char *name) {
		auto *pushButton = new QPushButton(text, this);
		pushButton->setObjectName(QLatin1String(name));
		pushButton->setAutoDefault(false);
		right->addWidget(pushButton);
		return pushButton;
	};
	m_skip = button(tr("&Find Next"), "findNext");
	m_findPrev = button(tr("Find Pre&v"), "findPrev");
	m_replace = button(tr("&Replace"), "replace");
	m_replaceAll = button(tr("Replace &All"), "replaceAll");
	QPushButton *cancel = button(tr("Cancel"), "cancel");
	m_skip->setDefault(true);
	right->addStretch(1);
	outer->addLayout(left, 1);
	outer->addLayout(right);

	connect(m_skip, &QPushButton::clicked, this, [this]() { findNextPrev(true); });
	connect(m_findPrev, &QPushButton::clicked, this, [this]() { findNextPrev(false); });
	connect(m_replace, &QPushButton::clicked, this, &EditReplaceDialog::editReplace);
	connect(m_replaceAll, &QPushButton::clicked, this, &EditReplaceDialog::editReplaceAll);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_findText->lineEdit(), &QLineEdit::textEdited,
		this, &EditReplaceDialog::changeEditText);
	connect(m_findText, &QComboBox::activated, this, &EditReplaceDialog::changeSelected);
	connect(m_regExpBox, &QCheckBox::clicked, this, &EditReplaceDialog::regExpClicked);
	// upstream reads the scope at the next button only, which leaves
	// Replace enabled for a click after "Selection": here it follows
	for (QRadioButton *radio : { m_scopeSelection, m_scopeWholeFile })
		connect(radio, &QRadioButton::toggled, this, [this](bool on) {
			if (on)
				updateData(true);
		});
}

void EditReplaceDialog::initDialog()
{
	loadHistory(m_replaceText, kReplaceGroup);
	loadHistory(m_findText, kFindGroup);
	m_newText = m_replaceText->currentText();
	updateData(false);
	m_scopeSelection->setEnabled(m_enableScopeSelection);
	m_found = false;
	m_findText->setFocus(Qt::OtherFocusReason);
	m_findText->lineEdit()->selectAll();
}

void EditReplaceDialog::updateData(bool saveAndValidate)
{
	if (saveAndValidate)
	{
		m_matchCase = m_matchCaseBox->isChecked();
		m_wholeWord = m_wholeWordBox->isChecked();
		m_regExp = m_regExpBox->isChecked();
		m_text = m_findText->currentText();
		m_newText = m_replaceText->currentText();
		m_scope = m_scopeSelection->isChecked() ? 0 : m_scopeWholeFile->isChecked() ? 1 : -1;
		m_dontWrap = m_dontWrapBox->isChecked();
	}
	else
	{
		m_matchCaseBox->setChecked(m_matchCase);
		m_wholeWordBox->setChecked(m_wholeWord);
		m_regExpBox->setChecked(m_regExp);
		setComboText(m_findText, m_text);
		setComboText(m_replaceText, m_newText);
		{
			const QSignalBlocker selectionBlocker(m_scopeSelection);
			const QSignalBlocker wholeFileBlocker(m_scopeWholeFile);
			if (m_scope == 0)
				m_scopeSelection->setChecked(true);
			else if (m_scope == 1)
				m_scopeWholeFile->setChecked(true);
		}
		m_dontWrapBox->setChecked(m_dontWrap);
	}
	updateControls();
}

void EditReplaceDialog::updateRegExp()
{
	if (m_regExp)
	{
		m_wholeWordBox->setEnabled(false);
		m_wholeWord = false;
		m_wholeWordBox->setChecked(false);
	}
	else
		m_wholeWordBox->setEnabled(true);
}

void EditReplaceDialog::updateControls()
{
	const bool hasText = !m_text.isEmpty();
	for (QPushButton *pushButton : { m_findPrev, m_skip, m_replace, m_replaceAll })
		pushButton->setEnabled(hasText);
	// in a selection, Replace All only
	if (m_scope == 0)
		m_replace->setEnabled(false);
	updateRegExp();
}

void EditReplaceDialog::changeEditText()
{
	updateData(true);
	m_found = false;
}

void EditReplaceDialog::changeSelected(int index)
{
	if (index < 0)
		return;
	// (a text typed in and confirmed with Enter is picked too, when the
	// list has it: that is no change)
	const QString item = m_findText->itemText(index);
	if (item == m_text)
		return;
	m_text = item;
	setComboText(m_findText, m_text);
	updateControls();
	m_found = false;
}

void EditReplaceDialog::regExpClicked()
{
	updateData(true);
	updateRegExp();
	updateData(false);
}

void EditReplaceDialog::reject()
{
	updateData(true);
	QDialog::reject();
	m_buddy->focusPane();
	if (m_replaced && m_buddy->selectionPushed())
		m_buddy->setSelection(m_buddy->savedSelStart(), m_buddy->savedSelEnd());
	m_buddy->setSelectionPushed(false);
}

unsigned EditReplaceDialog::searchFlags() const
{
	unsigned flags = 0;
	if (m_matchCase)
		flags |= lm::FindMatchCase;
	if (m_wholeWord)
		flags |= lm::FindWholeWord;
	if (m_regExp)
		flags |= lm::FindRegExp;
	if (m_direction == 0)
		flags |= lm::FindDirectionUp;
	return flags;
}

bool EditReplaceDialog::doHighlightText(bool notifyIfNotFound, bool updateView)
{
	const unsigned flags = searchFlags();
	bool found;
	if (m_scope == 0)
	{
		// in the selection only
		found = m_buddy->findTextInBlock(m_text, m_foundAt, m_blockBegin, m_blockEnd,
			flags, false, &m_foundAt);
	}
	else
		found = m_buddy->findText(m_text, m_foundAt, flags, !m_dontWrap, &m_foundAt);
	if (!found)
	{
		if (notifyIfNotFound)
			PaneSearch::tellNotFound(this, m_text);
		if (m_scope == 0)
			m_currentPos = m_blockBegin;
		return false;
	}
	m_buddy->highlightText(m_foundAt, m_buddy->lastFindWhatLen(), false, updateView);
	return true;
}

/** Where the next search starts: past what is selected, or the cursor
    in a selection; a line further on when what was found had no
    length. */
bool EditReplaceDialog::adjustSearchPos(lm::TextPoint &foundAt) const
{
	if (m_scope != 0)
		foundAt = m_buddy->searchPos(m_direction == 0 ? lm::FindDirectionUp : 0);
	else
		foundAt = m_buddy->cursorPos();
	if (m_buddy->lastFindWhatLen() == 0)
	{
		if (m_direction != 0)
		{
			if (foundAt.y + 1 >= m_buddy->lineCount())
				return false;
			foundAt.x = 0;
			++foundAt.y;
		}
		else
		{
			if (foundAt.y - 1 < 0)
				return false;
			--foundAt.y;
			foundAt.x = m_buddy->lineLength(foundAt.y);
		}
	}
	return true;
}

void EditReplaceDialog::setFoundButtons(bool found)
{
	QPushButton *on = found ? m_replace : m_skip;
	QPushButton *off = found ? m_skip : m_replace;
	off->setDefault(false);
	on->setDefault(true);
}

void EditReplaceDialog::findNextPrev(bool next)
{
	updateData(true);
	m_direction = next ? 1 : 0;
	fillCurrent(m_findText, kFindGroup);
	fillCurrent(m_replaceText, kReplaceGroup);
	lm::saveSearchHistory();
	updateLastSearch();
	if (!m_found)
	{
		m_foundAt = m_buddy->cursorPos();
		m_found = doHighlightText(true);
		setFoundButtons(m_found);
		return;
	}
	if (!adjustSearchPos(m_foundAt))
	{
		m_found = false;
		return;
	}
	m_found = doHighlightText(true);
	setFoundButtons(m_found);
}

void EditReplaceDialog::editReplace()
{
	updateData(true);
	fillCurrent(m_findText, kFindGroup);
	fillCurrent(m_replaceText, kReplaceGroup);
	lm::saveSearchHistory();
	updateLastSearch();
	if (!m_found)
	{
		m_foundAt = m_buddy->cursorPos();
		m_found = doHighlightText(true);
		setFoundButtons(m_found);
		return;
	}
	// what is selected is what was found
	m_buddy->replaceSelection(m_newText, searchFlags());
	if (m_enableScopeSelection)
	{
		m_blockBegin = m_buddy->savedSelStart();
		m_blockEnd = m_buddy->savedSelEnd();
	}
	if (!adjustSearchPos(m_foundAt))
	{
		m_found = false;
		return;
	}
	m_found = doHighlightText(true);
	m_replaced = true;
}

void EditReplaceDialog::editReplaceAll()
{
	updateData(true);
	fillCurrent(m_findText, kFindGroup);
	fillCurrent(m_replaceText, kReplaceGroup);
	lm::saveSearchHistory();
	updateLastSearch();
	int replaced = 0;
	bool wrapped = false;
	QGuiApplication::setOverrideCursor(Qt::WaitCursor);
	if (!m_found)
	{
		m_foundAt = m_currentPos;
		m_found = doHighlightText(false, false);
	}
	// the first one found, kept in step with the replacements before it
	const QTextCursor firstFound = m_buddy->trackPoint(m_foundAt);
	// all of them, one undo step
	QTextCursor group(m_buddy->pane()->document());
	group.beginEditBlock();
	while (m_found)
	{
		m_buddy->replaceSelection(m_newText, searchFlags());
		if (m_buddy->lastFindWhatLen() != 0 || m_buddy->lastReplaceLen() != 0)
			++replaced;
		if (m_enableScopeSelection)
		{
			m_blockBegin = m_buddy->savedSelStart();
			m_blockEnd = m_buddy->savedSelEnd();
		}
		const lm::TextPoint replacedEnd = m_buddy->cursorPos();
		if (!adjustSearchPos(m_foundAt))
		{
			m_found = false;
			break;
		}
		m_found = doHighlightText(false, false);
		// round the end of the file and back, it stops at the first one,
		// so that a replacement with the text in it ("here" by "there") is
		// not replaced again
		if (m_foundAt < replacedEnd)
			wrapped = true;
		if (wrapped && !(m_foundAt < m_buddy->pointOf(firstFound)))
			break;
	}
	group.endEditBlock();
	QGuiApplication::restoreOverrideCursor();
	m_buddy->ensureSelectionVisible();
	lm::showNumReplaced(this, replaced);
	m_replaced = true;
}

void EditReplaceDialog::updateLastSearch()
{
	m_lastSearch.matchCase = m_matchCase;
	m_lastSearch.wholeWord = m_wholeWord;
	m_lastSearch.regExp = m_regExp;
	m_lastSearch.text = m_text;
	m_lastSearch.noWrap = m_dontWrap;
	m_lastSearch.direction = m_direction;
	m_buddy->saveLastSearch(m_lastSearch);
}

void EditReplaceDialog::useLastSearch()
{
	m_matchCase = m_lastSearch.matchCase;
	m_wholeWord = m_lastSearch.wholeWord;
	m_regExp = m_lastSearch.regExp;
	m_text = m_lastSearch.text;
	m_dontWrap = m_lastSearch.noWrap;
	m_direction = m_lastSearch.direction;
}

void EditReplaceDialog::setScope(bool withSelection)
{
	m_scope = withSelection ? 0 : 1;
}
