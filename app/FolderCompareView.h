// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <QFutureWatcher>
#include <QHash>
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

public slots:
	void recompare();

signals:
	void openFileComparisonRequested(const QString &leftPath, const QString &rightPath);

private slots:
	void itemActivated(QTreeWidgetItem *item, int column);
	void updateProgress();
	void compareFinished();
	void copySelected(int sourceSide);
	void deleteSelected(bool leftSide, bool rightSide);

private:
	void populate(const lm::FolderCompareResult &result);
	void rebuildRows();
	QTreeWidgetItem *folderNode(const QString &folder,
		QHash<QString, QTreeWidgetItem *> &nodes);
	void updateRowFromDisk(QTreeWidgetItem *row);
	void updateActions();
	QString sidePath(QTreeWidgetItem *row, int side) const;
	QString intendedSidePath(QTreeWidgetItem *row, int side) const;

	QString m_roots[2];
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
