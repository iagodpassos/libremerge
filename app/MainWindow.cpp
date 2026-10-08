// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDragEnterEvent>
#include <QFileOpenEvent>
#include <QMimeData>
#include <QSettings>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include "Dialogs.h"
#include <QPushButton>
#include <QPointer>
#include <QProgressDialog>
#include <QTabWidget>
#include <QTemporaryDir>
#include <algorithm>
#include <memory>
#include <QKeyEvent>
#include <QTimer>

#include "EngineOptions.h"
#include "FileFilters.h"
#include "FiltersDialog.h"
#include "FileCompareView.h"
#include "TableCompareView.h"
#include "ImageCompareView.h"
#include "ImageFormats.h"
#include "MessageBoxes.h"
#include "OptionsDialog.h"
#include "AboutDialog.h"
#include "ArchiveCompare.h"
#include "FolderCompareView.h"
#include "NewComparisonView.h"
#include "ProjectLinks.h"
#include "StarPrompt.h"
#ifdef Q_OS_MACOS
#include "MacServices.h"
#endif

// engine
#include "OptionsMgr.h"
#include "OptionsDef.h"

namespace
{

/** Display name of a file or folder path; folders dragged in carry a
    trailing slash, which makes QFileInfo::fileName() return nothing. */
QString displayName(const QString &path)
{
	QString trimmed = path;
	while (trimmed.length() > 1 && (trimmed.endsWith(QLatin1Char('/'))
		|| trimmed.endsWith(QLatin1Char('\\'))))
		trimmed.chop(1);
	const QString name = QFileInfo(trimmed).fileName();
	return name.isEmpty() ? trimmed : name;
}

/** macOS moves menu-bar actions into the application menu by role, and
    an action without an explicit role gets one guessed from its text:
    anything starting or ending with the translated "About" becomes the
    About item, "Options"/"Settings" the Preferences item, "Quit"/"Exit"
    the Quit item. In pt-BR "Sobre" (About) also opens words like
    "Sobreposicao" (Overlay), which hijacked the About slot. Every action
    keeps its menu unless given a role on purpose. */
void disableMenuRoleHeuristics(QWidget *menuContainer)
{
	for (QAction *action : menuContainer->actions())
	{
		if (action->menuRole() == QAction::TextHeuristicRole)
			action->setMenuRole(QAction::NoRole);
		if (QMenu *submenu = action->menu())
			disableMenuRoleHeuristics(submenu);
	}
}

/** The files a file, table or image comparison shows (none for the
    other pages). */
QStringList comparedPaths(QWidget *page)
{
	if (auto *file = qobject_cast<FileCompareView *>(page))
		return file->paths();
	if (auto *table = qobject_cast<TableCompareView *>(page))
		return table->paths();
	if (auto *image = qobject_cast<ImageCompareView *>(page))
		return image->paths();
	return {};
}

/** Whether a file, table or image comparison holds unsaved changes. */
bool hasUnsavedChanges(QWidget *page)
{
	if (auto *file = qobject_cast<FileCompareView *>(page))
		return file->isModified();
	if (auto *table = qobject_cast<TableCompareView *>(page))
		return table->isModified();
	if (auto *image = qobject_cast<ImageCompareView *>(page))
		return image->isModified();
	return false;
}

std::function<int()> &savePromptForTest()
{
	static std::function<int()> answer;
	return answer;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
{
	setWindowTitle(QStringLiteral("LibreMerge"));
	resize(1100, 700);
	setAcceptDrops(true);
	qApp->installEventFilter(this); // QFileOpenEvent from Finder/Dock

	m_tabs = new QTabWidget(this);
	m_tabs->setTabsClosable(true);
	m_tabs->setDocumentMode(true);
	connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
	setCentralWidget(m_tabs);

	// WinMerge's checks for files another application changed
	// (CMainFrame::OnUser1). A comparison that becomes the current tab is
	// always checked (OnMDIActivate); OPT_AUTO_RELOAD_MODIFIED_FILES adds
	// the application coming to the front, or every change as the
	// watcher reports it
	m_fileCheckTimer = new QTimer(this);
	m_fileCheckTimer->setSingleShot(true);
	connect(m_fileCheckTimer, &QTimer::timeout, this, [this]() {
		// never on top of another dialog or an open menu: wait for it
		if (m_checkingFiles || QApplication::activeModalWidget() != nullptr
			|| QApplication::activePopupWidget() != nullptr)
		{
			m_fileCheckTimer->start(250);
			return;
		}
		checkFileChanged(m_tabs->currentWidget());
	});
	connect(m_tabs, &QTabWidget::currentChanged, this,
		[this]() { scheduleFileCheck(); });
	connect(qApp, &QGuiApplication::applicationStateChanged, this,
		[this](Qt::ApplicationState state) {
			// the question itself coming and going can look like the
			// application being left and returned to; counting that
			// would ask again in a loop after a No
			const bool ownDialog = m_checkingFiles
				|| (m_lastFileQuestion.isValid() && m_lastFileQuestion.elapsed() < 1000);
			if (state == Qt::ApplicationActive && !ownDialog)
				applicationActivated();
		});
	m_fileWatcher = new QFileSystemWatcher(this);
	m_watchTimer = new QTimer(this);
	m_watchTimer->setSingleShot(true);
	m_watchTimer->setInterval(300); // one save comes as several notifications
	connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, m_watchTimer,
		qOverload<>(&QTimer::start));
	connect(m_fileWatcher, &QFileSystemWatcher::directoryChanged, m_watchTimer,
		qOverload<>(&QTimer::start));
	connect(m_watchTimer, &QTimer::timeout, this,
		[this]() { updateFileWatches(); });

	// shortcuts live here (window scope) and route to the current tab
	auto fileView = [this]() {
		return qobject_cast<FileCompareView *>(m_tabs->currentWidget());
	};
	auto tableView = [this]() {
		return qobject_cast<TableCompareView *>(m_tabs->currentWidget());
	};
	auto imageView = [this]() {
		return qobject_cast<ImageCompareView *>(m_tabs->currentWidget());
	};
	auto addMenuAction = [this](QMenu *menu, const QString &text,
		const QKeySequence &shortcut, auto slot) -> QAction * {
		QAction *action = menu->addAction(text);
		if (!shortcut.isEmpty())
			action->setShortcut(shortcut);
		connect(action, &QAction::triggered, this, slot);
		return action;
	};

	QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
	// like WinMerge: New opens an empty text comparison to paste into,
	// Open brings up the file/folder selector
	addMenuAction(fileMenu, tr("&New"), QKeySequence::New,
		[this]() { openBlankComparison(); });
	addMenuAction(fileMenu, tr("&Open..."), QKeySequence::Open,
		[this]() { newComparison(); });
	// WinMerge's File > Recent Files or Folders: each entry is a whole
	// comparison, rebuilt from settings every time the menu opens
	QMenu *recentMenu = fileMenu->addMenu(tr("Recent Files or Folders"));
	connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() {
		recentMenu->clear();
		const QStringList entries = QSettings()
			.value(QStringLiteral("RecentComparisons/List")).toStringList();
		int number = 1;
		for (const QString &entry : entries)
		{
			const QStringList paths = entry.split(QChar('\n'));
			QStringList names;
			for (const QString &path : paths)
				names.append(displayName(path));
			// numbered mnemonics and the 128-character cap, like
			// WinMerge's "&1 title" MRU entries
			QAction *action = recentMenu->addAction(
				QStringLiteral("&%1 %2").arg(number++).arg(
					names.join(QString::fromUtf8(" \xE2\x86\x94 "))
						.left(128)));
			action->setToolTip(paths.join(QStringLiteral("\n")));
			connect(action, &QAction::triggered, this,
				[this, paths]() { reopenComparison(paths); });
		}
		if (entries.isEmpty())
		{
			QAction *empty = recentMenu->addAction(tr("(empty)"));
			empty->setEnabled(false);
		}
		// like WinMerge's "Clear all recent items": the menu and the
		// path fields' history, which the recent-list completion offers
		if (!entries.isEmpty() || !NewComparisonView::savedHistory().isEmpty())
		{
			recentMenu->addSeparator();
			recentMenu->addAction(tr("Clear Menu"), this, [this]() {
				QSettings().remove(QStringLiteral("RecentComparisons/List"));
				NewComparisonView::clearSavedHistory();
				for (int i = 0; i < m_tabs->count(); ++i)
					if (auto *selector = qobject_cast<NewComparisonView *>(m_tabs->widget(i)))
						selector->reloadHistory();
			});
		}
		// entries carry file names: a folder called "sobre" or "options"
		// must not be promoted into the application menu
		disableMenuRoleHeuristics(recentMenu);
	});
	fileMenu->addSeparator();
	addMenuAction(fileMenu, tr("&Save"), QKeySequence::Save,
		[fileView, tableView, imageView]() {
			QString error;
			if (auto *view = fileView())
				view->saveModified(&error);
			else if (auto *table = tableView())
				table->saveModified(&error);
			else if (auto *image = imageView())
				image->saveModified(&error);
		});
	// WinMerge's File > Reload (Ctrl+F5). With the Command key that is
	// macOS's VoiceOver switch, so Reload takes the platform's own Cmd+R
#ifdef Q_OS_MACOS
	const QKeySequence reloadShortcut(Qt::CTRL | Qt::Key_R);
#else
	const QKeySequence reloadShortcut(Qt::CTRL | Qt::Key_F5);
#endif
	addMenuAction(fileMenu, tr("Reloa&d"), reloadShortcut,
		[this]() { reloadCurrentComparison(); });
	addMenuAction(fileMenu, tr("&Close Tab"), QKeySequence::Close,
		[this]() { closeTab(m_tabs->currentIndex()); });
	fileMenu->addSeparator();
	QAction *quitAction = addMenuAction(fileMenu, tr("&Quit"), QKeySequence::Quit,
		[]() { QApplication::quit(); });
	quitAction->setMenuRole(QAction::QuitRole);

	QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
	addMenuAction(editMenu, tr("&Undo"), QKeySequence::Undo,
		[fileView, tableView, imageView]() {
			if (auto *view = fileView())
				view->undoActive();
			else if (auto *table = tableView())
				table->undo();
			else if (auto *image = imageView())
				image->undo();
		});
	addMenuAction(editMenu, tr("&Redo"), QKeySequence::Redo,
		[fileView, tableView, imageView]() {
			if (auto *view = fileView())
				view->redoActive();
			else if (auto *table = tableView())
				table->redo();
			else if (auto *image = imageView())
				image->redo();
		});
	editMenu->addSeparator();
	// WinMerge's Edit > Find... (Ctrl+F) and Replace... (Ctrl+H), the
	// dialogs of the active pane. On the Mac, Command+H hides the
	// application: Replace takes Option+Command+F there, the Mac's own
	QAction *findAction = addMenuAction(editMenu, tr("F&ind..."), QKeySequence::Find,
		[fileView]() {
			if (auto *view = fileView())
				view->showFind();
		});
	findAction->setObjectName(QStringLiteral("editFind"));
#ifdef Q_OS_MACOS
	const QKeySequence replaceShortcut(Qt::CTRL | Qt::ALT | Qt::Key_F);
#else
	const QKeySequence replaceShortcut(Qt::CTRL | Qt::Key_H);
#endif
	QAction *replaceAction = addMenuAction(editMenu, tr("Repla&ce..."), replaceShortcut,
		[fileView]() {
			if (auto *view = fileView(); view != nullptr && view->activePaneEditable())
				view->showReplace();
		});
	replaceAction->setObjectName(QStringLiteral("editReplace"));
	// greyed out over a read-only pane (OnUpdateEditReplace) while the menu
	// shows; its shortcut does nothing there either
	connect(editMenu, &QMenu::aboutToShow, this, [fileView, replaceAction]() {
		const FileCompareView *view = fileView();
		replaceAction->setEnabled(view == nullptr || view->activePaneEditable());
	});
	connect(editMenu, &QMenu::aboutToHide, this,
		[replaceAction]() { replaceAction->setEnabled(true); });
	// Edit > Marker... (Ctrl+Shift+M): the markers of every pane
	QAction *markAction = addMenuAction(editMenu, tr("&Marker..."),
		QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), [fileView]() {
			if (auto *view = fileView())
				view->showMarker();
		});
	markAction->setObjectName(QStringLiteral("editMark"));
	// F3 is upstream's: no menu item, the last search again; Shift goes the
	// other way, Ctrl takes the selection or the word at the cursor
	// (ID_EDIT_REPEAT). The Mac also has its Command+G
	const auto addRepeat = [this, fileView](const char *name, bool control, bool shift,
		QList<QKeySequence> keys) {
		auto *action = new QAction(this);
		action->setObjectName(QLatin1String(name));
		action->setShortcuts(keys);
		connect(action, &QAction::triggered, this, [fileView, control, shift]() {
			if (auto *view = fileView())
				view->findRepeat(control, shift);
		});
		addAction(action);
	};
	QList<QKeySequence> findNextKeys{ QKeySequence(Qt::Key_F3) };
	QList<QKeySequence> findPreviousKeys{ QKeySequence(Qt::SHIFT | Qt::Key_F3) };
#ifdef Q_OS_MACOS
	findNextKeys += QKeySequence::keyBindings(QKeySequence::FindNext);
	findPreviousKeys += QKeySequence::keyBindings(QKeySequence::FindPrevious);
#endif
	addRepeat("editFindNext", false, false, findNextKeys);
	addRepeat("editFindPrevious", false, true, findPreviousKeys);
	addRepeat("editFindSelectedNext", true, false, { QKeySequence(Qt::CTRL | Qt::Key_F3) });
	addRepeat("editFindSelectedPrevious", true, true,
		{ QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F3) });
	editMenu->addSeparator();
	// WinMerge's Edit > Go to... (Ctrl+G); on the Mac with Control, as
	// Command+G finds the next match there
	QAction *gotoAction = addMenuAction(editMenu, tr("&Go to..."),
		FileCompareView::goToShortcut(), [fileView]() {
			if (auto *view = fileView())
				view->showGoTo();
		});
	gotoAction->setObjectName(QStringLiteral("editGoto"));
	editMenu->addSeparator();
#ifdef Q_OS_MACOS
	// relocated to the application menu (LibreMerge > Settings...)
	QAction *optionsAction = editMenu->addAction(tr("Settings..."));
#else
	// like WinMerge: Edit > Options...
	QAction *optionsAction = editMenu->addAction(tr("&Options..."));
