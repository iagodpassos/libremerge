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
#include "TableFiles.h"
#include "ImageCompareView.h"
#include "HexCompareView.h"
#include "HexView.h"
#include "BinaryFiles.h"
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
	if (auto *hex = qobject_cast<HexCompareView *>(page))
		return hex->paths();
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
	if (auto *hex = qobject_cast<HexCompareView *>(page))
		return hex->isModified();
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
	connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
		QWidget *page = m_tabs->widget(index);
		m_tabHistory.removeIf([page](const QPointer<QWidget> &tab) {
			return tab.isNull() || tab == page;
		});
		if (page != nullptr)
			m_tabHistory.prepend(page);
	});
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
	auto hexView = [this]() {
		return qobject_cast<HexCompareView *>(m_tabs->currentWidget());
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
	QMenu *recentMenu = fileMenu->addMenu(tr("Recent F&iles or Folders"));
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
		[this, fileView, tableView, imageView, hexView]() {
			QString error;
			if (auto *view = fileView())
				view->saveModified(&error);
			else if (auto *table = tableView())
				table->saveModified(&error);
			else if (auto *image = imageView())
				image->saveModified(&error);
			else if (auto *hex = hexView())
			{
				if (!hex->saveModified(&error) && !error.isEmpty())
					lm::warning(this, tr("LibreMerge"), tr("Could not save:\n%1").arg(error));
			}
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
	// WinMerge's File > Convert Line Endings to, for the active pane
	QMenu *convertEolMenu = fileMenu->addMenu(tr("&Convert Line Endings to"));
	convertEolMenu->menuAction()->setObjectName(QStringLiteral("fileConvertEol"));
	connect(convertEolMenu, &QMenu::aboutToShow, this, [this, convertEolMenu]() {
		convertEolMenu->clear();
		if (auto *view = qobject_cast<FileCompareView *>(m_tabs->currentWidget()))
			view->buildConvertEolMenu(convertEolMenu);
	});
	connect(fileMenu, &QMenu::aboutToShow, this, [this, convertEolMenu]() {
		convertEolMenu->menuAction()->setEnabled(
			qobject_cast<FileCompareView *>(m_tabs->currentWidget()) != nullptr);
	});
	fileMenu->addSeparator();
	QAction *quitAction = addMenuAction(fileMenu, tr("&Quit"), QKeySequence::Quit,
		[]() { QApplication::quit(); });
	quitAction->setMenuRole(QAction::QuitRole);

	QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
	addMenuAction(editMenu, tr("&Undo"), QKeySequence::Undo,
		[fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView())
				view->undoActive();
			else if (auto *table = tableView())
				table->undo();
			else if (auto *image = imageView())
				image->undo();
			else if (auto *hex = hexView())
				hex->undo();
		});
	addMenuAction(editMenu, tr("&Redo"), QKeySequence::Redo,
		[fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView())
				view->redoActive();
			else if (auto *table = tableView())
				table->redo();
			else if (auto *image = imageView())
				image->redo();
			else if (auto *hex = hexView())
				hex->redo();
		});
	editMenu->addSeparator();
	// WinMerge's Edit > Select Line Difference (F4); Shift+F4, the previous
	// one, has no menu item (ID_SELECTLINEDIFF, ID_SELECTPREVLINEDIFF)
	QAction *lineDiffAction = addMenuAction(editMenu, tr("Select Line &Difference"),
		QKeySequence(Qt::Key_F4), [fileView]() {
			if (auto *view = fileView())
				view->selectLineDiff(false);
		});
	lineDiffAction->setObjectName(QStringLiteral("editSelectLineDiff"));
	editMenu->addSeparator();
	// the keys of the pane menu's commands that no menu here has: from the
	// neighbors into the active pane, and a difference to the clipboard
	const auto addFileKey = [this, fileView](const char *name, const QKeySequence &key,
		std::function<void(FileCompareView *)> run) {
		auto *action = new QAction(this);
		action->setObjectName(QLatin1String(name));
		action->setShortcut(key);
		connect(action, &QAction::triggered, this, [fileView, run]() {
			if (auto *view = fileView())
				run(view);
		});
		addAction(action);
	};
	addFileKey("editSelectPrevLineDiff", QKeySequence(Qt::SHIFT | Qt::Key_F4),
		[](FileCompareView *view) { view->selectLineDiff(true); });
	addFileKey("mergeCopyFromLeft", QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Right),
		[](FileCompareView *view) { view->copyFromLeft(); });
	addFileKey("mergeCopyFromRight", QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Left),
		[](FileCompareView *view) { view->copyFromRight(); });
	addFileKey("editDiffToClipboard1", QKeySequence(Qt::CTRL | Qt::Key_1),
		[](FileCompareView *view) { view->copySelectedDiffToClipboard(0); });
	addFileKey("editDiffToClipboard2", QKeySequence(Qt::CTRL | Qt::Key_2),
		[](FileCompareView *view) { view->copySelectedDiffToClipboard(1); });
	addFileKey("editDiffToClipboard3", QKeySequence(Qt::CTRL | Qt::Key_3),
		[](FileCompareView *view) {
			if (view->paneCount() == 3)
				view->copySelectedDiffToClipboard(2);
		});
	// WinMerge's Edit > Find... (Ctrl+F) and Replace... (Ctrl+H), the
	// dialogs of the active pane. On the Mac, Command+H hides the
	// application: Replace takes Option+Command+F there, the Mac's own
	QAction *findAction = addMenuAction(editMenu, tr("F&ind..."), QKeySequence::Find,
		[fileView, hexView]() {
			if (auto *view = fileView())
				view->showFind();
			else if (auto *hex = hexView())
				hex->showFind();
		});
	findAction->setObjectName(QStringLiteral("editFind"));
