// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMainWindow>

class QTabWidget;
class FileCompareView;

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	explicit MainWindow(QWidget *parent = nullptr);

	void openFileComparison(const QString &leftPath, const QString &rightPath);
	void openFileComparison(const QStringList &paths,
		const QList<bool> &readOnly = {}, bool forceText = false);
	/** CSV/TSV side-by-side grid comparison. */
	void openTableComparison(const QString &leftPath, const QString &rightPath);
	/** Image comparison, 2- or 3-way (WinMerge's image compare). */
	void openImageComparison(const QStringList &paths,
		const QList<bool> &readOnly = {});
	/** Empty, editable comparison (WinMerge's File > New). */
	void openBlankComparison();
	void openFolderComparison(const QString &leftDir, const QString &rightDir);

	/** Open (or focus) the "Select Files or Folders" page, optionally
	    pre-filling dropped/opened paths. */
	void openSelector(const QStringList &paths = {});

	/** Route paths dropped on the window or opened via Finder/Dock. */
	void handleIncomingPaths(const QStringList &paths);

	/** Jump to the first difference of the current file comparison
	    (used by tests; same as pressing Next after opening). */
	void gotoFirstDifference();

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	void closeEvent(QCloseEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void newComparison();
	void showOptions();
	void showLineFilters();
	void closeTab(int index);

private:
	void attachFileView(FileCompareView *view);
	/** Record a successful comparison in File > Recent Files or Folders. */
	void rememberComparison(const QStringList &paths);
	/** Reopen a recent comparison (folders or files). */
	void reopenComparison(const QStringList &paths);

	QTabWidget *m_tabs;
};
