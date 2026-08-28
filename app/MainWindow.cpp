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
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include "Dialogs.h"
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>

#include "FileCompareView.h"
#include "TableCompareView.h"
#include "ImageCompareView.h"
#include "ImageFormats.h"
#include "OptionsDialog.h"
#include "Theme.h"
#include "FolderCompareView.h"
#include "NewComparisonView.h"

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
		else
		{
			recentMenu->addSeparator();
			recentMenu->addAction(tr("Clear Menu"), this, []() {
				QSettings().remove(QStringLiteral("RecentComparisons/List"));
			});
		}
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
	addMenuAction(fileMenu, tr("&Close Tab"), QKeySequence::Close,
		[this]() { closeTab(m_tabs->currentIndex()); });
	fileMenu->addSeparator();
	addMenuAction(fileMenu, tr("&Quit"), QKeySequence::Quit,
		[]() { QApplication::quit(); });

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
	addMenuAction(editMenu, tr("&Find..."), QKeySequence::Find, [fileView]() {
		if (auto *view = fileView())
			view->showFindBar();
	});
	addMenuAction(editMenu, tr("Find &Next"), QKeySequence::FindNext, [fileView]() {
		if (auto *view = fileView())
			view->findNext(false);
	});
	addMenuAction(editMenu, tr("Find &Previous"), QKeySequence::FindPrevious,
		[fileView]() {
			if (auto *view = fileView())
				view->findNext(true);
		});
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
	QMenu *themeMenu = viewMenu->addMenu(tr("&Theme"));
	auto *themeGroup = new QActionGroup(this);
	auto addThemeAction = [themeMenu, themeGroup](const QString &text,
		lm::ThemeMode mode) {
		QAction *action = themeMenu->addAction(text);
		action->setCheckable(true);
		themeGroup->addAction(action);
		action->setChecked(lm::Theme::instance()->mode() == mode);
		connect(action, &QAction::triggered,
			[mode]() { lm::Theme::instance()->setMode(mode); });
	};
	addThemeAction(tr("&System"), lm::ThemeMode::System);
	addThemeAction(tr("&Light"), lm::ThemeMode::Light);
	addThemeAction(tr("&Dark"), lm::ThemeMode::Dark);
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
				view->recompare();
			else if (auto *table = tableView())
				table->recompare();
			else if (auto *image = imageView())
				image->recompare();
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
	addMenuAction(toolsMenu, tr("&Line Filters..."), QKeySequence(),
		[this]() { showLineFilters(); });

	QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
	QAction *aboutAction = helpMenu->addAction(tr("&About LibreMerge"));
	connect(aboutAction, &QAction::triggered, this, [this]() {
		QMessageBox::about(this, tr("About LibreMerge"),
			tr("<b>LibreMerge %1</b><br/>"
			   "A free differencing and merging tool for macOS and Linux.<br/><br/>"
			   "Based on the comparison engine of <a href=\"https://winmerge.org\">WinMerge</a>, "
			   "\xC2\xA9 Dean P. Grimm / Thingamahoochie Software and the WinMerge contributors "
			   "(GPL-2.0-or-later).<br/>"
			   "LibreMerge is licensed under the GNU GPL v3.0 or later.<br/><br/>"
			   "Not affiliated with or endorsed by the WinMerge project.")
				.arg(QApplication::applicationVersion()));
	});
}

void MainWindow::openFileComparison(const QString &leftPath, const QString &rightPath)
{
	openFileComparison(QStringList{ leftPath, rightPath });
}

void MainWindow::openFileComparison(const QStringList &paths, const QList<bool> &readOnly,
	bool forceText)
{
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
}

void MainWindow::openFolderComparison(const QString &leftDir, const QString &rightDir)
{
	auto *view = new FolderCompareView(this);
	connect(view, &FolderCompareView::openFileComparisonRequested, this,
		qOverload<const QString &, const QString &>(&MainWindow::openFileComparison));
	const QString title = displayName(leftDir)
		+ QString::fromUtf8(" \xE2\x86\x94 ") + displayName(rightDir);
	const int index = m_tabs->addTab(view, title);
	m_tabs->setTabToolTip(index, leftDir + QStringLiteral("\n") + rightDir);
	m_tabs->setCurrentIndex(index);
	rememberComparison({ leftDir, rightDir });
	view->start(leftDir, rightDir);
}

void MainWindow::showOptions()
{
	// the WinMerge-style categorized options dialog (General, Compare)
	OptionsDialog dialog(this);
	dialog.exec();
}

/** WinMerge's line filters: regular expressions whose matching lines
    are ignored (their diffs become trivial). */
