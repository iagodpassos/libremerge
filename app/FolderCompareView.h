// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <vector>
#include <QFutureWatcher>
#include <QHash>
#include <QTemporaryDir>
#include <QWidget>
#include "FolderCompareDriver.h"

class QAction;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * Two-way folder comparison view: runs the engine comparison on a worker
 * thread with live progress and cancellation, then shows the recursive
 * result as a hierarchical tree (folders as expandable nodes) or a flat
 * list. Supports file filters (engine mask/regex/expression syntax),
 * multi-selection copy between sides and delete-to-trash.
 * Double-clicking a file that exists on both sides asks the main window
 * to open a file comparison.
 */
class FolderCompareView : public QWidget
{
	Q_OBJECT
public:
	explicit FolderCompareView(QWidget *parent = nullptr);
	~FolderCompareView() override;

	/** Start the comparison asynchronously. */
	void start(const QString &leftDir, const QString &rightDir);
	/** Same, for two or three folders (3-way folder compare). */
	void start(const QStringList &dirs);

	/** Keep extracted-archive temp trees alive for this view's
	    lifetime (WinMerge's CTempPathContext). */
	void adoptTempDirs(std::vector<std::unique_ptr<QTemporaryDir>> dirs)
	{
		m_tempDirs = std::move(dirs);
	}

public slots:
	void recompare();

signals:
	void openFileComparisonRequested(const QString &leftPath, const QString &rightPath);
	/** 3-way activation: the item exists on all three sides. */
	void openFileComparison3Requested(const QStringList &paths);

private slots:
	void applyTheme();
	void itemActivated(QTreeWidgetItem *item, int column);
	void updateProgress();
	void compareFinished();
	void copySelected(int sourceSide);
	void deleteSelected(bool leftSide, bool rightSide);

private:
	void populate(const lm::FolderCompareResult &result);
	void rebuildRows();
	void setupColumns();
	int colSize(int side) const { return 3 + side; }
	int colDate(int side) const { return 3 + m_sides + side; }
	int colCount() const { return 3 + 2 * m_sides; }
	void fillRow(QTreeWidgetItem *row, const lm::FolderCompareItem &item);
	void setRowCategory(QTreeWidgetItem *row,
		lm::FolderCompareItem::Category category,
		lm::FolderCompareItem::ThreeWayInfo threeWay, bool isDir);
	QTreeWidgetItem *folderNode(const QString &folder,
		QHash<QString, QTreeWidgetItem *> &nodes);
	void updateRowFromDisk(QTreeWidgetItem *row);
	void updateActions();
	QString sidePath(QTreeWidgetItem *row, int side) const;
	QString intendedSidePath(QTreeWidgetItem *row, int side) const;

	QString m_roots[3];
	int m_sides = 2;
	std::vector<std::unique_ptr<QTemporaryDir>> m_tempDirs;
	lm::FolderCompareResult m_result;
	QLineEdit *m_filterEdit;
	QTreeWidget *m_tree;
	QLabel *m_status;
	QProgressBar *m_progress;
	QPushButton *m_cancelButton;
	QTimer *m_progressTimer;
	QAction *m_actTreeMode;
	QAction *m_actCopyRight;
	QAction *m_actCopyLeft;
	QAction *m_actDeleteLeft;
	QAction *m_actDeleteRight;
	QAction *m_actDeleteBoth;
	std::shared_ptr<lm::FolderCompareJob> m_job;
	QFutureWatcher<lm::FolderCompareResult> m_watcher;
};
