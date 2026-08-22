// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "FolderCompareView.h"

#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{

enum Column
{
	ColName = 0,
	ColFolder,
	ColResult,
	ColLeftSize,
	ColRightSize,
	ColLeftDate,
	ColRightDate,
	ColCount,
};

enum ItemRole
{
	RoleLeftPath = Qt::UserRole,
	RoleRightPath,
	RoleIsFile,
	RoleBothSides,
};

QString categoryText(const lm::FolderCompareItem &item)
{
	switch (item.category)
	{
	case lm::FolderCompareItem::Identical: return QObject::tr("Identical");
	case lm::FolderCompareItem::Different: return QObject::tr("Different");
	case lm::FolderCompareItem::LeftOnly: return QObject::tr("Left only");
	case lm::FolderCompareItem::RightOnly: return QObject::tr("Right only");
	case lm::FolderCompareItem::Skipped: return QObject::tr("Skipped");
	case lm::FolderCompareItem::Error: return QObject::tr("Error");
	}
	return {};
}

QColor categoryColor(const lm::FolderCompareItem &item)
{
	switch (item.category)
	{
	case lm::FolderCompareItem::Different: return QColor(255, 243, 176);
	case lm::FolderCompareItem::LeftOnly: return QColor(224, 236, 255);
	case lm::FolderCompareItem::RightOnly: return QColor(224, 255, 232);
	case lm::FolderCompareItem::Error: return QColor(255, 214, 214);
	default: return {};
	}
}

QString sizeText(qint64 size)
{
	return size < 0 ? QString() : QLocale().toString(size);
}

QString dateText(const QDateTime &dt)
{
	return dt.isValid() ? QLocale().toString(dt, QLocale::ShortFormat) : QString();
}

} // namespace

FolderCompareView::FolderCompareView(QWidget *parent)
	: QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	m_tree = new QTreeWidget(this);
	m_tree->setColumnCount(ColCount);
	m_tree->setHeaderLabels({ tr("Name"), tr("Folder"), tr("Comparison result"),
		tr("Left size"), tr("Right size"), tr("Left date"), tr("Right date") });
	m_tree->setRootIsDecorated(false);
	m_tree->setAlternatingRowColors(true);
	m_tree->setSortingEnabled(true);
	m_tree->setUniformRowHeights(true);
	connect(m_tree, &QTreeWidget::itemActivated, this, &FolderCompareView::itemActivated);
	layout->addWidget(m_tree, 1);

	m_status = new QLabel(this);
	m_status->setContentsMargins(6, 3, 6, 3);
	layout->addWidget(m_status);
}

bool FolderCompareView::compare(const QString &leftDir, const QString &rightDir, QString *error)
{
	const lm::FolderCompareResult result = lm::compareFolders(leftDir, rightDir, true);
	if (!result.ok)
	{
		if (error != nullptr)
			*error = result.error;
		return false;
	}

	m_tree->setSortingEnabled(false);
	for (const lm::FolderCompareItem &item : result.items)
	{
		auto *row = new QTreeWidgetItem(m_tree);
		row->setText(ColName, item.name);
		row->setText(ColFolder, item.folder);
		row->setText(ColResult, item.isDir
			? tr("Folder: %1").arg(categoryText(item)) : categoryText(item));
		row->setText(ColLeftSize, sizeText(item.size[0]));
		row->setText(ColRightSize, sizeText(item.size[1]));
		row->setText(ColLeftDate, dateText(item.mtime[0]));
		row->setText(ColRightDate, dateText(item.mtime[1]));
		row->setTextAlignment(ColLeftSize, Qt::AlignRight | Qt::AlignVCenter);
		row->setTextAlignment(ColRightSize, Qt::AlignRight | Qt::AlignVCenter);
		const QColor color = categoryColor(item);
		if (color.isValid())
		{
			for (int col = 0; col < ColCount; ++col)
				row->setBackground(col, color);
		}
		row->setData(0, RoleLeftPath, item.leftPath);
		row->setData(0, RoleRightPath, item.rightPath);
		row->setData(0, RoleIsFile, !item.isDir);
		row->setData(0, RoleBothSides,
			!item.leftPath.isEmpty() && !item.rightPath.isEmpty());
	}
	m_tree->setSortingEnabled(true);
	m_tree->sortByColumn(ColFolder, Qt::AscendingOrder);
	for (int col = 0; col < ColCount; ++col)
		m_tree->resizeColumnToContents(col);

	m_status->setText(tr("%1 item(s): %2 different, %3 unique, %4 identical")
		.arg(result.items.size()).arg(result.different).arg(result.unique)
		.arg(result.identical));
	return true;
}

void FolderCompareView::itemActivated(QTreeWidgetItem *item, int column)
{
	Q_UNUSED(column);
	if (item == nullptr)
		return;
	if (!item->data(0, RoleIsFile).toBool() || !item->data(0, RoleBothSides).toBool())
		return;
	emit openFileComparisonRequested(item->data(0, RoleLeftPath).toString(),
		item->data(0, RoleRightPath).toString());
}
