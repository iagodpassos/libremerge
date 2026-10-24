// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>

#include "CommandLine.h"
#include "FileOps.h"

class QFileSystemWatcher;
class QTabWidget;
class QTimer;
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
	/** CSV/TSV side-by-side grid comparison, 2- or 3-way. */
	void openTableComparison(const QStringList &paths);
	void openTableComparison(const QString &leftPath, const QString &rightPath)
	{
		openTableComparison(QStringList{ leftPath, rightPath });
	}
	/** Image comparison, 2- or 3-way (WinMerge's image compare). */
	void openImageComparison(const QStringList &paths,
		const QList<bool> &readOnly = {});
	/** WinMerge's binary comparison (CHexMergeDoc), 2 or 3 files. */
	void openHexComparison(const QStringList &paths, const QList<bool> &readOnly = {});
	/** Empty, editable comparison (WinMerge's File > New). */
	void openBlankComparison();
	void openFolderComparison(const QString &leftDir, const QString &rightDir);
	/** Two or three folders (WinMerge's 3-way folder compare), with
	    their subfolders or not; by default as "Include subfolders" says
	    (OpenFolderParams::m_bRecurse). */
	void openFolderComparison(const QStringList &dirs,
		std::optional<bool> recursive = std::nullopt);
	/** Two or three sides, each a folder or an archive: archives are
	    extracted to temp folders and the lot opens as a folder
	    comparison (WinMerge's DecompressArchive flow). */
	void openArchiveComparison(const QStringList &sources);

	/** A folder, or an archive standing in for one (the sides a folder
	    comparison accepts). */
	static bool isFolderLike(const QString &path);
	/** 2 or 3 folder-like sources: open the folder comparison. */
	static bool allFolderLike(const QStringList &paths);

	/** Open (or focus) the "Select Files or Folders" page, optionally
	    pre-filling dropped/opened paths. */
	void openSelector(const QStringList &paths = {});
	/** One file against a snapshot of itself, WinMerge's self-compare
	    (what Compare does with a single file). */
	void openSelfComparison(const QString &path);

	/** CMergeApp::ParseArgsAndDoOpen: what the command line opens and
	    how (its paths, descriptions, read-only sides, window type,
	    focus, line...), and what it asks of the window from then on
	    (-e, -x). */
	void openFromCommandLine(const lm::CommandLine &cmd);
	/** The last comparison's result (CMergeApp::m_nLastCompareResult): 0
	    identical, more when different, -1 an error or none yet. */
	int lastCompareResult() const { return m_lastCompareResult; }

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

	/** CMainFrame::ApplyDiffOptions, run when the Options dialog is
	    accepted: every open text and table comparison is recompared with
	    the options as they are now. */
	void applyDiffOptions();
	/** WinMerge's File > Reload (OnFileReload) for the current
	    comparison: unsaved changes are offered for saving, then the
	    files are read from disk again. */
	void reloadCurrentComparison();
	/** The application was brought to the front (OnActivateApp): in the
	    "Only on window activated" mode the current comparison is checked
	    for files changed elsewhere. Public for tests. */
	void applicationActivated();
	/** The paths the "Immediately" mode watches (for tests). */
	QStringList watchedPathsForTest() const;
	/** View > Display Filter Bar, for the folder, the file or the table
	    comparison in front. Upstream keeps two commands behind this one
	    item: picking it shows the bar or closes it (the frame's
	    OnViewDisplayFilterBar), while its accelerator only ever shows the
	    bar and puts the keyboard in its field (the view's or the
	    document's). A shortcut reaches a QAction the way a click does, so
	    the keys held down tell the two apart: held is what the keyboard
	    had down when the command came. Public for tests. */
	void displayFilterBarCommand(Qt::KeyboardModifiers held);
	/** What the save prompt answers instead of opening: 0 cancels, 1
	    saves, 2 discards (for tests; an empty function shows it). */
	static void setSavePromptForTest(std::function<int()> answer);

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	void closeEvent(QCloseEvent *event) override;
	/** Esc that no page used (to close its find bar, cancel a selection
	    or a scan) closes the tab or the window, per the options. */
	void keyPressEvent(QKeyEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;
	void changeEvent(QEvent *event) override;

private slots:
	void newComparison();
	void showOptions();
	void showFilters();
	void closeTab(int index);

private:
	void handleEscape();
	/** A forced Rescan of every open text and table comparison
	    (FlushAndRescan(true) on every merge document). */
	void rescanFileComparisons();
	/** CMainFrame::OnUser1, posted: check the current comparison once
	    the stack is clean and no dialog or menu is open. */
	void scheduleFileCheck();
	/** CMergeDoc::CheckFileChanged: a file of the comparison changed
	    elsewhere since it was loaded? Ask, and reload on Yes. A check
	    made by a Recompare leaves the identical files report to it. */
	void checkFileChanged(QWidget *page, bool beforeRescan = false);
	/** OnFileReload: false when the user cancelled or a file could not
	    be read. */
	bool reloadComparison(QWidget *page, bool reportIdentical = true);
	/** PromptAndSaveIfNeeded: offer to save a comparison's unsaved
	    changes before they are dropped; false when cancelled. */
	bool promptAndSaveIfNeeded(QWidget *page, bool closing);
	/** WatchDocuments: follow the "Immediately" mode's file watches to
	    the open comparisons, and post a check when one of the watched
	    files is not as the previous call saw it. */
	void updateFileWatches();
	/** Report identical files on opening, Recompare and saving. */
	void watchIdentical(QWidget *page);
	/** Hook a new comparison up to the file change checks. */
	void watchFiles(QWidget *page);
	void reportIfIdentical(QWidget *page, bool opening);
	void attachFileView(FileCompareView *view);
	/** Record a successful comparison in File > Recent Files or Folders. */
	void rememberComparison(const QStringList &paths);
	/** Name the sides of a comparison just opened, the ones given only,
	    in its panes and its tab. */
	void describeSides(QWidget *page, const QStringList &descriptions);
	void noteCompareResult(int result);
	/** Close once no folder comparison runs (the command line's
	    -noninteractive, CMergeApp::OnIdle). */
	void closeWhenIdle();
	/** Reopen a recent comparison (folders or files). */
	void reopenComparison(const QStringList &paths);

	QTabWidget *m_tabs;
	QMenu *m_helpMenu = nullptr;
	QFileSystemWatcher *m_fileWatcher;
	QTimer *m_watchTimer;     // gathers a burst of change notifications
	QTimer *m_fileCheckTimer; // the posted check
	bool m_checkingFiles = false;
	bool m_askingToClose = false; // closeEvent is waiting for an answer
	QElapsedTimer m_lastFileQuestion; // since the reload question closed
	QHash<QString, lm::FileStamp> m_watchedStamps; // as the watcher last saw them
	// the command line's: Esc closes LibreMerge (-e), and closing when the
	// first comparison finds the files identical (-x, -xq)
	bool m_escShutdown = false;
	lm::CommandLine::ExitNoDiff m_exitIfNoDiff = lm::CommandLine::ExitNoDiff::Disabled;
	const lm::CommandLine *m_openingCommandLine = nullptr; // while it opens
	int m_lastCompareResult = -1;
};