#ifdef Q_OS_MACOS
	const QKeySequence replaceShortcut(Qt::CTRL | Qt::ALT | Qt::Key_F);
#else
	const QKeySequence replaceShortcut(Qt::CTRL | Qt::Key_H);
#endif
	QAction *replaceAction = addMenuAction(editMenu, tr("Repla&ce..."), replaceShortcut,
		[fileView, hexView]() {
			if (auto *view = fileView(); view != nullptr && view->activePaneEditable())
				view->showReplace();
			else if (auto *hex = hexView(); hex != nullptr && hex->activePaneEditable())
				hex->showReplace();
		});
	replaceAction->setObjectName(QStringLiteral("editReplace"));
	// greyed out over a read-only pane (OnUpdateEditReplace) while the menu
	// shows; its shortcut does nothing there either
	connect(editMenu, &QMenu::aboutToShow, this,
		[fileView, hexView, replaceAction, lineDiffAction]() {
			const FileCompareView *view = fileView();
			const HexCompareView *hex = hexView();
			replaceAction->setEnabled(view != nullptr ? view->activePaneEditable()
				: hex == nullptr || hex->activePaneEditable());
			// OnUpdateSelectLineDiff
			lineDiffAction->setEnabled(view != nullptr && view->lineDiffSelectable());
		});
	connect(editMenu, &QMenu::aboutToHide, this, [replaceAction, lineDiffAction]() {
		replaceAction->setEnabled(true);
		lineDiffAction->setEnabled(true);
	});
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
	const auto addRepeat = [this, fileView, hexView](const char *name, bool control, bool shift,
		QList<QKeySequence> keys) {
		auto *action = new QAction(this);
		action->setObjectName(QLatin1String(name));
		action->setShortcuts(keys);
		connect(action, &QAction::triggered, this, [fileView, hexView, control, shift]() {
			if (auto *view = fileView())
				view->findRepeat(control, shift);
			// the binary comparison has F3 and Shift+F3 only (OnEditRepeat)
			else if (auto *hex = hexView(); hex != nullptr && !control)
				hex->findRepeat(shift);
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
		FileCompareView::goToShortcut(), [fileView, hexView]() {
			if (auto *view = fileView())
				view->showGoTo();
			else if (auto *hex = hexView())
				hex->showGoTo();
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
		QKeySequence::ZoomIn, [fileView, imageView, hexView]() {
			if (auto *view = fileView()) view->zoomIn();
			else if (auto *image = imageView()) image->zoomIn();
			else if (auto *hex = hexView()) hex->zoomIn();
		});
	// pt-BR and US keyboards type "+" as Shift+= — accept Cmd+= too
	zoomInAction->setShortcuts({ QKeySequence::ZoomIn,
		QKeySequence(Qt::CTRL | Qt::Key_Equal) });
	addMenuAction(viewMenu, tr("Zoom &Out"), QKeySequence::ZoomOut,
		[fileView, imageView, hexView]() {
			if (auto *view = fileView()) view->zoomOut();
			else if (auto *image = imageView()) image->zoomOut();
			else if (auto *hex = hexView()) hex->zoomOut();
		});
	addMenuAction(viewMenu, tr("&Actual Size"),
		QKeySequence(Qt::CTRL | Qt::Key_0), [fileView, imageView, hexView]() {
			if (auto *view = fileView()) view->zoomReset();
			else if (auto *image = imageView()) image->zoomReset();
			else if (auto *hex = hexView()) hex->zoomReset();
		});
	viewMenu->addSeparator();
	// WinMerge's View Whitespace and View EOL (OnViewWhitespace, OnViewEOL)
	// of the text window: the option turned over, the open comparisons
	// showing it (RefreshOptions)
	const auto addViewOption = [this, viewMenu, fileView](const QString &text, const char *name,
		const String &option) {
		QAction *action = viewMenu->addAction(text);
		action->setObjectName(QLatin1String(name));
		action->setCheckable(true);
		connect(action, &QAction::triggered, this, [this, option]() {
			if (COptionsMgr *mgr = GetOptionsMgr())
				mgr->SaveOption(option, !mgr->GetBool(option));
			for (int i = 0; i < m_tabs->count(); ++i)
				if (auto *file = qobject_cast<FileCompareView *>(m_tabs->widget(i)))
					file->refreshViewOptions();
		});
		connect(viewMenu, &QMenu::aboutToShow, this, [action, fileView, option]() {
			COptionsMgr *mgr = GetOptionsMgr();
			action->setEnabled(fileView() != nullptr);
			action->setChecked(mgr != nullptr && mgr->GetBool(option));
		});
	};
	addViewOption(tr("&View Whitespace"), "viewWhitespaceAction", OPT_VIEW_WHITESPACE);
	addViewOption(tr("View E&OL"), "viewEolAction", OPT_VIEW_EOL);
	// WinMerge's View Line Differences (CMergeEditView::OnViewLineDiffs),
	// of the text and table windows: the option turned over, the open
	// comparisons showing it as they repaint, the one in front compared
	// again (RefreshOptions, FlushAndRescan(true))
	QAction *lineDiffsAction = viewMenu->addAction(tr("Vie&w Line Differences"));
	lineDiffsAction->setObjectName(QStringLiteral("viewLineDiffsAction"));
	lineDiffsAction->setCheckable(true);
	const auto syncLineDiffsAction = [fileView, tableView, lineDiffsAction]() {
		lineDiffsAction->setEnabled(fileView() != nullptr || tableView() != nullptr);
		lineDiffsAction->setChecked(lm::viewLineDifferences());
	};
	connect(lineDiffsAction, &QAction::triggered, this,
		[this, fileView, tableView, syncLineDiffsAction]() {
			if (COptionsMgr *mgr = GetOptionsMgr())
				mgr->SaveOption(OPT_WORDDIFF_HIGHLIGHT, !lm::viewLineDifferences());
			for (int i = 0; i < m_tabs->count(); ++i)
			{
				if (auto *file = qobject_cast<FileCompareView *>(m_tabs->widget(i)))
					file->refreshLineDifferences();
				else if (auto *table = qobject_cast<TableCompareView *>(m_tabs->widget(i)))
					table->refreshLineDifferences();
			}
			if (auto *file = fileView())
				file->rescan();
			else if (auto *table = tableView())
				table->rescan();
			syncLineDiffsAction();
		});
	connect(viewMenu, &QMenu::aboutToShow, this, syncLineDiffsAction);
	connect(m_tabs, &QTabWidget::currentChanged, this, syncLineDiffsAction);
	syncLineDiffsAction();
	viewMenu->addSeparator();
	addMenuAction(viewMenu, tr("Next &Pane"), QKeySequence(Qt::Key_F6),
		[fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView()) view->focusNextPane();
			else if (auto *table = tableView()) table->focusNextPane();
			else if (auto *image = imageView()) image->focusNextPane();
			else if (auto *hex = hexView()) hex->focusNextPane();
		});

	QMenu *mergeMenu = menuBar()->addMenu(tr("&Merge"));
	addMenuAction(mergeMenu, tr("&First Difference"),
		QKeySequence(Qt::ALT | Qt::Key_Home), [fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView()) view->gotoFirstDiff();
			else if (auto *table = tableView()) table->gotoFirstDiff();
			else if (auto *image = imageView()) image->gotoFirstDiff();
			else if (auto *hex = hexView()) hex->gotoFirstDiff();
		});
	QAction *prevDiffAction = addMenuAction(mergeMenu, tr("&Previous Difference"),
		QKeySequence(Qt::ALT | Qt::Key_Up), [fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView()) view->gotoPrevDiff();
			else if (auto *table = tableView()) table->gotoPrevDiff();
			else if (auto *image = imageView()) image->gotoPrevDiff();
			else if (auto *hex = hexView()) hex->gotoPrevDiff();
		});
	// F7/F8 are upstream aliases for previous/next
	prevDiffAction->setShortcuts({ QKeySequence(Qt::ALT | Qt::Key_Up),
		QKeySequence(Qt::Key_F7) });
	QAction *nextDiffAction = addMenuAction(mergeMenu, tr("&Next Difference"),
		QKeySequence(Qt::ALT | Qt::Key_Down), [fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView()) view->gotoNextDiff();
			else if (auto *table = tableView()) table->gotoNextDiff();
			else if (auto *image = imageView()) image->gotoNextDiff();
			else if (auto *hex = hexView()) hex->gotoNextDiff();
		});
	nextDiffAction->setShortcuts({ QKeySequence(Qt::ALT | Qt::Key_Down),
		QKeySequence(Qt::Key_F8) });
	addMenuAction(mergeMenu, tr("&Last Difference"),
		QKeySequence(Qt::ALT | Qt::Key_End), [fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView()) view->gotoLastDiff();
			else if (auto *table = tableView()) table->gotoLastDiff();
			else if (auto *image = imageView()) image->gotoLastDiff();
			else if (auto *hex = hexView()) hex->gotoLastDiff();
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
		QKeySequence(Qt::ALT | Qt::Key_Right), [fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView()) view->copyToRight();
			else if (auto *table = tableView()) table->copyToRight();
			else if (auto *image = imageView()) image->copyToRight();
			else if (auto *hex = hexView()) hex->copyToRight();
		});
	addMenuAction(mergeMenu, tr("Copy to &Left"),
		QKeySequence(Qt::ALT | Qt::Key_Left), [fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView()) view->copyToLeft();
			else if (auto *table = tableView())
				table->copyToLeft();
			else if (auto *image = imageView())
				image->copyToLeft();
			else if (auto *hex = hexView())
				hex->copyToLeft();
		});
	// WinMerge's Ctrl+Alt variants copy and jump to the next difference
	// (the table copy always lands on the difference below the merge)
	addMenuAction(mergeMenu, tr("Copy to Right and Ad&vance"),
		QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right),
		[fileView, tableView]() {
			if (auto *view = fileView()) view->copyToRight(true);
			else if (auto *table = tableView()) table->copyToRight();
		});
	addMenuAction(mergeMenu, tr("Copy to Left and Advanc&e"),
		QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left),
		[fileView, tableView]() {
			if (auto *view = fileView()) view->copyToLeft(true);
			else if (auto *table = tableView())
				table->copyToLeft();
		});
	addMenuAction(mergeMenu, tr("Copy All to Righ&t"), QKeySequence(),
		[fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView()) view->copyAllToRight();
			else if (auto *table = tableView()) table->copyAllToRight();
			else if (auto *image = imageView()) image->copyAllToRight();
			else if (auto *hex = hexView()) hex->copyAllToRight();
		});
	addMenuAction(mergeMenu, tr("Copy All to Le&ft"), QKeySequence(),
		[fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView()) view->copyAllToLeft();
			else if (auto *table = tableView())
				table->copyAllToLeft();
			else if (auto *image = imageView())
				image->copyAllToLeft();
			else if (auto *hex = hexView())
				hex->copyAllToLeft();
		});
	mergeMenu->addSeparator();
	addMenuAction(mergeMenu, tr("S&wap Panes"), QKeySequence(),
		[fileView, tableView]() {
			if (auto *view = fileView()) view->swapSides();
			else if (auto *table = tableView()) table->swapSides();
		});
	addMenuAction(mergeMenu, tr("Re&compare"), QKeySequence(Qt::Key_F5),
		[this, fileView, tableView, imageView, hexView]() {
			if (auto *view = fileView())
				view->refreshByUser();
			else if (auto *table = tableView())
				table->refreshByUser();
			else if (auto *image = imageView())
				image->refreshByUser();
			else if (auto *hex = hexView())
				hex->refreshByUser();
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

	// WinMerge's ShowAutoMergeDoc: the files in the order of their panes,
	// the first one the image patterns take opening the image comparison
	// (2-way or 3-way), or the first one the binary patterns take the
	// binary one; a side with no file (an item of a folder comparison on
	// one side only) takes none. The table patterns come after
	if (!forceText && (paths.size() == 2 || paths.size() == 3))
	{
		for (const QString &path : paths)
		{
			if (lm::isImageFile(path))
			{
				openImageComparison(paths, readOnly);
				return;
			}
			if (lm::matchesBinaryFilePatterns(path))
			{
				openHexComparison(paths, readOnly);
				return;
			}
		}
	}

	// a file that the Table options' patterns take makes the comparison a
	// table one, side-by-side grids, two or three (WinMerge's
	// SetTableProperties: one table file is enough); "Open as Text" in the
	// table view forces the text path
	if (!forceText && (paths.size() == 2 || paths.size() == 3)
		&& std::any_of(paths.cbegin(), paths.cend(), [](const QString &path) {
			return lm::tablePropertiesByFileName(path).isTable;
		}))
	{
		openTableComparison(paths);
		return;
	}

	// binary content opens the binary comparison too (LibreMerge's own:
	// WinMerge shows it as text unless the patterns say otherwise)
	if ((paths.size() == 2 || paths.size() == 3)
		&& std::any_of(paths.cbegin(), paths.cend(), lm::looksBinary))
	{
		openHexComparison(paths, readOnly);
		return;
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

void MainWindow::openTableComparison(const QStringList &paths)
{
	auto *view = new TableCompareView(this);
	if (m_openingCommandLine != nullptr)
		view->setTableOverrides(m_openingCommandLine->tableDelimiter,
			m_openingCommandLine->tableQuote, m_openingCommandLine->tableAllowNewlinesInQuotes);
	QString error;
	if (!view->compare(paths, &error))
	{
		delete view;
		lm::warning(this, tr("LibreMerge"),
			tr("Could not compare files:\n%1").arg(error));
		return;
	}
	view->setSavePrompt([this, view]() { return promptAndSaveIfNeeded(view, false); });
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
		[this, view](const QStringList &files) {
			const int tabIndex = m_tabs->indexOf(view);
			openFileComparison(files, {}, true);
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
	view->setSavePrompt([this, view]() { return promptAndSaveIfNeeded(view, false); });
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

void MainWindow::openHexComparison(const QStringList &paths, const QList<bool> &readOnly)
{
	auto *view = new HexCompareView(this);
	QString error;
	if (!view->compare(paths, &error))
	{
		delete view;
		lm::warning(this, tr("LibreMerge"),
			tr("Could not compare files:\n%1").arg(error));
		return;
	}
	view->setReadOnlySides(readOnly);
	view->setSavePrompt([this, view]() { return promptAndSaveIfNeeded(view, false); });
	auto refreshTab = [this](HexCompareView *v) {
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
	connect(view, &HexCompareView::modifiedChanged, this,
		[view, refreshTab](bool) { refreshTab(view); });
	connect(view, &HexCompareView::pathsChanged, this,
		[view, refreshTab]() { refreshTab(view); });
	view->paneForTest(0)->setFocus();

	// upstream's MoveOnLoad: the first difference when the option asks
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
	// the path bar's ChangeFile saves what was changed first
	view->setSavePrompt([this, view]() { return promptAndSaveIfNeeded(view, false); });
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
    rescans) and on copying differences (FlushAndRescan). Deferred: the
    triggers come from inside the page. */
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
	else if (auto *hex = qobject_cast<HexCompareView *>(page))
	{
		// upstream's binary OnRefresh tells, its saving does not
		connect(hex, &HexCompareView::rescanned, this, [report]() { report(false); });
	}
}

void MainWindow::reportIfIdentical(QWidget *page, bool opening)
{
	// the comparison's result, for the command line's -enableexitcode
	{
		int diffs = -1;
		bool otherEncoding = false;
		if (auto *file = qobject_cast<FileCompareView *>(page))
		{
			diffs = file->diffCount();
			otherEncoding = file->encodingsDiffer();
		}
		else if (auto *table = qobject_cast<TableCompareView *>(page))
		{
			diffs = table->diffCount();
			otherEncoding = table->encodingsDiffer();
		}
		else if (auto *image = qobject_cast<ImageCompareView *>(page))
			diffs = image->diffCount();
		else if (auto *hex = qobject_cast<HexCompareView *>(page))
			diffs = hex->diffCount();
		noteCompareResult(diffs == 0 && (!otherEncoding || lm::ignoreCodepageDifferences())
			? 0 : 1);
	}
	// the command line's -x and -xq hold for the first comparison only
	const lm::CommandLine::ExitNoDiff exitIfNoDiff =
		opening ? m_exitIfNoDiff : lm::CommandLine::ExitNoDiff::Disabled;
	if (opening)
		m_exitIfNoDiff = lm::CommandLine::ExitNoDiff::Disabled;
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
	else if (auto *hex = qobject_cast<HexCompareView *>(page))
	{
		paths = hex->paths();
		diffs = hex->diffCount();
		modified = hex->isModified();
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
	// -x says so whatever was hidden, -xq says nothing; either closes
	// LibreMerge then (ShowIdenticalMessage)
	if (exitIfNoDiff != lm::CommandLine::ExitNoDiff::ExitQuiet)
		lm::showIdenticalMessage(this, paths,
			std::none_of(paths.cbegin(), paths.cend(), empty) && !modified,
			exitIfNoDiff == lm::CommandLine::ExitNoDiff::Exit);
	if (exitIfNoDiff != lm::CommandLine::ExitNoDiff::Disabled)
		QTimer::singleShot(0, this, &QWidget::close);
}

void MainWindow::noteCompareResult(int result)
{
	m_lastCompareResult = result;
}

void MainWindow::closeWhenIdle()
{
	// IsReallyIdle: no folder comparison still running
	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this, timer]() {
		for (int i = 0; i < m_tabs->count(); ++i)
			if (auto *folder = qobject_cast<FolderCompareView *>(m_tabs->widget(i)))
				if (folder->isComparing())
					return;
		timer->stop();
		close();
	});
	timer->start(50);
}

void MainWindow::describeSides(QWidget *page, const QStringList &descriptions)
{
	for (int side = 0; side < descriptions.size(); ++side)
	{
		const QString &description = descriptions.at(side);
		if (description.isEmpty())
			continue;
		if (auto *file = qobject_cast<FileCompareView *>(page))
			file->setSideDescription(side, description);
		else if (auto *table = qobject_cast<TableCompareView *>(page))
			table->setSideDescription(side, description);
		else if (auto *image = qobject_cast<ImageCompareView *>(page))
			image->setSideDescription(side, description);
		else if (auto *hex = qobject_cast<HexCompareView *>(page))
			hex->setSideDescription(side, description);
	}
	const int index = m_tabs->indexOf(page);
	if (index < 0)
		return;
	if (auto *file = qobject_cast<FileCompareView *>(page))
		m_tabs->setTabText(index, file->tabTitle());
	else if (auto *table = qobject_cast<TableCompareView *>(page))
		m_tabs->setTabText(index, table->tabTitle());
	else if (auto *image = qobject_cast<ImageCompareView *>(page))
		m_tabs->setTabText(index, image->tabTitle());
	else if (auto *hex = qobject_cast<HexCompareView *>(page))
		m_tabs->setTabText(index, hex->tabTitle());
}

void MainWindow::openFromCommandLine(const lm::CommandLine &cmd)
{
	using Cmd = lm::CommandLine;
	m_escShutdown = cmd.escShutdown;
	m_exitIfNoDiff = cmd.exitIfNoDiff;
	m_openingCommandLine = &cmd;
	// the sides in the order of the panes: left and right, or left,
	// middle and right
	const QStringList &paths = cmd.paths;
	const bool threeWay = paths.size() > 2;
	const QStringList descriptions = threeWay
		? QStringList{ cmd.leftDesc, cmd.middleDesc, cmd.rightDesc }
		: QStringList{ cmd.leftDesc, cmd.rightDesc };
	const QList<unsigned> flags = threeWay
		? QList<unsigned>{ cmd.leftFlags, cmd.middleFlags, cmd.rightFlags }
		: QList<unsigned>{ cmd.leftFlags, cmd.rightFlags };
	QList<bool> readOnly;
	int focusPane = -1;
	for (int side = 0; side < flags.size(); ++side)
	{
		readOnly.append((flags.at(side) & Cmd::ReadOnly) != 0);
		if (flags.at(side) & Cmd::SetFocus)
			focusPane = side; // GetActivePaneFromFlags: the last one asked
	}

	QWidget *const before = m_tabs->currentWidget();
	const int count = m_tabs->count();
	if (paths.size() == 2 || paths.size() == 3)
	{
		switch (cmd.windowType)
		{
		case Cmd::WindowType::Text:
			openFileComparison(paths, readOnly, true);
			break;
		case Cmd::WindowType::Table:
			openTableComparison(paths);
			break;
		case Cmd::WindowType::Binary:
			openHexComparison(paths, readOnly);
			break;
		case Cmd::WindowType::Image:
			openImageComparison(paths, readOnly);
			break;
		case Cmd::WindowType::Folder:
			openFolderComparison(paths, cmd.recursive);
			break;
		default:
			// folders and archives, in any mix, compare as folders
			if (allFolderLike(paths))
				openFolderComparison(paths, cmd.recursive);
			else
				openFileComparison(paths, readOnly);
			break;
		}
	}
	else if (paths.size() == 1 && cmd.selfCompare)
		openSelfComparison(paths.first());
	else if (!paths.isEmpty())
		openSelector(paths);
	else if (cmd.newCompare)
		openBlankComparison();
	else if (OptionsDialog::showSelectorAtStartup())
		// WinMerge's "show Select Files or Folders at startup" option
		openSelector();
	else
		// like WinMerge, start on a fresh (blank) comparison; the
		// selector stays one Cmd+O away
		openBlankComparison();
	m_openingCommandLine = nullptr;
	// unattended, LibreMerge closes once it compared (CMergeApp::OnIdle)
	if (cmd.nonInteractive)
		closeWhenIdle();

	// the comparison just opened: the sides' descriptions, where it
	// saves, then the pane and the line it opens on (MoveOnLoad) and its
	// language
	QWidget *page = m_tabs->currentWidget();
	if (page == nullptr || (page == before && m_tabs->count() == count))
		return;
	describeSides(page, descriptions);
	if (!cmd.outputPath.isEmpty())
	{
		if (auto *file = qobject_cast<FileCompareView *>(page))
			file->setSaveAsPath(cmd.outputPath);
		else if (auto *table = qobject_cast<TableCompareView *>(page))
			table->setSaveAsPath(cmd.outputPath);
		else if (auto *image = qobject_cast<ImageCompareView *>(page))
			image->setSaveAsPath(cmd.outputPath);
		else if (auto *hex = qobject_cast<HexCompareView *>(page))
			hex->setSaveAsPath(cmd.outputPath);
	}
	if (auto *file = qobject_cast<FileCompareView *>(page))
	{
		if (!cmd.fileExt.isEmpty())
			file->setFileExtension(cmd.fileExt);
		if (focusPane >= 0 || cmd.lineIndex >= 0)
			file->moveOnLoad(focusPane, cmd.lineIndex, cmd.charIndex);
	}
	if (cmd.gotoFirstDiff)
		gotoFirstDifference();
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
	else if (auto *hex = qobject_cast<HexCompareView *>(page))
	{
		connect(hex, &HexCompareView::pathsChanged, this, pathsChanged);
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
	else if (auto *hex = qobject_cast<HexCompareView *>(page))
		path = hex->changedPathOnDisk();
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
	auto *hex = qobject_cast<HexCompareView *>(page);
	if (file == nullptr && table == nullptr && image == nullptr && hex == nullptr)
		return false;
	if (!promptAndSaveIfNeeded(page, false))
		return false;
	QString error;
	const bool reloaded = file != nullptr ? file->reload(&error)
		: table != nullptr ? table->reload(&error)
		: image != nullptr ? image->reload(&error) : hex->reload(&error);
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
	// an unattended run has nobody to answer and must end on its own: it
	// writes what was merged to the output path it was given, when one
	// side only changed (CMergeDoc::PromptAndSaveIfNeeded)
	if (lm::unattended())
	{
		if (auto *file = qobject_cast<FileCompareView *>(page))
		{
			const QList<int> sides = file->modifiedSideIndexes();
			if (!file->saveAsPath().isEmpty() && sides.size() == 1
				&& !file->paths().value(sides.first()).isEmpty())
			{
				QString error;
				file->saveSideAt(sides.first(), &error);
			}
		}
		return true;
	}
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
			auto *hex = qobject_cast<HexCompareView *>(page);
			const bool saved = table != nullptr ? table->saveModified(&error)
				: image != nullptr ? image->saveModified(&error)
				: hex->saveModified(&error);
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
	describeSides(page, { tr("Original File") });
	m_tabs->setTabToolTip(m_tabs->indexOf(page), path);

	// the snapshot pair went into the recent list: keep the file instead
	// (unless the command line leaves it out)
	if (m_openingCommandLine != nullptr
		&& (m_openingCommandLine->leftFlags & lm::CommandLine::NoMru))
		return;
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
	else if (auto *hex = qobject_cast<HexCompareView *>(tab))
		connect(hex, &HexCompareView::fileSaved, folder, forward);
}

void MainWindow::openFolderComparison(const QString &leftDir, const QString &rightDir)
{
	openFolderComparison(QStringList{ leftDir, rightDir });
}

void MainWindow::openFolderComparison(const QStringList &dirs, std::optional<bool> recursive)
{
	for (const QString &dir : dirs)
		if (!QFileInfo(dir).isDir())
		{
			// an archive among the sides: extract, then compare
			openArchiveComparison(dirs);
			return;
		}
	auto *view = new FolderCompareView(this);
	view->setRecursive(recursive.value_or(lm::includeSubfolders()));
	connect(view, &FolderCompareView::filtersRequested, this, &MainWindow::showFilters);
	connect(view, &FolderCompareView::compared, this,
		[this](int differences) { noteCompareResult(differences); });
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
	// a subfolder or the parent folders, in a window of their own; not
	// among the recent ones (FFILEOPEN_NOMRU)
	connect(view, &FolderCompareView::openFolderComparisonRequested, this,
		[this, view](const QStringList &others) {
			const lm::CommandLine noRecent = [] {
				lm::CommandLine cmd;
				cmd.leftFlags = lm::CommandLine::NoMru;
				return cmd;
			}();
			m_openingCommandLine = &noRecent;
			openFolderComparison(others, view->isRecursive());
			m_openingCommandLine = nullptr;
		});
	const auto nameTab = [this, view]() {
		QStringList names, tips;
		for (const QString &dir : view->roots())
		{
			names.append(displayName(dir));
			tips.append(dir);
		}
		const int index = m_tabs->indexOf(view);
		m_tabs->setTabText(index, names.join(QString::fromUtf8(" \xE2\x86\x94 ")));
		m_tabs->setTabToolTip(index, tips.join(QStringLiteral("\n")));
	};
	connect(view, &FolderCompareView::rootsChanged, this, nameTab);
	const int index = m_tabs->addTab(view, QString());
	m_tabs->setCurrentIndex(index);
	rememberComparison(dirs);
	view->start(dirs);
	nameTab();
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
	connect(view, &FolderCompareView::compared, this,
		[this](int differences) { noteCompareResult(differences); });
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
	{
		// CMainFrame::OnOptions: the parsers in the highlight mode's order,
		// the default codepage, the word break characters, then every
		// merge document's options again
		lm::initSyntaxParserFactories();
		lm::updateCodepageModule();
		lm::updateBreakChars();
		for (int i = 0; i < m_tabs->count(); ++i)
			if (auto *file = qobject_cast<FileCompareView *>(m_tabs->widget(i)))
			{
				file->refreshSyntaxHighlighting();
				file->refreshEditorOptions();
				file->refreshDiffOptions();
			}
			else if (auto *table = qobject_cast<TableCompareView *>(m_tabs->widget(i)))
				table->refreshDiffOptions();
		applyDiffOptions();
	}
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
	// AddToRecentDocs: not for a command line that leaves the left side
	// out of the recent ones (-u, -ul)
	if (paths.size() < 2 || (m_openingCommandLine != nullptr
			&& (m_openingCommandLine->leftFlags & lm::CommandLine::NoMru)))
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
	// the command line's -e too (CMainFrame::PreTranslateMessage)
	if ((m_escShutdown || mode == OptionsDialog::EscMainWindowIfOneTab) && count <= 1)
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
	// WinMerge's "ask when closing multiple windows", off by default; an
	// unattended run has nobody to answer, and closes
	if (OptionsDialog::askBeforeClosingMultipleTabs() && m_tabs->count() > 1
		&& !lm::unattended())
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
	// MDI activates the window under the closing one in Z order, the
	// comparison that was current before it, where QTabWidget would pick
	// a neighbouring tab (issue #16). Switching before the removal spares
	// that neighbour a file check it is not shown for
	if (index == m_tabs->currentIndex())
	{
		for (const QPointer<QWidget> &tab : std::as_const(m_tabHistory))
		{
			const int tabIndex = m_tabs->indexOf(tab);
			if (tabIndex >= 0 && tabIndex != index)
			{
				m_tabs->setCurrentIndex(tabIndex);
				break;
			}
		}
	}
	m_tabs->removeTab(index);
	delete page;
	updateFileWatches();
}
