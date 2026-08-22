// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "FolderCompareView.h"
#include "FileOps.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

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
	RoleFolder,
	RoleName,
};

const QString kFilterSettingsKey = QStringLiteral("FolderCompare/Filter");

QString categoryText(lm::FolderCompareItem::Category category)
{
	switch (category)
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

QColor categoryColor(lm::FolderCompareItem::Category category)
{
	switch (category)
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

void setRowCategory(QTreeWidgetItem *row, lm::FolderCompareItem::Category category,
	bool isDir)
{
	row->setText(ColResult, isDir
		? QObject::tr("Folder: %1").arg(categoryText(category)) : categoryText(category));
	const QColor color = categoryColor(category);
	for (int col = 0; col < ColCount; ++col)
		row->setBackground(col, color.isValid() ? QBrush(color) : QBrush());
}

} // namespace

FolderCompareView::FolderCompareView(QWidget *parent)
	: QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto *toolbar = new QToolBar(this);
	toolbar->setIconSize(QSize(16, 16));
	toolbar->addWidget(new QLabel(tr(" Filter: "), this));
	m_filterEdit = new QLineEdit(this);
	m_filterEdit->setPlaceholderText(tr("*.* \xE2\x80\x94 masks (*.cpp;*.h), f:/d: regexes or expressions"));
	m_filterEdit->setMaximumWidth(340);
	m_filterEdit->setText(QSettings().value(kFilterSettingsKey, QStringLiteral("*.*")).toString());
	toolbar->addWidget(m_filterEdit);
	QAction *applyAction = toolbar->addAction(tr("Recompare"));
	applyAction->setShortcut(QKeySequence(Qt::Key_F5));
	applyAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(applyAction);
	connect(applyAction, &QAction::triggered, this, &FolderCompareView::recompare);
	connect(m_filterEdit, &QLineEdit::returnPressed, this, &FolderCompareView::recompare);
	toolbar->addSeparator();
	m_actCopyRight = toolbar->addAction(tr("Copy \xE2\x86\x92 Right"));
	connect(m_actCopyRight, &QAction::triggered, this, [this]() { copySelected(0); });
	m_actCopyLeft = toolbar->addAction(tr("Copy \xE2\x86\x90 Left"));
	connect(m_actCopyLeft, &QAction::triggered, this, [this]() { copySelected(1); });
	toolbar->addSeparator();
	m_actDeleteLeft = toolbar->addAction(tr("Delete Left"));
	connect(m_actDeleteLeft, &QAction::triggered, this, [this]() { deleteSelected(true, false); });
	m_actDeleteRight = toolbar->addAction(tr("Delete Right"));
	connect(m_actDeleteRight, &QAction::triggered, this, [this]() { deleteSelected(false, true); });
	m_actDeleteBoth = toolbar->addAction(tr("Delete Both"));
	connect(m_actDeleteBoth, &QAction::triggered, this, [this]() { deleteSelected(true, true); });
	layout->addWidget(toolbar);

	m_tree = new QTreeWidget(this);
	m_tree->setColumnCount(ColCount);
	m_tree->setHeaderLabels({ tr("Name"), tr("Folder"), tr("Comparison result"),
		tr("Left size"), tr("Right size"), tr("Left date"), tr("Right date") });
	m_tree->setRootIsDecorated(false);
	m_tree->setAlternatingRowColors(true);
	m_tree->setSortingEnabled(true);
	m_tree->setUniformRowHeights(true);
	m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_tree, &QTreeWidget::itemActivated, this, &FolderCompareView::itemActivated);
	connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &FolderCompareView::updateActions);
	connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
		QMenu menu(this);
		menu.addAction(m_actCopyRight);
		menu.addAction(m_actCopyLeft);
		menu.addSeparator();
		menu.addAction(m_actDeleteLeft);
		menu.addAction(m_actDeleteRight);
		menu.addAction(m_actDeleteBoth);
		menu.exec(m_tree->viewport()->mapToGlobal(pos));
	});
	layout->addWidget(m_tree, 1);

	auto *statusRow = new QHBoxLayout;
	statusRow->setContentsMargins(6, 3, 6, 3);
	m_status = new QLabel(this);
	statusRow->addWidget(m_status, 1);
	m_progress = new QProgressBar(this);
	m_progress->setMaximumWidth(240);
	m_progress->setVisible(false);
	statusRow->addWidget(m_progress);
	m_cancelButton = new QPushButton(tr("Cancel"), this);
	m_cancelButton->setVisible(false);
	connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
		if (m_job)
			m_job->requestAbort();
		m_cancelButton->setEnabled(false);
		m_status->setText(tr("Cancelling\xE2\x80\xA6"));
	});
	statusRow->addWidget(m_cancelButton);
	layout->addLayout(statusRow);

	m_progressTimer = new QTimer(this);
	m_progressTimer->setInterval(150);
	connect(m_progressTimer, &QTimer::timeout, this, &FolderCompareView::updateProgress);
	connect(&m_watcher, &QFutureWatcher<lm::FolderCompareResult>::finished,
		this, &FolderCompareView::compareFinished);

	updateActions();
}

