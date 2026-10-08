// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextMarkerDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace
{

std::function<bool(TextMarkerDialog *)> &presenter()
{
	static std::function<bool(TextMarkerDialog *)> handler;
	return handler;
}

constexpr int kKeyRole = Qt::UserRole;

} // namespace

TextMarkerDialog::TextMarkerDialog(const QString &text, unsigned flags, QWidget *parent)
	: QDialog(parent), m_tempMarkers(lm::TextMarkers::instance()->markers())
{
	setWindowTitle(tr("Markers"));
	// the pane's text: a new marker, in the color after the others'
	if (!text.isEmpty())
	{
		lm::TextMarkers::Marker marker;
		marker.findWhat = text;
		marker.flags = flags;
		marker.color = lm::TextMarkers::MarkerColor1 + static_cast<int>(m_tempMarkers.size() % 3);
		marker.userDefined = true;
		marker.visible = true;
		m_tempMarkers.insert_or_assign(lm::TextMarkers::makeNewId(m_tempMarkers), marker);
	}

	auto *layout = new QVBoxLayout(this);
	m_enabledBox = new QCheckBox(tr("Enable &markers"), this);
	m_enabledBox->setChecked(lm::TextMarkers::instance()->enabled());
	layout->addWidget(m_enabledBox);

	auto *listRow = new QHBoxLayout;
	m_list = new QListWidget(this);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	// as tall as upstream's list at first, taller with the dialog
	m_list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
	m_list->setMinimumHeight(m_list->fontMetrics().height() * 6 + 2 * m_list->frameWidth());
	listRow->addWidget(m_list, 1);
	auto *listButtons = new QVBoxLayout;
	m_new = new QPushButton(tr("New"), this);
	m_delete = new QPushButton(tr("&Delete"), this);
	for (QPushButton *button : { m_new, m_delete })
	{
		button->setAutoDefault(false);
		listButtons->addWidget(button);
	}
	listButtons->addStretch(1);
	listRow->addLayout(listButtons);
	layout->addLayout(listRow);

	auto *fields = new QGridLayout;
	auto *findLabel = new QLabel(tr("Fi&nd what:"), this);
	m_findWhat = new QComboBox(this);
	m_findWhat->setEditable(true);
	m_findWhat->setInsertPolicy(QComboBox::NoInsert);
	m_findWhat->setCompleter(nullptr);
	findLabel->setBuddy(m_findWhat);
	auto *colorLabel = new QLabel(tr("&Background color:"), this);
	m_color = new QComboBox(this);
	for (int i = 0; i < 3; ++i)
		m_color->addItem(tr("Marker Color %1").arg(i + 1));
	colorLabel->setBuddy(m_color);
	fields->addWidget(findLabel, 0, 0);
	fields->addWidget(m_findWhat, 0, 1);
	fields->addWidget(colorLabel, 1, 0);
	fields->addWidget(m_color, 1, 1);
	fields->setColumnStretch(1, 1);
	layout->addLayout(fields);
	m_wholeWordBox = new QCheckBox(tr("Match &whole word only"), this);
	m_matchCaseBox = new QCheckBox(tr("Match &case"), this);
	m_regExpBox = new QCheckBox(tr("Regular &expression"), this);
	m_matchCaseBox->setChecked((flags & lm::FindMatchCase) != 0);
	m_wholeWordBox->setChecked((flags & lm::FindWholeWord) != 0);
	m_regExpBox->setChecked((flags & lm::FindRegExp) != 0);
	for (QCheckBox *box : { m_wholeWordBox, m_matchCaseBox, m_regExpBox })
		layout->addWidget(box);

	auto *buttons = new QHBoxLayout;
	buttons->addStretch(1);
	auto *ok = new QPushButton(tr("&Ok"), this);
	ok->setDefault(true);
	m_apply = new QPushButton(tr("&Apply"), this);
	m_apply->setAutoDefault(false);
	auto *cancel = new QPushButton(tr("Cancel"), this);
	cancel->setAutoDefault(false);
	buttons->addWidget(ok);
	buttons->addWidget(m_apply);
	buttons->addWidget(cancel);
	layout->addLayout(buttons);

	connect(ok, &QPushButton::clicked, this, &TextMarkerDialog::accept);
	connect(m_apply, &QPushButton::clicked, this, &TextMarkerDialog::applyNow);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_new, &QPushButton::clicked, this, &TextMarkerDialog::newMarker);
	connect(m_delete, &QPushButton::clicked, this, &TextMarkerDialog::deleteMarker);
	connect(m_list, &QListWidget::itemSelectionChanged, this, &TextMarkerDialog::selectionChanged);
	connect(m_findWhat->lineEdit(), &QLineEdit::textEdited,
		this, &TextMarkerDialog::findWhatEdited);
	connect(m_regExpBox, &QCheckBox::clicked, this, &TextMarkerDialog::regExpClicked);

	// OnInitDialog: the user's markers by key, ticked when they show, the
	// last one chosen
	for (const auto &entry : m_tempMarkers)
	{
		if (entry.second.userDefined)
			addItem(entry.first, false);
	}
	if (m_list->count() > 0)
		m_list->setCurrentRow(m_list->count() - 1);
	m_wholeWordBox->setEnabled(!m_regExpBox->isChecked());
}

