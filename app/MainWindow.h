// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMainWindow>

class QTabWidget;

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	explicit MainWindow(QWidget *parent = nullptr);

	void openFileComparison(const QString &leftPath, const QString &rightPath);
	void openFileComparison(const QStringList &paths);
	void openFolderComparison(const QString &leftDir, const QString &rightDir);

private slots:
	void newComparison();
	void showOptions();
	void closeTab(int index);

private:
	QTabWidget *m_tabs;
};