#endif
	optionsAction->setMenuRole(QAction::PreferencesRole);
	connect(optionsAction, &QAction::triggered, this, &MainWindow::showOptions);

	QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
	// WinMerge's folder window View menu: which rows the list shows, per
	// folder comparison (CDirView::OnOptionsShow*)
	auto folderView = [this]() {
		return qobject_cast<FolderCompareView *>(m_tabs->currentWidget());
	};
	QList<QAction *> showActions;
	auto addShowAction = [this, folderView, &showActions](QMenu *menu,
		FolderCompareView::ShowFilter filter, const QString &text) {
		QAction *action = menu->addAction(text);
		action->setCheckable(true);
		action->setData(static_cast<int>(filter));
		connect(action, &QAction::triggered, this, [folderView, filter](bool on) {
			if (auto *folder = folderView())
				folder->setShowFilter(filter, on);
		});
		showActions.append(action);
	};
	addShowAction(viewMenu, FolderCompareView::ShowIdentical,
		tr("Show &Identical Items"));
	addShowAction(viewMenu, FolderCompareView::ShowDifferent,
		tr("Show &Different Items"));
	addShowAction(viewMenu, FolderCompareView::ShowUniqueLeft,
		tr("Show L&eft Unique Items"));
	addShowAction(viewMenu, FolderCompareView::ShowUniqueMiddle,
		tr("Show Midd&le Unique Items"));
	addShowAction(viewMenu, FolderCompareView::ShowUniqueRight,
		tr("Show Ri&ght Unique Items"));
	addShowAction(viewMenu, FolderCompareView::ShowSkipped,
		tr("Show S&kipped Items"));
	addShowAction(viewMenu, FolderCompareView::ShowBinaries,
		tr("S&how Binary Files"));
	QMenu *threeWayMenu = viewMenu->addMenu(tr("&3-way Compare"));
	addShowAction(threeWayMenu, FolderCompareView::ShowDifferentLeftOnly,
		tr("Show &Left Only Different Items"));
	addShowAction(threeWayMenu, FolderCompareView::ShowDifferentMiddleOnly,
		tr("Show &Middle Only Different Items"));
	addShowAction(threeWayMenu, FolderCompareView::ShowDifferentRightOnly,
		tr("Show &Right Only Different Items"));
	threeWayMenu->addSeparator();
	addShowAction(threeWayMenu, FolderCompareView::ShowMissingLeftOnly,
		tr("Show L&eft Only Missing Items"));
	addShowAction(threeWayMenu, FolderCompareView::ShowMissingMiddleOnly,
		tr("Show Mi&ddle Only Missing Items"));
	addShowAction(threeWayMenu, FolderCompareView::ShowMissingRightOnly,
		tr("Show Rig&ht Only Missing Items"));
	// the checks follow the active folder comparison; the middle and
	// 3-way items need three folders (OnUpdateOptionsShow*)
	connect(viewMenu, &QMenu::aboutToShow, this,
		[folderView, showActions, threeWayMenu]() {
			FolderCompareView *folder = folderView();
			const bool threeWay = folder != nullptr && folder->sideCount() == 3;
			threeWayMenu->setEnabled(threeWay);
			for (QAction *action : showActions)
			{
				const auto filter = static_cast<FolderCompareView::ShowFilter>(
					action->data().toInt());
				action->setEnabled(folder != nullptr
					&& (filter != FolderCompareView::ShowUniqueMiddle || threeWay));
				action->setChecked(folder != nullptr ? folder->showFilter(filter)
					: FolderCompareView::savedShowFilter(filter));
			}
		});
	viewMenu->addSeparator();
	// WinMerge's display filter bar, of the folder window and of the file
	// window, its table too (see displayFilterBarCommand for the two
	// things the item does)
	QAction *filterBarAction = viewMenu->addAction(tr("Displa&y Filter Bar"));
	filterBarAction->setObjectName(QStringLiteral("displayFilterBarAction"));
	filterBarAction->setCheckable(true);
	filterBarAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
	const auto syncFilterBarAction = [folderView, fileView, tableView, filterBarAction]() {
		const FolderCompareView *folder = folderView();
		const FileCompareView *file = fileView();
		const TableCompareView *table = tableView();
		filterBarAction->setEnabled(folder != nullptr || file != nullptr || table != nullptr);
		filterBarAction->setChecked(folder != nullptr ? folder->displayFilterBarShown()
			: file != nullptr ? file->displayFilterBarShown()
			: table != nullptr && table->displayFilterBarShown());
	};
	connect(filterBarAction, &QAction::triggered, this, [this, syncFilterBarAction]() {
		displayFilterBarCommand(QGuiApplication::queryKeyboardModifiers());
		// the tick follows the bar, not the click
		syncFilterBarAction();
	});
	connect(viewMenu, &QMenu::aboutToShow, this, syncFilterBarAction);
	// the shortcut works only while the action is enabled: it follows the
	// tab in front, not just the menu being opened
	connect(m_tabs, &QTabWidget::currentChanged, this, syncFilterBarAction);
	syncFilterBarAction();
	viewMenu->addSeparator();
	QAction *zoomInAction = addMenuAction(viewMenu, tr("Zoom &In"),
		QKeySequence::ZoomIn, [fileView, imageView]() {
			if (auto *view = fileView()) view->zoomIn();
			else if (auto *image = imageView()) image->zoomIn();
		});
	// pt-BR and US keyboards type "+" as Shift+= — accept Cmd+= too
	zoomInAction->setShortcuts({ QKeySequence::ZoomIn,
		QKeySequence(Qt::CTRL | Qt::Key_Equal) });
	addMenuAction(viewMenu, tr("Zoom &Out"), QKeySequence::ZoomOut,
		[fileView, imageView]() {
			if (auto *view = fileView()) view->zoomOut();
			else if (auto *image = imageView()) image->zoomOut();
		});
	addMenuAction(viewMenu, tr("&Actual Size"),
		QKeySequence(Qt::CTRL | Qt::Key_0), [fileView, imageView]() {
			if (auto *view = fileView()) view->zoomReset();
			else if (auto *image = imageView()) image->zoomReset();
		});
	viewMenu->addSeparator();
	addMenuAction(viewMenu, tr("Next &Pane"), QKeySequence(Qt::Key_F6),
		[fileView, tableView, imageView]() {
			if (auto *view = fileView()) view->focusNextPane();
			else if (auto *table = tableView()) table->focusNextPane();
			else if (auto *image = imageView()) image->focusNextPane();
		});

	QMenu *mergeMenu = menuBar()->addMenu(tr("&Merge"));
	addMenuAction(mergeMenu, tr("&First Difference"),
		QKeySequence(Qt::ALT | Qt::Key_Home), [fileView, tableView, imageView]() {
			if (auto *view = fileView()) view->gotoFirstDiff();
			else if (auto *table = tableView()) table->gotoFirstDiff();
			else if (auto *image = imageView()) image->gotoFirstDiff();
		});
	QAction *prevDiffAction = addMenuAction(mergeMenu, tr("&Previous Difference"),
		QKeySequence(Qt::ALT | Qt::Key_Up), [fileView, tableView, imageView]() {
			if (auto *view = fileView()) view->gotoPrevDiff();
			else if (auto *table = tableView()) table->gotoPrevDiff();
			else if (auto *image = imageView()) image->gotoPrevDiff();
		});
	// F7/F8 are upstream aliases for previous/next
	prevDiffAction->setShortcuts({ QKeySequence(Qt::ALT | Qt::Key_Up),
		QKeySequence(Qt::Key_F7) });
	QAction *nextDiffAction = addMenuAction(mergeMenu, tr("&Next Difference"),
		QKeySequence(Qt::ALT | Qt::Key_Down), [fileView, tableView, imageView]() {
			if (auto *view = fileView()) view->gotoNextDiff();
			else if (auto *table = tableView()) table->gotoNextDiff();
			else if (auto *image = imageView()) image->gotoNextDiff();
		});
	nextDiffAction->setShortcuts({ QKeySequence(Qt::ALT | Qt::Key_Down),
		QKeySequence(Qt::Key_F8) });
	addMenuAction(mergeMenu, tr("&Last Difference"),
		QKeySequence(Qt::ALT | Qt::Key_End), [fileView, tableView, imageView]() {
			if (auto *view = fileView()) view->gotoLastDiff();
			else if (auto *table = tableView()) table->gotoLastDiff();
			else if (auto *image = imageView()) image->gotoLastDiff();
		});
	addMenuAction(mergeMenu, tr("C&urrent Difference"),
		QKeySequence(Qt::ALT | Qt::Key_Return), [fileView, tableView, imageView]() {
			if (auto *view = fileView()) view->selectDiffAtCursor();
			else if (auto *table = tableView()) table->selectDiffAtCursor();
			else if (auto *image = imageView()) image->selectDiffAtCursor();
		});
	mergeMenu->addSeparator();
	// the file/image copy commands are pane-relative like WinMerge's
	// MenuIDtoXY: from the active pane toward its neighbor, so in 3-way
	// the middle pane can push into either side
	addMenuAction(mergeMenu, tr("Copy to &Right"),
		QKeySequence(Qt::ALT | Qt::Key_Right), [fileView, tableView, imageView]() {
			if (auto *view = fileView()) view->copyToRight();
			else if (auto *table = tableView()) table->copyCurrentDiff(0);
			else if (auto *image = imageView()) image->copyToRight();
		});
	addMenuAction(mergeMenu, tr("Copy to &Left"),
		QKeySequence(Qt::ALT | Qt::Key_Left), [fileView, tableView, imageView]() {
			if (auto *view = fileView()) view->copyToLeft();
			else if (auto *table = tableView())
				table->copyCurrentDiff(1);
			else if (auto *image = imageView())
				image->copyToLeft();
		});
	// WinMerge's Ctrl+Alt variants copy and jump to the next difference
	// (the table copy always lands on the difference below the merge)
	addMenuAction(mergeMenu, tr("Copy to Right and Ad&vance"),
		QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right),
		[fileView, tableView]() {
			if (auto *view = fileView()) view->copyToRight(true);
			else if (auto *table = tableView()) table->copyCurrentDiff(0);
		});
	addMenuAction(mergeMenu, tr("Copy to Left and Advanc&e"),
		QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left),
		[fileView, tableView]() {
			if (auto *view = fileView()) view->copyToLeft(true);
			else if (auto *table = tableView())
				table->copyCurrentDiff(1);
		});
	addMenuAction(mergeMenu, tr("Copy All to Righ&t"), QKeySequence(),
		[fileView, tableView, imageView]() {
			if (auto *view = fileView()) view->copyAllToRight();
			else if (auto *table = tableView()) table->copyAllFrom(0);
			else if (auto *image = imageView()) image->copyAllToRight();
		});
	addMenuAction(mergeMenu, tr("Copy All to Le&ft"), QKeySequence(),
		[fileView, tableView, imageView]() {
			if (auto *view = fileView()) view->copyAllToLeft();
			else if (auto *table = tableView())
				table->copyAllFrom(1);
			else if (auto *image = imageView())
				image->copyAllToLeft();
		});
	mergeMenu->addSeparator();
	addMenuAction(mergeMenu, tr("S&wap Panes"), QKeySequence(),
		[fileView, tableView]() {
			if (auto *view = fileView()) view->swapSides();
			else if (auto *table = tableView()) table->swapSides();
		});
	addMenuAction(mergeMenu, tr("Re&compare"), QKeySequence(Qt::Key_F5),
		[this, fileView, tableView, imageView]() {
			if (auto *view = fileView())
				view->refreshByUser();
			else if (auto *table = tableView())
				table->refreshByUser();
			else if (auto *image = imageView())
				image->refreshByUser();
			else if (auto *folder = qobject_cast<FolderCompareView *>(
					m_tabs->currentWidget()))
				folder->recompare();
		});

	// WinMerge's Image menu; active while an image comparison is current
	QMenu *imageMenu = menuBar()->addMenu(tr("&Image"));
	QAction *actViewDiffs = imageMenu->addAction(tr("View &Differences"));
	actViewDiffs->setCheckable(true);
	connect(actViewDiffs, &QAction::triggered, this, [imageView](bool on) {
		if (auto *image = imageView()) image->setShowDifferences(on);
	});
	QMenu *blockSizeMenu = imageMenu->addMenu(tr("Diff &Block Size"));
	auto *blockSizeGroup = new QActionGroup(this);
	for (const int size : { 1, 2, 4, 8, 16, 32 })
	{
		QAction *action = blockSizeMenu->addAction(QString::number(size));
		action->setCheckable(true);
		action->setData(size);
		blockSizeGroup->addAction(action);
		connect(action, &QAction::triggered, this, [imageView, size]() {
			if (auto *image = imageView()) image->setBlockSize(size);
		});
	}
	QMenu *thresholdMenu = imageMenu->addMenu(tr("&Ignore Color Difference"));
	auto *thresholdGroup = new QActionGroup(this);
	for (const int threshold : { 0, 2, 4, 8, 16, 32, 64 })
	{
		QAction *action = thresholdMenu->addAction(QString::number(threshold));
		action->setCheckable(true);
		action->setData(threshold);
		thresholdGroup->addAction(action);
		connect(action, &QAction::triggered, this, [imageView, threshold]() {
			if (auto *image = imageView())
				image->setColorDistanceThreshold(threshold);
		});
	}
	QMenu *insDelMenu = imageMenu->addMenu(tr("Ins&ertion/Deletion Detection"));
	auto *insDelGroup = new QActionGroup(this);
	const QStringList insDelNames = { tr("None"), tr("Vertical"),
		tr("Horizontal") };
	for (int mode = 0; mode < insDelNames.size(); ++mode)
	{
		QAction *action = insDelMenu->addAction(insDelNames.at(mode));
		action->setCheckable(true);
		action->setData(mode);
		insDelGroup->addAction(action);
		connect(action, &QAction::triggered, this, [imageView, mode]() {
			if (auto *image = imageView())
				image->setInsertionDeletionMode(mode);
		});
	}
	QMenu *overlayMenu = imageMenu->addMenu(tr("&Overlay"));
	auto *overlayGroup = new QActionGroup(this);
	const QStringList overlayNames = { tr("None"), tr("XOR"),
		tr("Alpha Blend"), tr("Alpha Blend Animation") };
	for (int mode = 0; mode < overlayNames.size(); ++mode)
	{
		QAction *action = overlayMenu->addAction(overlayNames.at(mode));
		action->setCheckable(true);
		action->setData(mode);
		overlayGroup->addAction(action);
		connect(action, &QAction::triggered, this, [imageView, mode]() {
			if (auto *image = imageView()) image->setOverlayMode(mode);
		});
	}
	QMenu *draggingMenu = imageMenu->addMenu(tr("Dragging &Mode"));
	auto *draggingGroup = new QActionGroup(this);
	const struct { int mode; QString name; } draggingModes[] = {
		{ 0, tr("None") }, { 1, tr("Move") }, { 2, tr("Adjust Offset") },
		{ 3, tr("Vertical Wipe") }, { 4, tr("Horizontal Wipe") },
		{ 5, tr("Rectangle Select") },
	};
	for (const auto &m : draggingModes)
	{
		QAction *action = draggingMenu->addAction(m.name);
		action->setCheckable(true);
		action->setData(m.mode);
		draggingGroup->addAction(action);
		connect(action, &QAction::triggered, this, [imageView, mode = m.mode]() {
			if (auto *image = imageView()) image->setDraggingMode(mode);
		});
	}
	imageMenu->addSeparator();
	QAction *actPrevPage = imageMenu->addAction(tr("&Previous Page"));
	connect(actPrevPage, &QAction::triggered, this, [imageView]() {
		if (auto *image = imageView()) image->prevPage();
	});
	QAction *actNextPage = imageMenu->addAction(tr("&Next Page"));
	connect(actNextPage, &QAction::triggered, this, [imageView]() {
		if (auto *image = imageView()) image->nextPage();
	});
	QMenu *activePaneMenu = imageMenu->addMenu(tr("&Active Pane"));
	activePaneMenu->addAction(tr("Rotate &Right 90\xC2\xB0"), this, [imageView]() {
		if (auto *image = imageView()) image->rotateActivePane(1);
	});
	activePaneMenu->addAction(tr("Rotate &Left 90\xC2\xB0"), this, [imageView]() {
		if (auto *image = imageView()) image->rotateActivePane(-1);
	});
	activePaneMenu->addAction(tr("Flip V&ertically"), this, [imageView]() {
		if (auto *image = imageView()) image->flipActivePaneVertical();
	});
	activePaneMenu->addAction(tr("Flip H&orizontally"), this, [imageView]() {
		if (auto *image = imageView()) image->flipActivePaneHorizontal();
	});
	activePaneMenu->addAction(tr("&Previous Page"), this, [imageView]() {
		if (auto *image = imageView()) image->prevPageActivePane();
	});
	activePaneMenu->addAction(tr("&Next Page"), this, [imageView]() {
		if (auto *image = imageView()) image->nextPageActivePane();
	});
	connect(imageMenu, &QMenu::aboutToShow, this, [=]() {
		ImageCompareView *image = imageView();
		const bool enabled = image != nullptr;
		for (QAction *action : imageMenu->actions())
			action->setEnabled(enabled);
		if (image == nullptr)
			return;
		actViewDiffs->setChecked(image->showDifferences());
		for (QAction *action : blockSizeGroup->actions())
			action->setChecked(action->data().toInt() == image->blockSize());
		for (QAction *action : thresholdGroup->actions())
			action->setChecked(action->data().toInt()
				== static_cast<int>(image->colorDistanceThreshold()));
		for (QAction *action : insDelGroup->actions())
			action->setChecked(action->data().toInt()
				== image->insertionDeletionMode());
		for (QAction *action : overlayGroup->actions())
			action->setChecked(action->data().toInt() == image->overlayMode());
		for (QAction *action : draggingGroup->actions())
			action->setChecked(action->data().toInt() == image->draggingMode());
		actPrevPage->setEnabled(image->maxPageCount() > 1);
		actNextPage->setEnabled(image->maxPageCount() > 1);
	});

	QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
	addMenuAction(toolsMenu, tr("&Filters..."), QKeySequence(),
		[this]() { showFilters(); });

	QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
	m_helpMenu = helpMenu;
	// the project's pages, opened in the browser: LibreMerge's own items
	// (WinMerge's Help menu has links of its kind, Release Notes and
	// Translations, to pages of its site)
	addMenuAction(helpMenu, tr("LibreMerge on &GitHub"), QKeySequence(),
		[]() { lm::openProjectPage(lm::projectPage()); })
		->setObjectName(QStringLiteral("helpProjectPage"));
	addMenuAction(helpMenu, tr("&Report a Problem"), QKeySequence(),
		[]() { lm::openProjectPage(lm::projectIssuesPage()); })
		->setObjectName(QStringLiteral("helpReportProblem"));
	addMenuAction(helpMenu, tr("Help &Translate"), QKeySequence(),
		[]() { lm::openProjectPage(lm::projectTranslationsPage()); })
		->setObjectName(QStringLiteral("helpTranslate"));
	helpMenu->addSeparator();
	QAction *aboutAction = helpMenu->addAction(tr("&About LibreMerge"));
	aboutAction->setMenuRole(QAction::AboutRole);
	connect(aboutAction, &QAction::triggered, this, [this]() {
		AboutDialog dialog(this);
		dialog.exec();
	});

	disableMenuRoleHeuristics(menuBar());
