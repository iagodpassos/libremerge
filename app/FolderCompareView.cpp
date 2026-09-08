// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "FolderCompareView.h"
#include "FileOps.h"
#include "Icons.h"
#include "Theme.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QStyleFactory>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include "Dialogs.h"
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
	// per-side size and date columns follow, laid out by colSize()/colDate()
};

enum ItemRole
{
	RoleLeftPath = Qt::UserRole,
	RoleRightPath,
	RoleIsFile,
	RoleBothSides, ///< the item exists on every compared side
	RoleFolder,
	RoleName,
	RoleMiddlePath, ///< 3-way only
};

const QString kFilterSettingsKey = QStringLiteral("FolderCompare/Filter");
const QString kTreeSettingsKey = QStringLiteral("FolderCompare/TreeView");

QString categoryText(lm::FolderCompareItem::Category category)
{
	switch (category)
	{
	case lm::FolderCompareItem::Identical: return QObject::tr("Identical");
	case lm::FolderCompareItem::Different: return QObject::tr("Different");
	case lm::FolderCompareItem::LeftOnly: return QObject::tr("Left only");
	case lm::FolderCompareItem::MiddleOnly: return QObject::tr("Middle only");
	case lm::FolderCompareItem::RightOnly: return QObject::tr("Right only");
	case lm::FolderCompareItem::MissingLeft:
		return QObject::tr("Does not exist on the left");
	case lm::FolderCompareItem::MissingMiddle:
		return QObject::tr("Does not exist in the middle");
	case lm::FolderCompareItem::MissingRight:
		return QObject::tr("Does not exist on the right");
	case lm::FolderCompareItem::Skipped: return QObject::tr("Skipped");
	case lm::FolderCompareItem::Error: return QObject::tr("Error");
	}
	return {};
}

/** WinMerge's COMPAREFLAGS3WAY suffix on the Result column. */
QString threeWayText(lm::FolderCompareItem::ThreeWayInfo info)
{
	switch (info)
	{
	case lm::FolderCompareItem::MiddleRightIdentical:
		return QObject::tr(" (middle and right are identical)");
	case lm::FolderCompareItem::LeftRightIdentical:
		return QObject::tr(" (left and right are identical)");
	case lm::FolderCompareItem::LeftMiddleIdentical:
		return QObject::tr(" (left and middle are identical)");
	case lm::FolderCompareItem::NoInfo:
		break;
	}
	return {};
}