FolderCompareView::~FolderCompareView()
{
	if (m_job)
		m_job->requestAbort();
	m_watcher.waitForFinished();
}

void FolderCompareView::start(const QString &leftDir, const QString &rightDir)
{
	m_roots[0] = leftDir;
	m_roots[1] = rightDir;
	m_tree->clear();
	updateActions();

	const QString filterMask = m_filterEdit->text().trimmed();
	QSettings().setValue(kFilterSettingsKey,
		filterMask.isEmpty() ? QStringLiteral("*.*") : filterMask);

	m_job = std::make_shared<lm::FolderCompareJob>();
	m_status->setText(tr("Scanning\xE2\x80\xA6"));
	m_progress->setRange(0, 0); // busy until totals are known
	m_progress->setVisible(true);
	m_cancelButton->setVisible(true);
	m_cancelButton->setEnabled(true);
	m_progressTimer->start();

	auto job = m_job;
	m_watcher.setFuture(QtConcurrent::run([leftDir, rightDir, job, filterMask]() {
		return lm::compareFolders(leftDir, rightDir, true, job, filterMask);
	}));
}

void FolderCompareView::recompare()
{
	if (m_job)
		return; // already comparing
	if (!m_roots[0].isEmpty() && !m_roots[1].isEmpty())
		start(m_roots[0], m_roots[1]);
}

void FolderCompareView::updateProgress()
{
	if (!m_job)
		return;
	const int total = m_job->totalItems();
	const int done = m_job->comparedItems();
	if (total > 0)
	{
		m_progress->setRange(0, total);
		m_progress->setValue(done);
		m_status->setText(tr("Comparing\xE2\x80\xA6 %1 of %2 items").arg(done).arg(total));
	}
}

void FolderCompareView::compareFinished()
{
	m_progressTimer->stop();
	m_progress->setVisible(false);
	m_cancelButton->setVisible(false);
	populate(m_watcher.result());
	m_job.reset();
}

void FolderCompareView::populate(const lm::FolderCompareResult &result)
{
	if (!result.ok)
	{
		m_status->setText(tr("Comparison failed: %1").arg(result.error));
		return;
	}

	m_tree->setSortingEnabled(false);
	for (const lm::FolderCompareItem &item : result.items)
	{
		auto *row = new QTreeWidgetItem(m_tree);
		row->setText(ColName, item.name);
		row->setText(ColFolder, item.folder);
		row->setText(ColLeftSize, sizeText(item.size[0]));
		row->setText(ColRightSize, sizeText(item.size[1]));
		row->setText(ColLeftDate, dateText(item.mtime[0]));
		row->setText(ColRightDate, dateText(item.mtime[1]));
		row->setTextAlignment(ColLeftSize, Qt::AlignRight | Qt::AlignVCenter);
		row->setTextAlignment(ColRightSize, Qt::AlignRight | Qt::AlignVCenter);
		setRowCategory(row, item.category, item.isDir);
		row->setData(0, RoleLeftPath, item.leftPath);
		row->setData(0, RoleRightPath, item.rightPath);
		row->setData(0, RoleIsFile, !item.isDir);
		row->setData(0, RoleBothSides,
			!item.leftPath.isEmpty() && !item.rightPath.isEmpty());
		row->setData(0, RoleFolder, item.folder);
		row->setData(0, RoleName, item.name);
	}
	m_tree->setSortingEnabled(true);
	m_tree->sortByColumn(ColFolder, Qt::AscendingOrder);
	for (int col = 0; col < ColCount; ++col)
		m_tree->resizeColumnToContents(col);

	QString text = tr("%1 item(s): %2 different, %3 unique, %4 identical")
		.arg(result.items.size()).arg(result.different).arg(result.unique)
		.arg(result.identical);
	if (result.aborted)
		text = tr("Cancelled \xE2\x80\x94 partial results. ") + text;
	m_status->setText(text);
}

QString FolderCompareView::sidePath(QTreeWidgetItem *row, int side) const
{
	return row->data(0, side == 0 ? RoleLeftPath : RoleRightPath).toString();
}

/** The path this row has (or would have) on the given side. */
QString FolderCompareView::intendedSidePath(QTreeWidgetItem *row, int side) const
{
	const QString existing = sidePath(row, side);
	if (!existing.isEmpty())
		return existing;
	QString path = m_roots[side];
	const QString folder = row->data(0, RoleFolder).toString();
	if (!folder.isEmpty())
		path += QLatin1Char('/') + folder;
	return path + QLatin1Char('/') + row->data(0, RoleName).toString();
}