unsigned TextMarkerDialog::lastSearchFlags() const
{
	return (m_matchCaseBox->isChecked() ? lm::FindMatchCase : 0u)
		| (m_wholeWordBox->isChecked() ? lm::FindWholeWord : 0u)
		| (m_regExpBox->isChecked() ? lm::FindRegExp : 0u);
}

QString TextMarkerDialog::keyOf(const QListWidgetItem *item) const
{
	return item != nullptr ? item->data(kKeyRole).toString() : QString();
}

void TextMarkerDialog::addItem(const QString &key, bool select)
{
	const lm::TextMarkers::Marker &marker = m_tempMarkers.at(key);
	auto *item = new QListWidgetItem(marker.findWhat);
	item->setData(kKeyRole, key);
	item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
	item->setCheckState(marker.visible ? Qt::Checked : Qt::Unchecked);
	m_list->addItem(item);
	if (select)
		m_list->setCurrentItem(item);
}

/** UpdateDataListView(true): the marker the fields belong to takes what
    they say. */
void TextMarkerDialog::keepFields()
{
	const auto it = m_tempMarkers.find(m_shownKey);
	if (it == m_tempMarkers.end())
		return;
	it->second.findWhat = m_findWhat->currentText();
	it->second.color = lm::TextMarkers::MarkerColor1 + m_color->currentIndex();
	it->second.flags = lastSearchFlags();
}

/** UpdateDataListView(false): the fields show a marker; without one,
    they stay as they are. A regular expression leaves whole word out. */
void TextMarkerDialog::showMarker(const QString &key)
{
	m_shownKey = key;
	const auto it = m_tempMarkers.find(key);
	if (it != m_tempMarkers.end())
	{
		const lm::TextMarkers::Marker &marker = it->second;
		const QSignalBlocker blocker(m_findWhat);
		m_findWhat->setEditText(marker.findWhat);
		m_color->setCurrentIndex(qBound(0, marker.color - lm::TextMarkers::MarkerColor1, 2));
		const bool regExp = (marker.flags & lm::FindRegExp) != 0;
		m_wholeWordBox->setChecked(!regExp && (marker.flags & lm::FindWholeWord) != 0);
		m_matchCaseBox->setChecked((marker.flags & lm::FindMatchCase) != 0);
		m_regExpBox->setChecked(regExp);
	}
	m_wholeWordBox->setEnabled(!m_regExpBox->isChecked());
}

/** OnItemchangingEditMarkerList and OnItemchangedEditMarkerList: the
    marker left keeps the fields, the one chosen fills them. */
void TextMarkerDialog::selectionChanged()
{
	keepFields();
	showMarker(keyOf(m_list->currentItem() != nullptr && m_list->currentItem()->isSelected()
		? m_list->currentItem() : nullptr));
}

void TextMarkerDialog::newMarker()
{
	keepFields();
	const QString key = lm::TextMarkers::makeNewId(m_tempMarkers);
	lm::TextMarkers::Marker marker;
	marker.findWhat = tr("New Pattern");
	marker.flags = lastSearchFlags();
	marker.color = lm::TextMarkers::MarkerColor1 + static_cast<int>(m_tempMarkers.size() % 3);
	marker.userDefined = true;
	marker.visible = true;
	m_tempMarkers.insert_or_assign(key, marker);
	addItem(key, true);
}

void TextMarkerDialog::deleteMarker()
{
	int row = m_list->currentRow();
	if (row < 0)
		return;
	const QString key = keyOf(m_list->item(row));
	m_shownKey.clear(); // the fields go with it
	delete m_list->takeItem(row);
	m_tempMarkers.erase(key);
	// the one that took its place, or the last
	if (row >= m_list->count() - 1)
		row = m_list->count() - 1;
	if (row >= 0)
		m_list->setCurrentRow(row);
}

void TextMarkerDialog::findWhatEdited(const QString &text)
{
	if (QListWidgetItem *item = m_list->currentItem(); item != nullptr && item->isSelected())
		item->setText(text);
}

void TextMarkerDialog::regExpClicked()
{
	if (m_regExpBox->isChecked())
		m_wholeWordBox->setChecked(false);
	m_wholeWordBox->setEnabled(!m_regExpBox->isChecked());
	keepFields();
}

/** OnBnClickedApplyNow: the panes take the markers. */
void TextMarkerDialog::applyNow()
{
	keepFields();
	for (int i = 0; i < m_list->count(); ++i)
	{
		const auto it = m_tempMarkers.find(keyOf(m_list->item(i)));
		if (it != m_tempMarkers.end())
			it->second.visible = m_list->item(i)->checkState() == Qt::Checked;
	}
	lm::TextMarkers::instance()->setMarkers(m_tempMarkers, m_enabledBox->isChecked());
}

void TextMarkerDialog::accept()
{
	applyNow();
	QDialog::accept();
}

bool TextMarkerDialog::run(TextMarkerDialog *dialog)
{
	if (presenter())
		return presenter()(dialog);
	return dialog->exec() == QDialog::Accepted;
}

void TextMarkerDialog::setPresenterForTest(std::function<bool(TextMarkerDialog *dialog)> handler)
{
	presenter() = std::move(handler);
}