QColor categoryColor(lm::FolderCompareItem::Category category)
{
	if (lm::Theme::instance()->dark())
	{
		// same hues as the light pastels, tuned for light text
		// (matching the dark set in lm::diffColors())
		switch (category)
		{
		case lm::FolderCompareItem::Different:
		case lm::FolderCompareItem::MissingLeft:
		case lm::FolderCompareItem::MissingMiddle:
		case lm::FolderCompareItem::MissingRight: return QColor(105, 92, 38);
		case lm::FolderCompareItem::LeftOnly: return QColor(42, 62, 96);
		case lm::FolderCompareItem::MiddleOnly: return QColor(74, 52, 96);
		case lm::FolderCompareItem::RightOnly: return QColor(38, 82, 58);
		case lm::FolderCompareItem::Error: return QColor(110, 48, 48);
		default: return {};
		}
	}
	switch (category)
	{
	case lm::FolderCompareItem::Different:
	case lm::FolderCompareItem::MissingLeft:
	case lm::FolderCompareItem::MissingMiddle:
	case lm::FolderCompareItem::MissingRight: return QColor(255, 243, 176);
	case lm::FolderCompareItem::LeftOnly: return QColor(224, 236, 255);
	case lm::FolderCompareItem::MiddleOnly: return QColor(238, 224, 255);
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

void FolderCompareView::setRowCategory(QTreeWidgetItem *row,
	lm::FolderCompareItem::Category category,
	lm::FolderCompareItem::ThreeWayInfo threeWay, bool isDir)
{
	const QString text = categoryText(category) + threeWayText(threeWay);
	row->setText(ColResult, isDir
		? QObject::tr("Folder: %1").arg(text) : text);
	const QColor color = categoryColor(category);
	// explicit text color so the row stays readable whatever the
	// platform palette is: black on the light pastels, near-white on
	// the dark category colors
	const QColor textColor = lm::Theme::instance()->dark()
		? QColor(0xd4, 0xd4, 0xd4) : QColor(Qt::black);
	for (int col = 0; col < colCount(); ++col)
	{
		row->setBackground(col, color.isValid() ? QBrush(color) : QBrush());
		row->setForeground(col, color.isValid() ? QBrush(textColor) : QBrush());
	}
}

void FolderCompareView::fillRow(QTreeWidgetItem *row,
	const lm::FolderCompareItem &item)
{
	row->setText(ColName, item.name);
	row->setText(ColFolder, item.folder);
	bool allSides = true;
	for (int i = 0; i < m_sides; ++i)
	{
		row->setText(colSize(i), sizeText(item.size[i]));
		row->setText(colDate(i), dateText(item.mtime[i]));
		row->setTextAlignment(colSize(i), Qt::AlignRight | Qt::AlignVCenter);
		allSides = allSides && !item.path[i].isEmpty();
	}
	setRowCategory(row, item.category, item.threeWay, item.isDir);
	row->setData(0, RoleLeftPath, item.path[0]);
	if (m_sides == 3)
	{
		row->setData(0, RoleMiddlePath, item.path[1]);
		row->setData(0, RoleRightPath, item.path[2]);
	}
	else
		row->setData(0, RoleRightPath, item.path[1]);
	row->setData(0, RoleIsFile, !item.isDir);
	row->setData(0, RoleBothSides, allSides);
	row->setData(0, RoleFolder, item.folder);
	row->setData(0, RoleName, item.name);
}

FolderCompareView::FolderCompareView(QWidget *parent)
	: QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto *toolbar = new QToolBar(this);
	toolbar->setIconSize(QSize(16, 16));
	toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	toolbar->addWidget(new QLabel(tr(" Filter: "), this));
	m_filterEdit = new QLineEdit(this);
	// the macOS style paints this field natively (white, own focus ring)
	// no matter what stylesheet it carries; Fusion honors our theming
	static QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"));
	if (fusion != nullptr)
		m_filterEdit->setStyle(fusion);
	m_filterEdit->setAttribute(Qt::WA_MacShowFocusRect, false);
	m_filterEdit->setPlaceholderText(tr("*.* \xE2\x80\x94 masks (*.cpp;*.h), f:/d: regexes or expressions"));
	m_filterEdit->setMaximumWidth(340);
	m_filterEdit->setText(QSettings().value(kFilterSettingsKey, QStringLiteral("*.*")).toString());
	toolbar->addWidget(m_filterEdit);
	// F5 arrives via the main window's Merge menu
	QAction *applyAction = toolbar->addAction(lm::icon(lm::Icon::Refresh), tr("Recompare"));
	applyAction->setToolTip(tr("Recompare (F5)"));
	connect(applyAction, &QAction::triggered, this, &FolderCompareView::recompare);
	connect(m_filterEdit, &QLineEdit::returnPressed, this, &FolderCompareView::recompare);
	toolbar->addSeparator();
	m_actTreeMode = toolbar->addAction(lm::icon(lm::Icon::TreeView), tr("Tree View"));
	m_actTreeMode->setCheckable(true);
	m_actTreeMode->setToolTip(tr("Tree View"));
	m_actTreeMode->setChecked(QSettings().value(kTreeSettingsKey, true).toBool());
	connect(m_actTreeMode, &QAction::toggled, this, [this](bool on) {
		QSettings().setValue(kTreeSettingsKey, on);
		rebuildRows();
	});
	toolbar->addSeparator();
	m_actCopyRight = toolbar->addAction(lm::icon(lm::Icon::CopyRight), tr("Copy to Right"));
	m_actCopyRight->setToolTip(tr("Copy to Right"));
	connect(m_actCopyRight, &QAction::triggered, this, [this]() { copySelected(0); });
	m_actCopyLeft = toolbar->addAction(lm::icon(lm::Icon::CopyLeft), tr("Copy to Left"));
	m_actCopyLeft->setToolTip(tr("Copy to Left"));
	connect(m_actCopyLeft, &QAction::triggered, this, [this]() { copySelected(1); });
	toolbar->addSeparator();
	m_actDeleteLeft = toolbar->addAction(lm::icon(lm::Icon::DeleteLeft), tr("Delete Left"));
	m_actDeleteLeft->setToolTip(tr("Delete Left"));
	connect(m_actDeleteLeft, &QAction::triggered, this, [this]() { deleteSelected(true, false); });
	m_actDeleteRight = toolbar->addAction(lm::icon(lm::Icon::DeleteRight), tr("Delete Right"));
	m_actDeleteRight->setToolTip(tr("Delete Right"));
	connect(m_actDeleteRight, &QAction::triggered, this, [this]() { deleteSelected(false, true); });
	m_actDeleteBoth = toolbar->addAction(lm::icon(lm::Icon::DeleteBoth), tr("Delete Both"));
	m_actDeleteBoth->setToolTip(tr("Delete Both"));
	connect(m_actDeleteBoth, &QAction::triggered, this, [this]() { deleteSelected(true, true); });
	// tag with the lm::Icon so applyToolbarTheme() can re-render them
	applyAction->setData(static_cast<int>(lm::Icon::Refresh));
	m_actTreeMode->setData(static_cast<int>(lm::Icon::TreeView));
	m_actCopyRight->setData(static_cast<int>(lm::Icon::CopyRight));
	m_actCopyLeft->setData(static_cast<int>(lm::Icon::CopyLeft));
	m_actDeleteLeft->setData(static_cast<int>(lm::Icon::DeleteLeft));
	m_actDeleteRight->setData(static_cast<int>(lm::Icon::DeleteRight));
	m_actDeleteBoth->setData(static_cast<int>(lm::Icon::DeleteBoth));
	layout->addWidget(toolbar);

	m_tree = new QTreeWidget(this);
	setupColumns();
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
	// last: every widget this touches must already exist
	applyTheme();
	connect(lm::Theme::instance(), &lm::Theme::changed,
		this, &FolderCompareView::applyTheme);
}

/** Follow the application theme like the file compare does: the tree,
 *  filter field and status bar get explicit palettes (the platform
 *  palette may disagree with the chosen theme), and the rows are
 *  rebuilt so the category colors switch set. */
void FolderCompareView::applyTheme()
{
	const bool dark = lm::Theme::instance()->dark();
	QPalette pal;
	if (dark)
	{
		pal.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
		pal.setColor(QPalette::AlternateBase, QColor(0x24, 0x24, 0x24));
		pal.setColor(QPalette::Text, QColor(0xd4, 0xd4, 0xd4));
		pal.setColor(QPalette::Window, QColor(0x2a, 0x2a, 0x2a));
		pal.setColor(QPalette::WindowText, QColor(0xd4, 0xd4, 0xd4));
		pal.setColor(QPalette::PlaceholderText, QColor(0x70, 0x70, 0x70));
		pal.setColor(QPalette::Highlight, QColor(0x26, 0x4f, 0x78));
		pal.setColor(QPalette::HighlightedText, QColor(0xe6, 0xe6, 0xe6));
		pal.setColor(QPalette::Button, QColor(0x2a, 0x2a, 0x2a));
		pal.setColor(QPalette::ButtonText, QColor(0xd4, 0xd4, 0xd4));
	}
	else
	{
		pal.setColor(QPalette::Base, Qt::white);
		pal.setColor(QPalette::AlternateBase, QColor(0xf6, 0xf6, 0xf6));
		pal.setColor(QPalette::Text, Qt::black);
		pal.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
		pal.setColor(QPalette::WindowText, QColor(0x10, 0x10, 0x10));
		pal.setColor(QPalette::PlaceholderText, QColor(0x88, 0x88, 0x88));
		pal.setColor(QPalette::Highlight, QColor(0xb5, 0xd5, 0xff));
		pal.setColor(QPalette::HighlightedText, Qt::black);
		pal.setColor(QPalette::Button, QColor(0xf0, 0xf0, 0xf0));
		pal.setColor(QPalette::ButtonText, QColor(0x10, 0x10, 0x10));
	}
	setPalette(pal);
	setAutoFillBackground(true); // gaps between widgets follow the theme
	m_tree->setPalette(pal);
	// direct stylesheet: the toolbar-level selector loses to the native
	// macOS focus frame
	m_filterEdit->setStyleSheet(dark
		? QStringLiteral("QLineEdit { background: #1e1e1e; color: #d4d4d4;"
			" border: 1px solid #4a4a4a; border-radius: 4px; padding: 2px 6px; }")
		: QStringLiteral("QLineEdit { background: white; color: black;"
			" border: 1px solid #b6b6b6; border-radius: 4px; padding: 2px 6px; }"));
	m_status->setStyleSheet(dark
		? QStringLiteral("QLabel { background: #2c2c2c; color: #b8b8b8; }")
		: QStringLiteral("QLabel { background: #ececec; color: #303030; }"));
	// header row of the tree follows the view palette on all platforms
	m_tree->header()->setPalette(pal);
	lm::applyToolbarTheme(this);
	rebuildRows();
}

FolderCompareView::~FolderCompareView()
{
	if (m_job)
		m_job->requestAbort();
	m_watcher.waitForFinished();
}

/** Header labels for the current side count: 2-way keeps the classic
    left/right pairs, 3-way inserts the middle columns. */
void FolderCompareView::setupColumns()
{
	QStringList labels{ tr("Name"), tr("Folder"), tr("Comparison result") };
	if (m_sides == 3)
		labels << tr("Left size") << tr("Middle size") << tr("Right size")
			<< tr("Left date") << tr("Middle date") << tr("Right date");
	else
		labels << tr("Left size") << tr("Right size")
			<< tr("Left date") << tr("Right date");
	m_tree->setColumnCount(labels.size());
	m_tree->setHeaderLabels(labels);
}

void FolderCompareView::start(const QString &leftDir, const QString &rightDir)
{
	start(QStringList{ leftDir, rightDir });
}

void FolderCompareView::start(const QStringList &dirs)
{
	m_sides = dirs.size();
	for (int i = 0; i < 3; ++i)
		m_roots[i] = i < m_sides ? dirs.at(i) : QString();
	setupColumns();
	m_result = lm::FolderCompareResult();
	m_tree->clear();
	updateActions();

	const QString filterMask = m_filterEdit->text().trimmed();
	QSettings().setValue(kFilterSettingsKey,
		filterMask.isEmpty() ? QStringLiteral("*.*") : filterMask);

	m_job = std::make_shared<lm::FolderCompareJob>(m_sides);
	m_status->setText(tr("Scanning\xE2\x80\xA6"));
	m_progress->setRange(0, 0); // busy until totals are known
	m_progress->setVisible(true);
	m_cancelButton->setVisible(true);
	m_cancelButton->setEnabled(true);
	m_progressTimer->start();

	auto job = m_job;
	m_watcher.setFuture(QtConcurrent::run([dirs, job, filterMask]() {
		return lm::compareFolders(dirs, true, job, filterMask);
	}));
}

void FolderCompareView::recompare()
{
	if (m_job)
		return; // already comparing
	QStringList dirs;
	for (int i = 0; i < m_sides; ++i)
	{
		if (m_roots[i].isEmpty())
			return;
		dirs.append(m_roots[i]);
	}
	start(dirs);
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

	m_result = result;
	rebuildRows();
	// results are in: move focus to the list (also keeps macOS from
	// painting its native focus treatment over the themed filter field)
	m_tree->setFocus();

	QString text = tr("%1 item(s): %2 different, %3 unique, %4 identical")
		.arg(result.items.size()).arg(result.different).arg(result.unique)
		.arg(result.identical);
	if (result.aborted)
		text = tr("Cancelled \xE2\x80\x94 partial results. ") + text;
	m_status->setText(text);
}

void FolderCompareView::rebuildRows()
{
	const bool treeMode = m_actTreeMode->isChecked();
	m_tree->clear();
	m_tree->setRootIsDecorated(treeMode);
	m_tree->setColumnHidden(ColFolder, treeMode);
	if (!m_result.ok)
		return;

	m_tree->setSortingEnabled(false);
	QHash<QString, QTreeWidgetItem *> folderNodes;
	for (const lm::FolderCompareItem &item : m_result.items)
	{
		QTreeWidgetItem *row = nullptr;
		if (treeMode)
		{
			const QString relPath = item.folder.isEmpty()
				? item.name : item.folder + QLatin1Char('/') + item.name;
			if (item.isDir)
				row = folderNodes.value(relPath); // placeholder made earlier
			if (row == nullptr)
			{
				QTreeWidgetItem *parent = folderNode(item.folder, folderNodes);
				row = parent != nullptr
					? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
			}
			if (item.isDir)
				folderNodes.insert(relPath, row);
		}
		else
		{
			row = new QTreeWidgetItem(m_tree);
		}
		fillRow(row, item);
	}
	m_tree->setSortingEnabled(true);
	m_tree->sortByColumn(treeMode ? ColName : ColFolder, Qt::AscendingOrder);
	if (treeMode)
		m_tree->expandAll();
	for (int col = 0; col < colCount(); ++col)
		m_tree->resizeColumnToContents(col);
	updateActions();
}

/** Tree-mode parent node for a relative folder path (nullptr = top level),
    synthesizing plain folder rows the comparison result did not list. */
QTreeWidgetItem *FolderCompareView::folderNode(const QString &folder,
	QHash<QString, QTreeWidgetItem *> &nodes)
{
	if (folder.isEmpty())
		return nullptr;
	if (QTreeWidgetItem *existing = nodes.value(folder))
		return existing;

	const int slash = folder.lastIndexOf(QLatin1Char('/'));
	const QString parentFolder = slash < 0 ? QString() : folder.left(slash);
	const QString name = slash < 0 ? folder : folder.mid(slash + 1);
	QTreeWidgetItem *parent = folderNode(parentFolder, nodes);
	auto *row = parent != nullptr
		? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
	row->setText(ColName, name);
	row->setText(ColFolder, parentFolder);
	bool allSides = true;
	for (int i = 0; i < m_sides; ++i)
	{
		const QString path = m_roots[i] + QLatin1Char('/') + folder;
		const bool exists = QFileInfo(path).isDir();
		const int role = i == 0 ? int(RoleLeftPath)
			: (m_sides == 3 && i == 1) ? int(RoleMiddlePath) : int(RoleRightPath);
		row->setData(0, role, exists ? path : QString());
		allSides = allSides && exists;
	}
	row->setData(0, RoleIsFile, false);
	row->setData(0, RoleBothSides, allSides);
	row->setData(0, RoleFolder, parentFolder);
	row->setData(0, RoleName, name);
	nodes.insert(folder, row);
	return row;
}

QString FolderCompareView::sidePath(QTreeWidgetItem *row, int side) const
{
	const int role = side == 0 ? int(RoleLeftPath)
		: (m_sides == 3 && side == 1) ? int(RoleMiddlePath) : int(RoleRightPath);
	return row->data(0, role).toString();
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
	if (m_sides == 3)
	{
		// v1 of the 3-way folder compare is read-only: WinMerge's
		// pane-relative copy/delete matrix is not wired yet
		m_actCopyRight->setEnabled(false);
		m_actCopyLeft->setEnabled(false);
		m_actDeleteLeft->setEnabled(false);
		m_actDeleteRight->setEnabled(false);
		m_actDeleteBoth->setEnabled(false);
		return;
	}
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
	row->setText(colSize(0), exists[0] && !isDir ? sizeText(infos[0].size()) : QString());
	row->setText(colSize(1), exists[1] && !isDir ? sizeText(infos[1].size()) : QString());
	row->setText(colDate(0), exists[0] ? dateText(infos[0].lastModified()) : QString());
	row->setText(colDate(1), exists[1] ? dateText(infos[1].lastModified()) : QString());

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
	setRowCategory(row, category, lm::FolderCompareItem::NoInfo, isDir);
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
	if (lm::question(this, tr("LibreMerge"),
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
		lm::warning(this, tr("LibreMerge"),
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

	if (lm::question(this, tr("LibreMerge"),
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
		lm::warning(this, tr("LibreMerge"),
			tr("%n item(s) could not be moved to the Trash.", nullptr, failures));
	updateActions();
}

void FolderCompareView::itemActivated(QTreeWidgetItem *item, int column)
{
	Q_UNUSED(column);
	if (item == nullptr || !item->data(0, RoleIsFile).toBool())
		return;
	if (m_sides == 3)
	{
		// present on all three sides opens the 3-way comparison; on
		// exactly two, the 2-way of the pair that exists
		QStringList present;
		for (int i = 0; i < 3; ++i)
		{
			const QString path = sidePath(item, i);
			if (!path.isEmpty())
				present.append(path);
		}
		if (present.size() == 3)
			emit openFileComparison3Requested(present);
		else if (present.size() == 2)
			emit openFileComparisonRequested(present.at(0), present.at(1));
		return;
	}
	if (!item->data(0, RoleBothSides).toBool())
		return;
	emit openFileComparisonRequested(item->data(0, RoleLeftPath).toString(),
		item->data(0, RoleRightPath).toString());
}