void MainWindow::showLineFilters()
{
	QDialog dialog(this);
	dialog.setWindowModality(Qt::WindowModal);
	dialog.setWindowTitle(tr("Line Filters"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *note = new QLabel(tr("Differences whose lines all match an "
		"enabled regular expression are shown as trivial and skipped "
		"by the navigation."), &dialog);
	note->setWordWrap(true);
	layout->addWidget(note);

	auto *list = new QListWidget(&dialog);
	const QStringList entries = QSettings()
		.value(QStringLiteral("LineFilters/List")).toStringList();
	for (const QString &entry : entries)
	{
		const bool enabled = entry.startsWith(QStringLiteral("1\t"));
		auto *item = new QListWidgetItem(entry.mid(2), list);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable
			| Qt::ItemIsEditable);
		item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
	}
	layout->addWidget(list, 1);

	auto *rowButtons = new QHBoxLayout;
	auto *addButton = new QPushButton(tr("Add"), &dialog);
	connect(addButton, &QPushButton::clicked, &dialog, [list]() {
		auto *item = new QListWidgetItem(QString(), list);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable
			| Qt::ItemIsEditable);
		item->setCheckState(Qt::Checked);
		list->setCurrentItem(item);
		list->editItem(item);
	});
	rowButtons->addWidget(addButton);
	auto *removeButton = new QPushButton(tr("Remove"), &dialog);
	connect(removeButton, &QPushButton::clicked, &dialog, [list]() {
		delete list->currentItem();
	});
	rowButtons->addWidget(removeButton);
	rowButtons->addStretch(1);
	layout->addLayout(rowButtons);

	auto *hint = new QLabel(tr("Open comparisons pick the new options up on "
		"Recompare (F5) or when reopened."), &dialog);
	hint->setWordWrap(true);
	layout->addWidget(hint);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
		| QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	dialog.resize(480, 360);

	if (dialog.exec() != QDialog::Accepted)
		return;
	QStringList saved;
	for (int i = 0; i < list->count(); ++i)
	{
		const QListWidgetItem *item = list->item(i);
		if (item->text().trimmed().isEmpty())
			continue;
		saved.append((item->checkState() == Qt::Checked
			? QStringLiteral("1\t") : QStringLiteral("0\t"))
			+ item->text().trimmed());
	}
	QSettings().setValue(QStringLiteral("LineFilters/List"), saved);
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
	int dirs = 0;
	for (const QString &path : paths)
		if (QFileInfo(path).isDir())
			++dirs;
	if (paths.size() == 2 && dirs == 2)
		openFolderComparison(paths.at(0), paths.at(1));
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
	connect(selector, &NewComparisonView::compareRequested, this,
		[this, selector](const QStringList &selected, const QList<bool> &readOnly,
			bool folders) {
			if (folders)
				openFolderComparison(selected.at(0), selected.at(1));
			else
				openFileComparison(selected, readOnly);
			const int index = m_tabs->indexOf(selector);
			if (index >= 0 && m_tabs->count() > 1)
				closeTab(index);
		});
	connect(selector, &NewComparisonView::cancelled, this, [this, selector]() {
		const int index = m_tabs->indexOf(selector);
		if (index >= 0)
			closeTab(index);
	});
	const int index = m_tabs->addTab(selector, tr("Select Files or Folders"));
	m_tabs->setCurrentIndex(index);
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

	// like WinMerge: dropping a complete pair/triple starts the comparison
	int files = 0, dirs = 0;
	for (const QString &path : paths)
	{
		const QFileInfo info(path);
		if (info.isFile()) ++files;
		else if (info.isDir()) ++dirs;
	}
	if (paths.size() == 2 && dirs == 2)
	{
		openFolderComparison(paths.at(0), paths.at(1));
		return;
	}
	if ((paths.size() == 2 || paths.size() == 3) && files == paths.size())
	{
		openFileComparison(paths);
		return;
	}
	openSelector(paths);
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
	handleIncomingPaths(paths);
	event->acceptProposedAction();
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
	if (auto *image = qobject_cast<ImageCompareView *>(page);
		image != nullptr && image->isModified())
	{
		const auto choice = lm::question(this, tr("Save Changes"),
			tr("This comparison has unsaved changes. Save before closing?"),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
		if (choice == QMessageBox::Cancel)
			return;
		if (choice == QMessageBox::Save)
		{
			QString error;
			if (!image->saveModified(&error))
			{
				lm::warning(this, tr("LibreMerge"),
					tr("Could not save:\n%1").arg(error));
				return;
			}
		}
	}
	if (auto *view = qobject_cast<FileCompareView *>(page); view != nullptr && view->isModified())
	{
		// like WinMerge's closing dialog: list each modified side and
		// let the user pick what gets saved
		QDialog dialog(this);
		dialog.setWindowModality(Qt::WindowModal);
		dialog.setWindowTitle(tr("Save Changes"));
		auto *layout = new QVBoxLayout(&dialog);
		auto *label = new QLabel(
			tr("This comparison has unsaved changes. Save the checked "
			   "files before closing?"), &dialog);
		label->setWordWrap(true);
		layout->addWidget(label);
		QList<QCheckBox *> boxes;
		const QList<int> sides = view->modifiedSideIndexes();
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
				case QDialogButtonBox::Save: dialog.done(1); break;
				case QDialogButtonBox::Discard: dialog.done(2); break;
				default: dialog.reject(); break;
				}
			});
		layout->addWidget(buttons);

		const int choice = dialog.exec();
		if (choice == 0)
			return; // cancelled
		if (choice == 1)
		{
			for (int k = 0; k < sides.size(); ++k)
			{
				if (!boxes.at(k)->isChecked())
					continue;
				QString error;
				if (!view->saveSideAt(sides.at(k), &error))
				{
					lm::warning(this, tr("LibreMerge"),
						tr("Could not save:\n%1").arg(error));
					return;
				}
			}
		}
	}
	m_tabs->removeTab(index);
	delete page;
}
