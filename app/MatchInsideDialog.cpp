// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "MatchInsideDialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include "FileFilterCombo.h"
#include "FileFilters.h"
#include "LineFilterHelper.h"
#include "LineFilterMenu.h"

MatchInsideDialog::MatchInsideDialog(const QString &filter1, const QString &filter2,
	QWidget *parent)
	: QDialog(parent)
	, m_checker1(std::make_unique<LineFilterHelper>())
	, m_checker2(std::make_unique<LineFilterHelper>())
	, m_filter1(filter1)
	, m_filter2(filter2)
{
	setObjectName(QStringLiteral("matchInsideDialog"));
	setWindowModality(Qt::WindowModal);
	setWindowTitle(tr("Match Inside/Outside Filter Expressions"));

	auto *layout = new QVBoxLayout(this);
	m_grid = new QGridLayout;
	m_grid->setColumnStretch(1, 1);
	layout->addLayout(m_grid);
	m_field1 = addField(tr("&Start filter:"), "matchInsideStart", m_checker1.get(), filter1, 0);
	m_field2 = addField(tr("&End filter:"), "matchInsideEnd", m_checker2.get(), filter2, 1);
	auto *note = new QLabel(
		tr("Lines between start and end filter matches will be included."), this);
	note->setWordWrap(true);
	layout->addWidget(note);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &MatchInsideDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	resize(560, sizeHint().height());
}

MatchInsideDialog::~MatchInsideDialog() = default;

/** A filter field as the bar has it, with its "=" button. */
FileFilterCombo *MatchInsideDialog::addField(const QString &label, const char *name,
	LineFilterHelper *checker, const QString &initial, int row)
{
	auto *field = new FileFilterCombo(this);
	field->setObjectName(QLatin1String(name));
	field->setHistoryKey(lm::lineDisplayFilterHistoryKey());
	field->setChecker([checker](const QString &text) {
		return lm::lineFilterErrors(checker, text);
	});
	field->lineEdit()->setPlaceholderText(
		tr("e.g. %1").arg(QStringLiteral("ERROR / le:Line contains \"ERROR\"")));
	// the history's latest entry, unless the field was given a filter
	field->loadHistoryAndShowLatest();
	if (!initial.isEmpty())
		field->setMask(initial, false);
	else
		field->checkNow();

	auto *caption = new QLabel(label, this);
	caption->setBuddy(field);
	m_grid->addWidget(caption, row, 0);
	m_grid->addWidget(field, row, 1);

	// the "=" button: a menu of its own for each press, with its targets
	// and operator as they are at first. It lasts until the next press: a
	// target picked in it brings it back for the condition to follow
	auto *button = new QToolButton(this);
	button->setObjectName(QLatin1String(name) + QStringLiteral("Menu"));
	button->setText(QStringLiteral("="));
	m_grid->addWidget(button, row, 2);
	connect(button, &QToolButton::clicked, this, [this, field, button]() {
		const QString popupName = button->objectName() + QStringLiteral("Popup");
		delete findChild<LineFilterMenu *>(popupName, Qt::FindDirectChildrenOnly);
		auto *menu = new LineFilterMenu(this);
		menu->setObjectName(popupName);
		menu->setFilterSource([field]() { return field->mask(); });
		const auto show = [menu, button]() {
			menu->popup(button->mapToGlobal(QPoint(0, button->height())));
		};
		connect(menu, &LineFilterMenu::reopenRequested, menu, show, Qt::QueuedConnection);
		connect(menu, &LineFilterMenu::filterChosen, field, [field](const QString &filter) {
			field->setMask(filter, false);
		});
		show();
	});
	return field;
}

void MatchInsideDialog::accept()
{
	m_filter1 = m_field1->mask();
	m_filter2 = m_field2->mask();
	QDialog::accept();
}
