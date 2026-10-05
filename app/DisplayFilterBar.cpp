// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "DisplayFilterBar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>

#include "FileFilterCombo.h"
#include "FileFilterHelper.h"
#include "FileFilterMenu.h"
#include "FileFilters.h"
#include "LineFilterHelper.h"
#include "LineFilterMenu.h"

DisplayFilterBar::DisplayFilterBar(Kind kind, QWidget *parent)
	: QWidget(parent)
	, m_kind(kind)
{
	setObjectName(QStringLiteral("displayFilterBar"));
	auto *row = new QHBoxLayout(this);
	row->setContentsMargins(6, 3, 6, 3);
	row->setSpacing(6);

	m_field = new FileFilterCombo(this);
	m_field->setObjectName(QStringLiteral("displayFilterMask"));
	if (kind == Lines)
	{
		m_lineChecker = std::make_unique<LineFilterHelper>();
		m_field->setHistoryKey(lm::lineDisplayFilterHistoryKey());
		m_field->setChecker([this](const QString &text) {
			return lm::lineFilterErrors(m_lineChecker.get(), text);
		});
		m_field->lineEdit()->setPlaceholderText(
			tr("e.g. %1").arg(QStringLiteral("ERROR / le:Line contains \"ERROR\"")));
	}
	else
	{
		m_checker = std::make_unique<FileFilterHelper>();
		m_field->setHistoryKey(lm::displayFilterHistoryKey());
		m_field->setChecker([this](const QString &text) {
			return lm::fileFilterErrors(m_checker.get(), text);
		});
		m_field->lineEdit()->setPlaceholderText(
			tr("e.g. %1").arg(QStringLiteral("*.txt|fe:Size > 100KB")));
	}
	m_field->lineEdit()->installEventFilter(this);
	row->addWidget(m_field, 1);

	// the "=" button: ready-made changes to the filter
	// (CFileFilterHelperMenu, CLineFilterHelperMenu)
	m_menuButton = new QToolButton(this);
	m_menuButton->setObjectName(QStringLiteral("displayFilterMaskMenu"));
	m_menuButton->setText(QStringLiteral("="));
	row->addWidget(m_menuButton);
	connect(m_menuButton, &QToolButton::clicked, this, &DisplayFilterBar::showMenu);
	if (kind == Lines)
	{
		auto *menu = new LineFilterMenu(this);
		menu->setFilterSource([this]() { return m_field->mask(); });
		connect(menu, &LineFilterMenu::reopenRequested, this, &DisplayFilterBar::showMenu,
			Qt::QueuedConnection);
		connect(menu, &LineFilterMenu::filterChosen, this, &DisplayFilterBar::takeFromMenu);
		m_menu = menu;
	}
	else
	{
		auto *menu = new FileFilterMenu(this);
		menu->setMaskSource([this]() { return m_field->mask(); });
		connect(menu, &FileFilterMenu::reopenRequested, this, &DisplayFilterBar::showMenu,
			Qt::QueuedConnection);
		connect(menu, &FileFilterMenu::maskChosen, this, &DisplayFilterBar::takeFromMenu);
		m_menu = menu;
	}

	m_apply = new QPushButton(tr("&Apply"), this);
	m_apply->setObjectName(QStringLiteral("displayFilterApply"));
	m_apply->setAutoDefault(false);
	connect(m_apply, &QPushButton::clicked, this, &DisplayFilterBar::applyRequested);
	row->addWidget(m_apply);
	m_close = new QPushButton(tr("&Close"), this);
	m_close->setObjectName(QStringLiteral("displayFilterClose"));
	m_close->setAutoDefault(false);
	connect(m_close, &QPushButton::clicked, this, &DisplayFilterBar::closeRequested);
	row->addWidget(m_close);

	// Create: the list of the filters used before, its latest in the field
	m_field->loadHistoryAndShowLatest();
	m_field->checkNow();
}

DisplayFilterBar::~DisplayFilterBar() = default;

void DisplayFilterBar::showMenu()
{
	m_menu->popup(m_menuButton->mapToGlobal(QPoint(0, m_menuButton->height())));
}

/** ShowFilterMenu: what an item made goes into the field, to be applied. */
void DisplayFilterBar::takeFromMenu(const QString &filter)
{
	m_field->setMask(filter, false);
	m_field->setApplied(false);
	focusField();
}

QString DisplayFilterBar::filterText() const
{
	return m_field->mask();
}

void DisplayFilterBar::setFilterText(const QString &text)
{
	m_field->setMask(text, false);
}

void DisplayFilterBar::saveFilterText()
{
	if (m_field->mask().isEmpty())
		return;
	// CSuperComboBox's SaveState, then LoadState: the list is read again
	// and the field shows its latest entry
	lm::rememberFileFilter(m_field->mask(), m_kind == Lines
		? lm::lineDisplayFilterHistoryKey() : lm::displayFilterHistoryKey());
	m_field->loadHistoryAndShowLatest();
	m_field->checkNow();
}

void DisplayFilterBar::setFilterApplied(bool applied)
{
	m_field->setApplied(applied);
}

void DisplayFilterBar::focusField()
{
	m_field->setFocus();
	m_field->lineEdit()->selectAll();
}

/** Enter in the field is the default button, Apply; Esc is Close. The
    combo keeps both keys to itself otherwise. */
bool DisplayFilterBar::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == m_field->lineEdit() && event->type() == QEvent::KeyPress)
	{
		const int key = static_cast<QKeyEvent *>(event)->key();
		if (key == Qt::Key_Return || key == Qt::Key_Enter)
		{
			emit applyRequested();
			return true;
		}
		if (key == Qt::Key_Escape)
		{
			emit closeRequested();
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}

/** The same two keys with a button in front: Enter presses that button,
    Esc closes the bar instead of going up to the window. */
void DisplayFilterBar::keyPressEvent(QKeyEvent *event)
{
	switch (event->key())
	{
	case Qt::Key_Escape:
		emit closeRequested();
		return;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		if (auto *button = qobject_cast<QAbstractButton *>(focusWidget()))
			button->click();
		else
			emit applyRequested();
		return;
	default:
		QWidget::keyPressEvent(event);
	}
}
