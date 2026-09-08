// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMainWindow>

class QTabWidget;
class FileCompareView;
class FolderCompareView;

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
	/** Two or three folders (WinMerge's 3-way folder compare). */
	void openFolderComparison(const QStringList &dirs);
	/** Extract two archives to temp folders and open them as a folder
	    comparison (WinMerge's DecompressArchive flow). */
	void openArchiveComparison(const QString &leftArchive,
		const QString &rightArchive);

	/** Open (or focus) the "Select Files or Folders" page, optionally
	    pre-filling dropped/opened paths. */
	void openSelector(const QStringList &paths = {});

	/** Route paths dropped on the window or opened via Finder/Dock. */
	void handleIncomingPaths(const QStringList &paths);
	/** Close the untouched blank comparison the startup opened, when a
	    comparison arriving from outside made it redundant. */
	void closeStartupPlaceholder();
	/** Connect the tab just opened from a folder comparison so saving
	    in it refreshes the matching folder row (UpdateChangedItem). */
	void wireFolderRowSync(FolderCompareView *folder);

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