#ifdef Q_OS_MACOS
	// macOS tops the Help menu with its search field over every menu
	lm::setHelpMenu(m_helpMenu);
#endif
}

/** On macOS the window in front lends the application its menus: its Help
    menu is the one macOS searches from. */
void MainWindow::changeEvent(QEvent *event)
{
	QMainWindow::changeEvent(event);
#ifdef Q_OS_MACOS
	if (event->type() == QEvent::ActivationChange && isActiveWindow())
		lm::setHelpMenu(m_helpMenu);
#endif
}

bool MainWindow::isFolderLike(const QString &path)
{
	const QFileInfo info(path);
	return info.isDir() || (info.isFile() && lm::isArchivePath(path));
}

bool MainWindow::allFolderLike(const QStringList &paths)
{
	if (paths.size() != 2 && paths.size() != 3)
		return false;
	for (const QString &path : paths)
		if (!isFolderLike(path))
			return false;
	return true;
}

void MainWindow::openFileComparison(const QString &leftPath, const QString &rightPath)
{
	openFileComparison(QStringList{ leftPath, rightPath });
}

void MainWindow::openFileComparison(const QStringList &paths, const QList<bool> &readOnly,
	bool forceText)
{
	// archives (with each other or with folders) extract to temp folders
	// and open as a folder comparison, like WinMerge's DecompressArchive
	if (!forceText && allFolderLike(paths))
	{
		openFolderComparison(paths);
		return;
	}

	// image pairs/triples open as an image comparison (WinMerge's image
	// file patterns decide, checked before the table patterns like
	// upstream; 3-way image compare is supported)
	if (!forceText && (paths.size() == 2 || paths.size() == 3))
	{
		bool allImages = true;
		for (const QString &path : paths)
			allImages = allImages && lm::isImageFile(path);
		if (allImages)
		{
			openImageComparison(paths, readOnly);
			return;
		}
	}

	// CSV/TSV pairs open as side-by-side grids, like WinMerge's table
	// compare; "Open as Text" in the table view forces the text path
	if (!forceText && paths.size() == 2)
	{
		const QStringList tableExts = { QStringLiteral("csv"),
			QStringLiteral("tsv") };
		if (tableExts.contains(QFileInfo(paths.at(0)).suffix().toLower())
			&& tableExts.contains(QFileInfo(paths.at(1)).suffix().toLower()))
		{
			openTableComparison(paths.at(0), paths.at(1));
			return;
		}
	}

	auto *view = new FileCompareView(this);
	view->setReadOnlySides(readOnly);
	QString error;
	if (!view->compare(paths, &error))
	{
		delete view;
		lm::warning(this, tr("LibreMerge"),
			tr("Could not compare files:\n%1").arg(error));
		return;
	}
	attachFileView(view);
}