void FolderCompareView::updateActions()
{
	const bool hasSelection = !m_tree->selectedItems().isEmpty();
	const bool busy = m_job != nullptr;
	bool anyLeft = false, anyRight = false;
	for (QTreeWidgetItem *row : m_tree->selectedItems())
	{
		anyLeft = anyLeft || !sidePath(row, 0).isEmpty();
		anyRight = anyRight || !sidePath(row, 1).isEmpty();
	}
	m_actCopyRight->setEnabled(hasSelection && !busy && anyLeft);
	m_actCopyLeft->setEnabled(hasSelection && !busy && anyRight);
	m_actDeleteLeft->setEnabled(hasSelection && !busy && anyLeft);
	m_actDeleteRight->setEnabled(hasSelection && !busy && anyRight);
	m_actDeleteBoth->setEnabled(hasSelection && !busy && (anyLeft || anyRight));
}

void FolderCompareView::updateRowFromDisk(QTreeWidgetItem *row)
{
	const QString paths[2] = { intendedSidePath(row, 0), intendedSidePath(row, 1) };
	const QFileInfo infos[2] = { QFileInfo(paths[0]), QFileInfo(paths[1]) };
	const bool exists[2] = { infos[0].exists(), infos[1].exists() };
	const bool isDir = (exists[0] && infos[0].isDir()) || (exists[1] && infos[1].isDir());

	row->setData(0, RoleLeftPath, exists[0] ? paths[0] : QString());
	row->setData(0, RoleRightPath, exists[1] ? paths[1] : QString());
	row->setData(0, RoleBothSides, exists[0] && exists[1]);
	row->setText(ColLeftSize, exists[0] && !isDir ? sizeText(infos[0].size()) : QString());
	row->setText(ColRightSize, exists[1] && !isDir ? sizeText(infos[1].size()) : QString());
	row->setText(ColLeftDate, exists[0] ? dateText(infos[0].lastModified()) : QString());
	row->setText(ColRightDate, exists[1] ? dateText(infos[1].lastModified()) : QString());

	if (!exists[0] && !exists[1])
	{
		delete row;
		return;
	}
	lm::FolderCompareItem::Category category;
	if (!exists[1])
		category = lm::FolderCompareItem::LeftOnly;
	else if (!exists[0])
		category = lm::FolderCompareItem::RightOnly;
	else
		category = lm::FolderCompareItem::Identical; // post-copy state
	setRowCategory(row, category, isDir);
}

void FolderCompareView::copySelected(int sourceSide)
{
	const int target = 1 - sourceSide;
	QList<QTreeWidgetItem *> rows;
	for (QTreeWidgetItem *row : m_tree->selectedItems())
	{
		if (!sidePath(row, sourceSide).isEmpty())
			rows.append(row);
	}
	if (rows.isEmpty())
		return;

	const QString direction = sourceSide == 0
		? tr("left \xE2\x86\x92 right") : tr("right \xE2\x86\x92 left");
	if (QMessageBox::question(this, tr("LibreMerge"),
		tr("Copy %n item(s) (%1)? Overwritten files go to the Trash.", nullptr,
			rows.size()).arg(direction)) != QMessageBox::Yes)
		return;

	int failures = 0;
	for (QTreeWidgetItem *row : rows)
	{
		const QString src = sidePath(row, sourceSide);
		const QString dst = intendedSidePath(row, target);
		if (lm::copyRecursively(src, dst))
			updateRowFromDisk(row);
		else
			++failures;
	}
	if (failures > 0)
		QMessageBox::warning(this, tr("LibreMerge"),
			tr("%n item(s) could not be copied.", nullptr, failures));
	updateActions();
}

void FolderCompareView::deleteSelected(bool leftSide, bool rightSide)
{
	QList<QTreeWidgetItem *> rows;
	for (QTreeWidgetItem *row : m_tree->selectedItems())
	{
		if ((leftSide && !sidePath(row, 0).isEmpty())
			|| (rightSide && !sidePath(row, 1).isEmpty()))
			rows.append(row);
	}
	if (rows.isEmpty())
		return;

	if (QMessageBox::question(this, tr("LibreMerge"),
		tr("Move %n item(s) to the Trash?", nullptr, rows.size())) != QMessageBox::Yes)
		return;

	int failures = 0;
	for (QTreeWidgetItem *row : rows)
	{
		bool ok = true;
		if (leftSide)
		{
			const QString path = sidePath(row, 0);
			if (!path.isEmpty() && !QFile::moveToTrash(path))
				ok = false;
		}
		if (rightSide)
		{
			const QString path = sidePath(row, 1);
			if (!path.isEmpty() && !QFile::moveToTrash(path))
				ok = false;
		}
		if (!ok)
			++failures;
		updateRowFromDisk(row);
	}
	if (failures > 0)
		QMessageBox::warning(this, tr("LibreMerge"),
			tr("%n item(s) could not be moved to the Trash.", nullptr, failures));
	updateActions();
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
