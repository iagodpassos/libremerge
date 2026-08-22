// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>
#include "FolderCompareDriver.h"

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * Two-way folder comparison view (v0): flat recursive list with result
 * classification. Double-clicking a file that exists on both sides asks
 * the main window to open a file comparison.
 */
class FolderCompareView : public QWidget
{
	Q_OBJECT
public:
	explicit FolderCompareView(QWidget *parent = nullptr);

	bool compare(const QString &leftDir, const QString &rightDir, QString *error);

signals:
	void openFileComparisonRequested(const QString &leftPath, const QString &rightPath);

private slots:
	void itemActivated(QTreeWidgetItem *item, int column);

private:
	QTreeWidget *m_tree;
	QLabel *m_status;
};