void MainWindow::openTableComparison(const QString &leftPath,
	const QString &rightPath)
{
	auto *view = new TableCompareView(this);
	QString error;
	if (!view->compare(leftPath, rightPath, &error))
	{
		delete view;
		lm::warning(this, tr("LibreMerge"),
			tr("Could not compare files:\n%1").arg(error));
		return;
	}
	auto refreshTab = [this](TableCompareView *v) {
		const int tabIndex = m_tabs->indexOf(v);
		if (tabIndex < 0)
			return;
		m_tabs->setTabText(tabIndex, (v->isModified()
			? QString::fromUtf8("\xE2\x80\xA2 ") : QString()) + v->tabTitle());
	};
	rememberComparison(view->paths());
	const int index = m_tabs->addTab(view, view->tabTitle());
	m_tabs->setTabToolTip(index, view->paths().join(QStringLiteral("\n")));
	m_tabs->setCurrentIndex(index);
	connect(view, &TableCompareView::modifiedChanged, this,
		[view, refreshTab](bool) { refreshTab(view); });
	connect(view, &TableCompareView::pathsChanged, this,
		[this, view, refreshTab]() {
			refreshTab(view);
			const int tabIndex = m_tabs->indexOf(view);
			if (tabIndex >= 0)
				m_tabs->setTabToolTip(tabIndex,
					view->paths().join(QStringLiteral("\n")));
		});
	connect(view, &TableCompareView::openAsTextRequested, this,
		[this, view](const QString &left, const QString &right) {
			const int tabIndex = m_tabs->indexOf(view);
			openFileComparison({ left, right }, {}, true);
			if (tabIndex >= 0 && !view->isModified())
			{
				m_tabs->removeTab(tabIndex);
				view->deleteLater();
			}
		});

	if (OptionsDialog::scrollToFirstDiff())
		QTimer::singleShot(0, view, [view]() { view->gotoFirstDiff(); });
	watchIdentical(view);
	watchFiles(view);
}

void MainWindow::openImageComparison(const QStringList &paths,
	const QList<bool> &readOnly)
{
	auto *view = new ImageCompareView(this);
	QString error;
	if (!view->compare(paths, &error))
	{
		delete view;
		lm::warning(this, tr("LibreMerge"),
			tr("Could not compare files:\n%1").arg(error));
		return;
	}
	view->setReadOnlySides(readOnly);
	auto refreshTab = [this](ImageCompareView *v) {
		const int tabIndex = m_tabs->indexOf(v);
		if (tabIndex < 0)
			return;
		m_tabs->setTabText(tabIndex, (v->isModified()
			? QString::fromUtf8("\xE2\x80\xA2 ") : QString()) + v->tabTitle());
	};
	rememberComparison(view->paths());
	const int index = m_tabs->addTab(view, view->tabTitle());
	m_tabs->setTabToolTip(index, view->paths().join(QStringLiteral("\n")));
	m_tabs->setCurrentIndex(index);
	connect(view, &ImageCompareView::modifiedChanged, this,
		[view, refreshTab](bool) { refreshTab(view); });
	connect(view, &ImageCompareView::pathsChanged, this,
		[this, view, refreshTab]() {
			refreshTab(view);
			const int tabIndex = m_tabs->indexOf(view);
			if (tabIndex >= 0)
				m_tabs->setTabToolTip(tabIndex,
					view->paths().join(QStringLiteral("\n")));
		});

	if (OptionsDialog::scrollToFirstDiff())
		QTimer::singleShot(0, view, [view]() { view->gotoFirstDiff(); });
	watchIdentical(view);
	watchFiles(view);
}

void MainWindow::openBlankComparison()
{
	// WinMerge's File > New: paste or type text on both sides
	auto *view = new FileCompareView(this);
	view->startBlank();
	attachFileView(view);
}

void MainWindow::attachFileView(FileCompareView *view)
{
	auto refreshTab = [this](FileCompareView *v) {
		const int tabIndex = m_tabs->indexOf(v);
		if (tabIndex < 0)
			return;
		m_tabs->setTabText(tabIndex, (v->isModified()
			? QString::fromUtf8("\xE2\x80\xA2 ") : QString()) + v->tabTitle());
		m_tabs->setTabToolTip(tabIndex, v->paths().join(QStringLiteral("\n")));
	};
	rememberComparison(view->paths());
	const int index = m_tabs->addTab(view, view->tabTitle());
	m_tabs->setTabToolTip(index, view->paths().join(QStringLiteral("\n")));
	m_tabs->setCurrentIndex(index);
	connect(view, &FileCompareView::modifiedChanged, this,
		[view, refreshTab](bool) { refreshTab(view); });
	connect(view, &FileCompareView::pathsChanged, this,
		[view, refreshTab]() { refreshTab(view); });
	connect(view, &FileCompareView::optionsRequested,
		this, &MainWindow::showOptions);

	// WinMerge's "automatically scroll to first difference" (issue #3);
	// deferred one event-loop cycle so the fresh documents are laid out
	// (centerCursor/ensureCursorVisible are unreliable before that)
	if (OptionsDialog::scrollToFirstDiff())
	{
		QTimer::singleShot(0, view, [view]() {
			view->gotoFirstDiff();
			if (OptionsDialog::scrollToFirstInlineDiff())
				view->scrollToFirstInlineDiff();
		});
	}
	watchIdentical(view);
	watchFiles(view);
}

/** WinMerge's identical files message: on opening (OpenDocs), on
    Recompare (OnRefresh) and, for text and tables, on saving (OnFileSave
    rescans). Deferred: the triggers come from inside the page. */
void MainWindow::watchIdentical(QWidget *page)
{
	QPointer<QWidget> guard(page);
	const auto report = [this, guard](bool opening) {
		QTimer::singleShot(0, this, [this, guard, opening]() {
			if (guard)
				reportIfIdentical(guard, opening);
		});
	};
	report(true);
	if (auto *file = qobject_cast<FileCompareView *>(page))
	{
		connect(file, &FileCompareView::rescanned, this, [report]() { report(false); });
		connect(file, &FileCompareView::fileSaved, this, [report]() { report(false); });
	}
	else if (auto *table = qobject_cast<TableCompareView *>(page))
	{
		connect(table, &TableCompareView::rescanned, this, [report]() { report(false); });
		connect(table, &TableCompareView::fileSaved, this, [report]() { report(false); });
	}
	else if (auto *image = qobject_cast<ImageCompareView *>(page))
	{
		connect(image, &ImageCompareView::rescanned, this, [report]() { report(false); });
	}
}

void MainWindow::reportIfIdentical(QWidget *page, bool opening)
{
	QStringList paths;
	int diffs = -1;
	bool modified = false;
	bool otherEncoding = false;
	if (auto *file = qobject_cast<FileCompareView *>(page))
	{
		paths = file->paths();
		diffs = file->diffCount();
		modified = file->isModified();
		otherEncoding = file->encodingsDiffer();
	}
	else if (auto *table = qobject_cast<TableCompareView *>(page))
	{
		paths = table->paths();
		diffs = table->diffCount();
		modified = table->isModified();
		otherEncoding = table->encodingsDiffer();
	}
	else if (auto *image = qobject_cast<ImageCompareView *>(page))
	{
		paths = image->paths();
		diffs = image->diffCount();
		modified = image->isModified();
	}
	if (diffs != 0)
		return;
	// the same text in another encoding is not identical unless "Ignore
	// codepage differences" says so (the end of WinMerge's Rescan)
	if (otherEncoding && !lm::ignoreCodepageDifferences())
		return;
	const auto empty = [](const QString &path) { return path.isEmpty(); };
	// "Don't show message if new buffers created": never for File > New
	if (opening && std::all_of(paths.cbegin(), paths.cend(), empty))
		return;
	lm::showIdenticalMessage(this, paths,
		std::none_of(paths.cbegin(), paths.cend(), empty) && !modified);
}

/** Recompare checks the files first, like WinMerge's Rescan for text and
    tables (its image Refresh does not), and the watches of the
    "Immediately" mode follow the comparison's paths. */
void MainWindow::watchFiles(QWidget *page)
{
	const auto pathsChanged = [this]() { updateFileWatches(); };
	if (auto *file = qobject_cast<FileCompareView *>(page))
	{
		connect(file, &FileCompareView::aboutToRescan, this,
			[this, file]() { checkFileChanged(file, true); });
		connect(file, &FileCompareView::pathsChanged, this, pathsChanged);
	}
	else if (auto *table = qobject_cast<TableCompareView *>(page))
	{
		connect(table, &TableCompareView::aboutToRescan, this,
			[this, table]() { checkFileChanged(table, true); });
		connect(table, &TableCompareView::pathsChanged, this, pathsChanged);
	}
	else if (auto *image = qobject_cast<ImageCompareView *>(page))
	{
		connect(image, &ImageCompareView::pathsChanged, this, pathsChanged);
	}
	updateFileWatches();
}

void MainWindow::applicationActivated()
{
	if (OptionsDialog::autoReloadModifiedFiles()
		== OptionsDialog::AutoReloadOnWindowActivated)
		scheduleFileCheck();
}

void MainWindow::scheduleFileCheck()
{
	if (!m_fileCheckTimer->isActive())
		m_fileCheckTimer->start(0);
}

void MainWindow::checkFileChanged(QWidget *page, bool beforeRescan)
{
	if (m_checkingFiles)
		return;
	QString path;
	if (auto *file = qobject_cast<FileCompareView *>(page))
		path = file->changedPathOnDisk();
	else if (auto *table = qobject_cast<TableCompareView *>(page))
		path = table->changedPathOnDisk();
	else if (auto *image = qobject_cast<ImageCompareView *>(page))
		path = image->changedPathOnDisk();
	if (path.isEmpty())
		return;
	// one question at a time: the triggers keep coming while it is open.
	// Like WinMerge's, it names the first changed file and a Yes reloads
	// the whole comparison; a No asks again at the next trigger
	m_checkingFiles = true;
	const QPointer<QWidget> guard(page);
	if (lm::askReloadChangedFile(this, path) && guard)
		reloadComparison(guard, !beforeRescan);
	m_checkingFiles = false;
	// the answer covers every trigger that came in meanwhile
	m_fileCheckTimer->stop();
	m_lastFileQuestion.start();
}

