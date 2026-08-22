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
	void openFileComparison(const QStringList &paths,
		const QList<bool> &readOnly = {});
	void openFolderComparison(const QString &leftDir, const QString &rightDir);

	/** Open (or focus) the "Select Files or Folders" page, optionally
	    pre-filling dropped/opened paths. */
	void openSelector(const QStringList &paths = {});

	/** Route paths dropped on the window or opened via Finder/Dock. */
	void handleIncomingPaths(const QStringList &paths);

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void newComparison();
	void showOptions();
	void closeTab(int index);

private:
	QTabWidget *m_tabs;
};
