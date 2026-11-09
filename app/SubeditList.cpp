// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "SubeditList.h"

#include "WildcardDropList.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QMouseEvent>

namespace lm
{

SubeditList::SubeditList(QWidget *parent)
	: QTreeWidget(parent)
{
	// a report list (LVS_REPORT) of one selected row at a time
	// (LVS_SINGLESEL), selected whole (LVS_EX_FULLROWSELECT), whose
	// headers do not sort (LVS_NOSORTHEADER)
	setRootIsDecorated(false);
	setUniformRowHeights(true);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setAllColumnsShowFocus(true);
	setEditTriggers(QAbstractItemView::NoEditTriggers);
	header()->setSectionsClickable(false);
	header()->setStretchLastSection(false);
}

void SubeditList::setReadOnlyColumn(int column)
{
	m_readOnlyColumns.insert(column);
}

void SubeditList::setWildcardDropListColumn(int column, int columns)
{
	m_wildcardColumn = column;
	m_wildcardColumns = columns;
}

void SubeditList::setDropListFixedPattern(int row, int column, const QString &fixedPattern)
{
	m_fixedPatterns.insert(qMakePair(row, column), fixedPattern);
}

QString SubeditList::dropListFixedPattern(int row, int column) const
{
	return m_fixedPatterns.value(qMakePair(row, column));
}

void SubeditList::mousePressEvent(QMouseEvent *event)
{
	// OnLButtonDown: a click on the row that has the focus edits the cell
	// under it, a click on another row gives that row the focus
	const QModelIndex hit = indexAt(event->position().toPoint());
	const bool focused = hit.isValid() && currentIndex().isValid()
		&& currentIndex().row() == hit.row();
	QTreeWidget::mousePressEvent(event);
	if (event->button() != Qt::LeftButton || !focused || m_readOnlyColumns.contains(hit.column()))
		return;
	if (hit.column() == m_wildcardColumn)
		editSubLabel(hit.row(), hit.column());
}

void SubeditList::editSubLabel(int row, int column)
{
	QTreeWidgetItem *item = topLevelItem(row);
	if (item == nullptr || column != m_wildcardColumn || m_editor != nullptr)
		return;
	scrollToItem(item);
	if (columnWidth(column) < 5)
		return;

	// WildcardDropList::OnItemActivate: a combo box over the cell, its
	// text the cell's with the caret at its end, the drop-down open
	const QRect cell = visualRect(indexFromItem(item, column));
	auto *editor = new WildcardDropList(viewport());
	editor->setFont(font());
	editor->setColumns(m_wildcardColumns);
	editor->setFixedPatterns(dropListFixedPattern(row, column));
	editor->setAllowUserAddedPatterns(true);
	const QString text = item->text(column);
	editor->setEditText(text);
	editor->lineEdit()->setCursorPosition(static_cast<int>(text.size()));
	editor->setGeometry(cell.left(), cell.top() - 1, cell.width(), editor->sizeHint().height());
	m_editor = editor;
	// CBN_CLOSEUP: the cell takes the field, which goes, and the list
	// has the focus back
	connect(editor, &WildcardDropList::closedUp, this, [this, editor, row, column]() {
		if (QTreeWidgetItem *edited = topLevelItem(row))
			edited->setText(column, editor->currentText());
		editor->hide();
		editor->deleteLater();
		m_editor = nullptr;
		setFocus();
	});
	editor->show();
	editor->setFocus();
	editor->showPopup();
}

} // namespace lm