void MainWindow::reloadCurrentComparison()
{
	reloadComparison(m_tabs->currentWidget());
}

bool MainWindow::reloadComparison(QWidget *page, bool reportIdentical)
{
	auto *file = qobject_cast<FileCompareView *>(page);
	auto *table = qobject_cast<TableCompareView *>(page);
	auto *image = qobject_cast<ImageCompareView *>(page);
	if (file == nullptr && table == nullptr && image == nullptr)
		return false;
	if (!promptAndSaveIfNeeded(page, false))
		return false;
	QString error;
	const bool reloaded = file != nullptr ? file->reload(&error)
		: table != nullptr ? table->reload(&error) : image->reload(&error);
	if (!reloaded)
	{
		lm::warning(this, tr("LibreMerge"), tr("Could not reload:\n%1").arg(error));
		return false;
	}
	// like opening (OpenDocs), a reload tells when the files are identical
	if (reportIdentical)
	{
		QTimer::singleShot(0, this, [this, guard = QPointer<QWidget>(page)]() {
			if (guard)
				reportIfIdentical(guard, true);
		});
	}
	return true;
}

bool MainWindow::promptAndSaveIfNeeded(QWidget *page, bool closing)
{
	if (!hasUnsavedChanges(page))
		return true;
	auto *view = qobject_cast<FileCompareView *>(page);
	auto *table = qobject_cast<TableCompareView *>(page);
	auto *image = qobject_cast<ImageCompareView *>(page);

	enum { Cancel, Save, Discard };
	if (view == nullptr)
	{
		// tables and images are saved as a whole
		int choice = Cancel;
		if (savePromptForTest())
		{
			choice = savePromptForTest()();
		}
		else
		{
			const auto answer = lm::question(this, tr("Save Changes"),
				closing
					? tr("This comparison has unsaved changes. Save before closing?")
					: tr("This comparison has unsaved changes. Save before reloading?"),
				QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
			choice = answer == QMessageBox::Save ? Save
				: answer == QMessageBox::Discard ? Discard : Cancel;
		}
		if (choice == Cancel)
			return false;
		if (choice == Save)
		{
			QString error;
			const bool saved = table != nullptr ? table->saveModified(&error)
				: image->saveModified(&error);
			if (!saved)
			{
				lm::warning(this, tr("LibreMerge"),
					tr("Could not save:\n%1").arg(error));
				return false;
			}
		}
		return true;
	}

	// like WinMerge's Save Modified Files dialog: list each modified side
	// and let the user pick what gets saved
	const QList<int> sides = view->modifiedSideIndexes();
	QList<int> sidesToSave;
	if (savePromptForTest())
	{
		const int choice = savePromptForTest()();
		if (choice == Cancel)
			return false;
		if (choice == Save)
			sidesToSave = sides;
	}
	else
	{
		QDialog dialog(this);
		dialog.setWindowModality(Qt::WindowModal);
		dialog.setWindowTitle(tr("Save Changes"));
		auto *layout = new QVBoxLayout(&dialog);
		auto *label = new QLabel(closing
			? tr("This comparison has unsaved changes. Save the checked "
				"files before closing?")
			: tr("This comparison has unsaved changes. Save the checked "
				"files before reloading?"), &dialog);
		label->setWordWrap(true);
		layout->addWidget(label);
		QList<QCheckBox *> boxes;
		for (const int side : sides)
		{
			auto *box = new QCheckBox(view->sideLabel(side), &dialog);
			box->setChecked(true);
			layout->addWidget(box);
			boxes.append(box);
		}
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save
			| QDialogButtonBox::Discard | QDialogButtonBox::Cancel, &dialog);
		connect(buttons, &QDialogButtonBox::clicked, &dialog,
			[&dialog, buttons](QAbstractButton *button) {
				switch (buttons->standardButton(button))
				{
				case QDialogButtonBox::Save: dialog.done(Save); break;
				case QDialogButtonBox::Discard: dialog.done(Discard); break;
				default: dialog.reject(); break;
				}
			});
		layout->addWidget(buttons);

		const int choice = dialog.exec();
		if (choice == Cancel)
			return false;
		if (choice == Save)
			for (int k = 0; k < sides.size(); ++k)
				if (boxes.at(k)->isChecked())
					sidesToSave.append(sides.at(k));
	}
	for (const int side : sidesToSave)
	{
		QString error;
		if (!view->saveSideAt(side, &error))
		{
			lm::warning(this, tr("LibreMerge"),
				tr("Could not save:\n%1").arg(error));
			return false;
		}
	}
	return true;
}

void MainWindow::updateFileWatches()
{
	// only the "Immediately" mode watches; a file is watched through its
	// folder too, which tells when it comes back after being replaced or
	// deleted (WinMerge's DirWatcher listens on the folder). A file that
	// was replaced lost its own watch: this puts it back
	QStringList files;
	QStringList wanted;
	if (OptionsDialog::autoReloadModifiedFiles() == OptionsDialog::AutoReloadImmediately)
	{
		for (int i = 0; i < m_tabs->count(); ++i)
		{
			const QStringList paths = comparedPaths(m_tabs->widget(i));
			for (const QString &path : paths)
			{
				if (path.isEmpty() || files.contains(path))
					continue;
				files.append(path);
				const QFileInfo info(path);
				if (info.exists())
					wanted.append(path);
				const QString folder = info.absolutePath();
				if (QFileInfo(folder).isDir() && !wanted.contains(folder))
					wanted.append(folder);
			}
		}
	}
	const QStringList watched = m_fileWatcher->files() + m_fileWatcher->directories();
	QStringList stale;
	for (const QString &path : watched)
		if (!wanted.contains(path))
			stale.append(path);
	if (!stale.isEmpty())
		m_fileWatcher->removePaths(stale);
	QStringList added;
	for (const QString &path : std::as_const(wanted))
		if (!watched.contains(path))
			added.append(path);
	if (!added.isEmpty())
		m_fileWatcher->addPaths(added);

	// only a change to a compared file asks: the folder watch reports its
	// neighbors too, which WinMerge's DirWatcher drops by name. A file new
	// to the watch has nothing to compare against yet
	bool changed = false;
	QHash<QString, lm::FileStamp> seen;
	for (const QString &path : std::as_const(files))
	{
		const lm::FileStamp stamp = lm::fileStamp(path);
		const auto known = m_watchedStamps.constFind(path);
		changed = changed || (known != m_watchedStamps.constEnd() && *known != stamp);
		seen.insert(path, stamp);
	}
	m_watchedStamps = seen;
	if (changed)
		scheduleFileCheck();
}

QStringList MainWindow::watchedPathsForTest() const
{
	return m_fileWatcher->files() + m_fileWatcher->directories();
}

void MainWindow::setSavePromptForTest(std::function<int()> answer)
{
	savePromptForTest() = std::move(answer);
}

/** WinMerge's DoSelfCompare: one file against a snapshot of itself taken
    now, the copy read-only on the left as "Original File", the file
    itself editable on the right. The recent list keeps the file alone. */
void MainWindow::openSelfComparison(const QString &path)
{
	// keeps the snapshot as long as the comparison it belongs to
	class SnapshotOwner : public QObject
	{
	public:
		QTemporaryDir dir;
	};
	auto owner = std::make_unique<SnapshotOwner>();
	const QString copy = owner->dir.filePath(QFileInfo(path).fileName());
	if (!owner->dir.isValid() || !QFile::copy(path, copy))
	{
		lm::warning(this, tr("LibreMerge"), tr("Could not compare files:\n%1")
			.arg(tr("cannot copy %1").arg(path)));
		return;
	}
	const int before = m_tabs->count();
	openFileComparison({ copy, path }, { true, false });
	if (m_tabs->count() == before)
		return;
	QWidget *page = m_tabs->currentWidget();
	owner.release()->setParent(page);
	const QString description = tr("Original File");
	if (auto *file = qobject_cast<FileCompareView *>(page))
		file->setSideDescription(0, description);
	else if (auto *table = qobject_cast<TableCompareView *>(page))
		table->setSideDescription(0, description);
	else if (auto *image = qobject_cast<ImageCompareView *>(page))
		image->setSideDescription(0, description);
	const int index = m_tabs->indexOf(page);
	if (auto *file = qobject_cast<FileCompareView *>(page))
		m_tabs->setTabText(index, file->tabTitle());
	else if (auto *table = qobject_cast<TableCompareView *>(page))
		m_tabs->setTabText(index, table->tabTitle());
	else if (auto *image = qobject_cast<ImageCompareView *>(page))
		m_tabs->setTabText(index, image->tabTitle());
	m_tabs->setTabToolTip(index, path);

	// the snapshot pair went into the recent list: keep the file instead
	QSettings settings;
	const QString key = QStringLiteral("RecentComparisons/List");
	QStringList entries = settings.value(key).toStringList();
	entries.removeAll(QStringList{ copy, path }.join(QChar('\n')));
	entries.removeAll(path);
	entries.prepend(path);
	settings.setValue(key, entries);
}

void MainWindow::displayFilterBarCommand(Qt::KeyboardModifiers held)
{
	const bool byShortcut = held.testFlag(Qt::ControlModifier)
		&& held.testFlag(Qt::ShiftModifier);
	if (auto *folder = qobject_cast<FolderCompareView *>(m_tabs->currentWidget()))
	{
		if (byShortcut)
			folder->showDisplayFilterBar();
		else
			folder->toggleDisplayFilterBar();
	}
	else if (auto *file = qobject_cast<FileCompareView *>(m_tabs->currentWidget()))
	{
		if (byShortcut)
			file->showDisplayFilterBar();
		else
			file->toggleDisplayFilterBar();
	}
	else if (auto *table = qobject_cast<TableCompareView *>(m_tabs->currentWidget()))
	{
		if (byShortcut)
			table->showDisplayFilterBar();
		else
			table->toggleDisplayFilterBar();
	}
}

void MainWindow::wireFolderRowSync(FolderCompareView *folder)
{
	QWidget *tab = m_tabs->currentWidget();
	QPointer<FolderCompareView> guard(folder);
	const auto forward = [guard](const QStringList &paths, int diffs) {
		if (!guard.isNull())
			guard->updateSavedItem(paths, diffs);
	};
	if (auto *file = qobject_cast<FileCompareView *>(tab))
		connect(file, &FileCompareView::fileSaved, folder, forward);
	else if (auto *table = qobject_cast<TableCompareView *>(tab))
		connect(table, &TableCompareView::fileSaved, folder, forward);
	else if (auto *image = qobject_cast<ImageCompareView *>(tab))
		connect(image, &ImageCompareView::fileSaved, folder, forward);
}

void MainWindow::openFolderComparison(const QString &leftDir, const QString &rightDir)
{
	openFolderComparison(QStringList{ leftDir, rightDir });
}

void MainWindow::openFolderComparison(const QStringList &dirs)
{
	for (const QString &dir : dirs)
		if (!QFileInfo(dir).isDir())
		{
			// an archive among the sides: extract, then compare
			openArchiveComparison(dirs);
			return;
		}
	auto *view = new FolderCompareView(this);
	connect(view, &FolderCompareView::filtersRequested, this, &MainWindow::showFilters);
	connect(view, &FolderCompareView::openFileComparisonRequested, this,
		[this, view](const QString &l, const QString &r) {
			openFileComparison(l, r);
			wireFolderRowSync(view);
		});
	connect(view, &FolderCompareView::openFileComparison3Requested, this,
		[this, view](const QStringList &paths) {
			openFileComparison(paths);
			wireFolderRowSync(view);
		});
	QStringList names, tips;
	for (const QString &dir : dirs)
	{
		names.append(displayName(dir));
		tips.append(dir);
	}
	const int index = m_tabs->addTab(view,
		names.join(QString::fromUtf8(" \xE2\x86\x94 ")));
	m_tabs->setTabToolTip(index, tips.join(QStringLiteral("\n")));
	m_tabs->setCurrentIndex(index);
	rememberComparison(dirs);
	view->start(dirs);
}

void MainWindow::openArchiveComparison(const QStringList &sources)
{
	// WinMerge's DecompressArchive handles every side on its own: archives
	// are extracted to temporary folders, folders are compared in place,
	// so an archive compares against a plain folder, two or three ways
	QProgressDialog progress(QString(), tr("Cancel"), 0, 0, this);
	progress.setWindowModality(Qt::WindowModal);
	progress.setMinimumDuration(300);

	std::vector<std::unique_ptr<QTemporaryDir>> temps;
	QStringList roots;
	QStringList archiveNames; // empty for sides that are plain folders
	for (const QString &source : sources)
	{
		if (QFileInfo(source).isDir())
		{
			roots.append(source);
			archiveNames.append(QString());
			continue;
		}
		progress.setLabelText(tr("Extracting %1\xE2\x80\xA6")
			.arg(QFileInfo(source).fileName()));
		auto dir = std::make_unique<QTemporaryDir>(
			QDir::tempPath() + QStringLiteral("/libremerge-archive-XXXXXX"));
		if (!dir->isValid())
		{
			lm::warning(this, tr("LibreMerge"),
				tr("Could not create a temporary folder for extraction."));
			return;
		}
		QString error;
		bool cancelled = false;
		const bool ok = lm::extractArchive(source, dir->path(), &error,
			[&progress, &cancelled](const QString &) {
				QCoreApplication::processEvents();
				cancelled = progress.wasCanceled();
				return !cancelled;
			});
		if (!ok)
		{
			if (!cancelled)
				lm::warning(this, tr("LibreMerge"),
					tr("Could not read the archive:\n%1\n\n%2")
						.arg(source, error));
			return; // the temp dirs clean themselves up
		}
		roots.append(dir->path());
		archiveNames.append(QFileInfo(source).fileName());
		temps.push_back(std::move(dir));
	}
	progress.close();

	auto *view = new FolderCompareView(this);
	connect(view, &FolderCompareView::filtersRequested, this, &MainWindow::showFilters);
	// pane headers show the path inside the archive, WinMerge's display
	// roots, instead of the extraction temp path; folder sides keep their
	// real paths
	const auto captionPanes = [this, roots, archiveNames](
		const QStringList &opened) {
		auto *fc = qobject_cast<FileCompareView *>(m_tabs->currentWidget());
		if (fc == nullptr)
			return;
		for (int pane = 0; pane < opened.size(); ++pane)
			for (int side = 0; side < roots.size(); ++side)
			{
				const QString root = roots.at(side) + QLatin1Char('/');
				if (!archiveNames.at(side).isEmpty()
					&& opened.at(pane).startsWith(root))
				{
					fc->setSideCaption(pane, archiveNames.at(side)
						+ opened.at(pane).mid(roots.at(side).length()));
					break;
				}
			}
	};
	connect(view, &FolderCompareView::openFileComparisonRequested, this,
		[this, view, captionPanes](const QString &l, const QString &r) {
			openFileComparison(l, r);
			captionPanes({ l, r });
			wireFolderRowSync(view);
		});
	connect(view, &FolderCompareView::openFileComparison3Requested, this,
		[this, view, captionPanes](const QStringList &paths) {
			openFileComparison(paths);
			captionPanes(paths);
			wireFolderRowSync(view);
		});
	view->adoptTempDirs(std::move(temps));
	// the tab carries the sources' names, not the temp paths
	QStringList names;
	for (const QString &source : sources)
		names.append(displayName(source));
	const int index = m_tabs->addTab(view,
		names.join(QString::fromUtf8(" \xE2\x86\x94 ")));
	m_tabs->setTabToolTip(index, sources.join(QStringLiteral("\n")));
	m_tabs->setCurrentIndex(index);
	rememberComparison(sources);
	view->start(roots);
}

void MainWindow::showOptions()
{
	// the WinMerge-style categorized options dialog (General, Compare)
	OptionsDialog dialog(this);
	const bool accepted = dialog.exec() == QDialog::Accepted;
	// the auto-reload mode decides what is watched
	updateFileWatches();
	if (accepted)
		applyDiffOptions();
}

void MainWindow::applyDiffOptions()
{
	// a forced Rescan of every merge document, as WinMerge does on OK
	// whatever was changed. Folder comparisons keep their results until
	// refreshed (CDirDoc::RefreshOptions only updates the display), and so
	// do images
	rescanFileComparisons();
}

void MainWindow::rescanFileComparisons()
{
	// the files are checked for changes made elsewhere, compared with the
	// options and filters as they are now, and reported when identical
	for (int i = 0; i < m_tabs->count(); ++i)
	{
		QWidget *page = m_tabs->widget(i);
		if (auto *file = qobject_cast<FileCompareView *>(page))
			file->refreshByUser();
		else if (auto *table = qobject_cast<TableCompareView *>(page))
			table->refreshByUser();
	}
}

/** WinMerge's Tools > Filters (CMainFrame::OnToolsFilters): the file, line
    and substitution filters, saved on OK and applied at once to the kind
    of comparison in front. */
void MainWindow::showFilters()
{
	const QString fileFilterBefore = lm::fileFilterMask();
	FiltersDialog dialog(this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	// the file filter is in use from here on (FileFiltersDlg::OnOK)
	lm::adoptFileFilter(dialog.fileFilter());
	const bool fileFilterChanged = lm::fileFilterMask() != fileFilterBefore;
	LineFiltersList lineFilters;
	lm::copyLineFilters(&lineFilters);
	SubstitutionFiltersList substitutionFilters;
	lm::copySubstitutionFilters(&substitutionFilters);
	const bool lineFiltersChanged = dialog.lineFiltersEnabled() != lm::lineFiltersEnabled()
		|| !dialog.lineFilters().Compare(&lineFilters);
	const bool substitutionFiltersChanged =
		!dialog.substitutionFilters().Compare(&substitutionFilters);

	// a text or table comparison in front rescans every open one when the
	// line or the substitution filters changed; a folder comparison in
	// front asks first, when the line filters or the file filter changed,
	// and a Yes refreshes all the open folder comparisons. Anything else
	// in front rescans nothing
	QWidget *front = m_tabs->currentWidget();
	bool rescanFiles = false;
	bool rescanFolders = false;
	if (qobject_cast<FileCompareView *>(front) != nullptr
		|| qobject_cast<TableCompareView *>(front) != nullptr)
		rescanFiles = lineFiltersChanged || substitutionFiltersChanged;
	else if (qobject_cast<FolderCompareView *>(front) != nullptr
		&& (lineFiltersChanged || fileFilterChanged))
		rescanFolders = lm::askRefreshFolderCompares(this);

	// saved whatever the answer, and before the rescans read them
	lm::saveLineFilters(dialog.lineFiltersEnabled(), dialog.lineFilters());
	lm::saveSubstitutionFilters(dialog.substitutionFilters());
	if (rescanFiles)
	{
		rescanFileComparisons();
	}
	else if (rescanFolders)
	{
		for (int i = 0; i < m_tabs->count(); ++i)
			if (auto *folder = qobject_cast<FolderCompareView *>(m_tabs->widget(i)))
				folder->recompare();
	}
}

void MainWindow::newComparison()
{
	openSelector();
}

void MainWindow::rememberComparison(const QStringList &paths)
{
	if (paths.size() < 2)
		return;
	for (const QString &path : paths)
		if (path.isEmpty())
			return; // untitled panes are not reopenable
	// a comparison of files or folders was opened: use of the
	// application, which the request for a star waits for
	lm::StarPrompt::instance()->noteComparisonOpened();
	QSettings settings;
	const QString key = QStringLiteral("RecentComparisons/List");
	QStringList entries = settings.value(key).toStringList();
	const QString entry = paths.join(QChar('\n'));
	entries.removeAll(entry);
	entries.prepend(entry);
	while (entries.size() > 9)
		entries.removeLast();
	settings.setValue(key, entries);
}

void MainWindow::reopenComparison(const QStringList &paths)
{
	// 2- and 3-way folder comparisons, archives included (a 3-way folder
	// entry used to reopen as a file comparison)
	if (paths.size() == 1)
		openSelfComparison(paths.first()); // a self-compare entry
	else if (allFolderLike(paths))
		openFolderComparison(paths);
	else
		openFileComparison(paths);
}

void MainWindow::openSelector(const QStringList &paths)
{
	// reuse an existing selector tab if one is open
	for (int i = 0; i < m_tabs->count(); ++i)
	{
		if (auto *selector = qobject_cast<NewComparisonView *>(m_tabs->widget(i)))
		{
			selector->addPaths(paths);
			m_tabs->setCurrentIndex(i);
			return;
		}
	}

	auto *selector = new NewComparisonView(this);
	selector->addPaths(paths);
	// Cancel closes the screen even when it is the last tab, leaving the
	// window empty, like COpenView::OnCancel (ID_FILE_CLOSE)
	const auto closeSelectorLater = [this, guard = QPointer(selector)]() {
		QTimer::singleShot(0, this, [this, guard]() {
			const int index = guard ? m_tabs->indexOf(guard) : -1;
			if (index >= 0)
				closeTab(index);
		});
	};
	// these signals come from deep inside the selector's own child widgets
	// (Enter in a path field, a button click), so the work is deferred one
	// event-loop cycle: closeTab deletes the page, and opening an archive
	// runs processEvents() for its progress dialog, which would let an
	// already queued close delete the path field while its key handling is
	// still on the stack (the archive x folder crash of cb41cac's CI run)
	connect(selector, &NewComparisonView::compareRequested, this,
		[this, guard = QPointer(selector)](const QStringList &selected,
			const QList<bool> &readOnly, bool folders) {
			QTimer::singleShot(0, this,
				[this, guard, selected, readOnly, folders]() {
					// one file alone: WinMerge's self-compare, which
					// leaves the selection open whatever the option
					if (selected.size() == 1)
					{
						openSelfComparison(selected.first());
						return;
					}
					if (folders)
						openFolderComparison(selected);
					else
						openFileComparison(selected, readOnly);
					// WinMerge's OPT_CLOSE_WITH_OK, off by default: the
					// selection stays open for the next comparison
					if (!OptionsDialog::closeSelectorOnCompare())
						return;
					const int index = guard ? m_tabs->indexOf(guard) : -1;
					if (index >= 0 && m_tabs->count() > 1)
						closeTab(index);
				});
		});
	connect(selector, &NewComparisonView::cancelled, this, closeSelectorLater);
	const int index = m_tabs->addTab(selector, tr("Select Files or Folders"));
	m_tabs->setCurrentIndex(index);
	selector->focusFirstField();
}

void MainWindow::gotoFirstDifference()
{
	if (auto *view = qobject_cast<FileCompareView *>(m_tabs->currentWidget()))
		view->gotoNextDiff();
}

void MainWindow::handleIncomingPaths(const QStringList &paths)
{
	if (paths.isEmpty())
		return;

	// a selector tab that is open collects the drops
	for (int i = 0; i < m_tabs->count(); ++i)
	{
		if (auto *selector = qobject_cast<NewComparisonView *>(m_tabs->widget(i)))
		{
			selector->addPaths(paths);
			m_tabs->setCurrentIndex(i);
			return;
		}
	}

	// like WinMerge: dropping a complete pair/triple starts the comparison;
	// folders and archives, in any mix, open as a folder comparison
	int files = 0;
	for (const QString &path : paths)
		if (QFileInfo(path).isFile())
			++files;
	if (allFolderLike(paths))
	{
		openFolderComparison(paths);
		closeStartupPlaceholder();
		return;
	}
	if ((paths.size() == 2 || paths.size() == 3) && files == paths.size())
	{
		openFileComparison(paths);
		closeStartupPlaceholder();
		return;
	}
	openSelector(paths);
	closeStartupPlaceholder();
}

/** A comparison arriving from the Finder (the Services entry, a file
    association, a Dock drop) right after a cold start lands next to
    the blank untitled comparison the startup opened; WinMerge's shell
    integration leaves no such empty document behind. Only the lone,
    untouched placeholder goes: anything typed into it, or any second
    tab the user made, stays. */
void MainWindow::closeStartupPlaceholder()
{
	if (m_tabs->count() != 2)
		return; // just the placeholder plus what was opened now
	for (int i = 0; i < m_tabs->count(); ++i)
	{
		auto *view = qobject_cast<FileCompareView *>(m_tabs->widget(i));
		if (view == nullptr || view->isModified())
			continue;
		bool blank = true;
		const QStringList sidePaths = view->paths();
		for (const QString &path : sidePaths)
			blank = blank && path.isEmpty();
		if (blank)
		{
			closeTab(i);
			return;
		}
	}
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
	QStringList paths;
	for (const QUrl &url : event->mimeData()->urls())
	{
		if (url.isLocalFile())
			paths.append(url.toLocalFile());
	}
	event->acceptProposedAction();
	// open from a clean stack, not from inside the drop handler: an archive
	// among the paths extracts with processEvents() for its progress
	QTimer::singleShot(0, this, [this, paths]() { handleIncomingPaths(paths); });
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier)
	{
		event->accept();
		// the key came up through the page's own widgets: closing (and
		// deleting) the page waits one event-loop cycle
		QTimer::singleShot(0, this, [this]() { handleEscape(); });
		return;
	}
	QMainWindow::keyPressEvent(event);
}

/** WinMerge's OPT_CLOSE_WITH_ESC, as its views and CMainFrame apply it
    (MDI child windows being this window's tabs). */
void MainWindow::handleEscape()
{
	const OptionsDialog::CloseWithEsc mode = OptionsDialog::closeWithEsc();
	const int count = m_tabs->count();
	// the selection screen takes Esc as its Cancel button, whatever the
	// option and before the window weighs it: WinMerge's open dialog
	// consumes the key as IDCANCEL ahead of CMainFrame
	if (qobject_cast<NewComparisonView *>(m_tabs->currentWidget()) != nullptr)
	{
		closeTab(m_tabs->currentIndex());
		return;
	}
	if (mode == OptionsDialog::EscMainWindowIfOneTab && count <= 1)
	{
		close();
		return;
	}
	if (count == 0)
	{
		if (mode == OptionsDialog::EscTabOrMainWindow)
			close();
		return;
	}
	if (mode != OptionsDialog::EscDisabled)
		closeTab(m_tabs->currentIndex());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	// WinMerge's "ask when closing multiple windows", off by default
	if (OptionsDialog::askBeforeClosingMultipleTabs() && m_tabs->count() > 1)
	{
		const auto choice = lm::question(this, tr("LibreMerge"),
			tr("Close all %1 comparison tabs?").arg(m_tabs->count()));
		if (choice != QMessageBox::Yes)
		{
			event->ignore();
			return;
		}
	}

	// every comparison with unsaved changes offers to save them first, and
	// one Cancel keeps the window open: what WinMerge's OnClose does with
	// CloseNow (image frames) and SaveAllModified (documents). The tab
	// that asks comes to the front, the prompt of tables and images
	// naming no file
	if (m_askingToClose)
	{
		event->ignore(); // a second close while a prompt is still open
		return;
	}
	m_askingToClose = true;
	bool cancelled = false;
	for (int i = 0; i < m_tabs->count() && !cancelled; ++i)
	{
		QWidget *page = m_tabs->widget(i);
		if (!hasUnsavedChanges(page))
			continue;
		m_tabs->setCurrentIndex(i);
		cancelled = !promptAndSaveIfNeeded(page, true);
	}
	m_askingToClose = false;
	if (cancelled)
	{
		event->ignore();
		return;
	}
	// a check posted by the tabs coming to the front has no window left
	// to ask on
	m_fileCheckTimer->stop();
	m_watchTimer->stop();
	QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::FileOpen)
	{
		const auto *fileEvent = static_cast<QFileOpenEvent *>(event);
		if (!fileEvent->file().isEmpty())
			handleIncomingPaths({ fileEvent->file() });
		return true;
	}
	return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeTab(int index)
{
	QWidget *page = m_tabs->widget(index);
	if (!promptAndSaveIfNeeded(page, true))
		return;
	m_tabs->removeTab(index);
	delete page;
	updateFileWatches();
}
