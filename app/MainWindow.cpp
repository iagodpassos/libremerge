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
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>

#include "FileCompareView.h"
#include "Theme.h"
#include "FolderCompareView.h"
#include "NewComparisonView.h"

// engine
#include "OptionsMgr.h"
#include "OptionsDef.h"

namespace
{

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
	fileMenu->addSeparator();
	addMenuAction(fileMenu, tr("&Save"), QKeySequence::Save, [fileView]() {
		if (auto *view = fileView())
		{
			QString error;
			view->saveModified(&error);
		}
	});
	addMenuAction(fileMenu, tr("&Close Tab"), QKeySequence::Close,
		[this]() { closeTab(m_tabs->currentIndex()); });
	fileMenu->addSeparator();
	addMenuAction(fileMenu, tr("&Quit"), QKeySequence::Quit,
		[]() { QApplication::quit(); });

	QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
	addMenuAction(editMenu, tr("&Undo"), QKeySequence::Undo, [fileView]() {
		if (auto *view = fileView())
			view->undoActive();
	});
	addMenuAction(editMenu, tr("&Redo"), QKeySequence::Redo, [fileView]() {
		if (auto *view = fileView())
			view->redoActive();
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
	QAction *optionsAction = editMenu->addAction(tr("Comparison &Options..."));
	optionsAction->setMenuRole(QAction::PreferencesRole); // macOS app menu
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
		QKeySequence::ZoomIn, [fileView]() {
			if (auto *view = fileView()) view->zoomIn();
		});
	// pt-BR and US keyboards type "+" as Shift+= — accept Cmd+= too
	zoomInAction->setShortcuts({ QKeySequence::ZoomIn,
		QKeySequence(Qt::CTRL | Qt::Key_Equal) });
	addMenuAction(viewMenu, tr("Zoom &Out"), QKeySequence::ZoomOut,
		[fileView]() {
			if (auto *view = fileView()) view->zoomOut();
		});
	addMenuAction(viewMenu, tr("&Actual Size"),
		QKeySequence(Qt::CTRL | Qt::Key_0), [fileView]() {
			if (auto *view = fileView()) view->zoomReset();
		});
	viewMenu->addSeparator();
	addMenuAction(viewMenu, tr("Next &Pane"), QKeySequence(Qt::Key_F6),
		[fileView]() {
			if (auto *view = fileView()) view->focusNextPane();
		});

	QMenu *mergeMenu = menuBar()->addMenu(tr("&Merge"));
	addMenuAction(mergeMenu, tr("&First Difference"),
		QKeySequence(Qt::ALT | Qt::Key_Home), [fileView]() {
			if (auto *view = fileView()) view->gotoFirstDiff();
		});
	QAction *prevDiffAction = addMenuAction(mergeMenu, tr("&Previous Difference"),
		QKeySequence(Qt::ALT | Qt::Key_Up), [fileView]() {
			if (auto *view = fileView()) view->gotoPrevDiff();
		});
	// F7/F8 are upstream aliases for previous/next
	prevDiffAction->setShortcuts({ QKeySequence(Qt::ALT | Qt::Key_Up),
		QKeySequence(Qt::Key_F7) });
	QAction *nextDiffAction = addMenuAction(mergeMenu, tr("&Next Difference"),
		QKeySequence(Qt::ALT | Qt::Key_Down), [fileView]() {
			if (auto *view = fileView()) view->gotoNextDiff();
		});
	nextDiffAction->setShortcuts({ QKeySequence(Qt::ALT | Qt::Key_Down),
		QKeySequence(Qt::Key_F8) });
	addMenuAction(mergeMenu, tr("&Last Difference"),
		QKeySequence(Qt::ALT | Qt::Key_End), [fileView]() {
			if (auto *view = fileView()) view->gotoLastDiff();
		});
	addMenuAction(mergeMenu, tr("C&urrent Difference"),
		QKeySequence(Qt::ALT | Qt::Key_Return), [fileView]() {
			if (auto *view = fileView()) view->selectDiffAtCursor();
		});
	mergeMenu->addSeparator();
	addMenuAction(mergeMenu, tr("Copy to &Right"),
		QKeySequence(Qt::ALT | Qt::Key_Right), [fileView]() {
			if (auto *view = fileView()) view->copyCurrentDiff(0);
		});
	addMenuAction(mergeMenu, tr("Copy to &Left"),
		QKeySequence(Qt::ALT | Qt::Key_Left), [fileView]() {
			if (auto *view = fileView())
				view->copyCurrentDiff(view->paneCount() == 3 ? 2 : 1);
		});
	// WinMerge's Ctrl+Alt variants copy and jump to the next difference
	addMenuAction(mergeMenu, tr("Copy to Right and Ad&vance"),
		QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right), [fileView]() {
			if (auto *view = fileView()) view->copyCurrentDiff(0, true);
		});
	addMenuAction(mergeMenu, tr("Copy to Left and Advanc&e"),
		QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left), [fileView]() {
			if (auto *view = fileView())
				view->copyCurrentDiff(view->paneCount() == 3 ? 2 : 1, true);
		});
	addMenuAction(mergeMenu, tr("Copy All to Righ&t"), QKeySequence(),
		[fileView]() {
			if (auto *view = fileView()) view->copyAllFrom(0);
		});
	addMenuAction(mergeMenu, tr("Copy All to Le&ft"), QKeySequence(),
		[fileView]() {
			if (auto *view = fileView())
				view->copyAllFrom(view->paneCount() == 3 ? 2 : 1);
		});
	mergeMenu->addSeparator();
	addMenuAction(mergeMenu, tr("S&wap Panes"), QKeySequence(), [fileView]() {
		if (auto *view = fileView()) view->swapSides();
	});
	addMenuAction(mergeMenu, tr("Re&compare"), QKeySequence(Qt::Key_F5),
		[this, fileView]() {
			if (auto *view = fileView())
				view->recompare();
			else if (auto *folder = qobject_cast<FolderCompareView *>(
					m_tabs->currentWidget()))
				folder->recompare();
		});

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

void MainWindow::openFileComparison(const QStringList &paths, const QList<bool> &readOnly)
{
	auto *view = new FileCompareView(this);
	view->setReadOnlySides(readOnly);
	QString error;
	if (!view->compare(paths, &error))
	{
		delete view;
		QMessageBox::warning(this, tr("LibreMerge"),
			tr("Could not compare files:\n%1").arg(error));
		return;
	}
	attachFileView(view);
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
	const int index = m_tabs->addTab(view, view->tabTitle());
	m_tabs->setTabToolTip(index, view->paths().join(QStringLiteral("\n")));
	m_tabs->setCurrentIndex(index);
	connect(view, &FileCompareView::modifiedChanged, this,
		[view, refreshTab](bool) { refreshTab(view); });
	connect(view, &FileCompareView::pathsChanged, this,
		[view, refreshTab]() { refreshTab(view); });
	connect(view, &FileCompareView::optionsRequested,
		this, &MainWindow::showOptions);
}

void MainWindow::openFolderComparison(const QString &leftDir, const QString &rightDir)
{
	auto *view = new FolderCompareView(this);
	connect(view, &FolderCompareView::openFileComparisonRequested, this,
		qOverload<const QString &, const QString &>(&MainWindow::openFileComparison));
	const QString title = QFileInfo(leftDir).fileName() + QString::fromUtf8(" \xE2\x86\x94 ")
		+ QFileInfo(rightDir).fileName();
	const int index = m_tabs->addTab(view, title);
	m_tabs->setTabToolTip(index, leftDir + QStringLiteral("\n") + rightDir);
	m_tabs->setCurrentIndex(index);
	view->start(leftDir, rightDir);
}

void MainWindow::showOptions()
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr)
		return;

	QDialog dialog(this);
	dialog.setWindowTitle(tr("Comparison Options"));
	auto *grid = new QGridLayout(&dialog);

	grid->addWidget(new QLabel(tr("Whitespace:"), &dialog), 0, 0);
	auto *whitespace = new QComboBox(&dialog);
	whitespace->addItems({ tr("Compare"), tr("Ignore changes"), tr("Ignore all") });
	whitespace->setCurrentIndex(mgr->GetInt(OPT_CMP_IGNORE_WHITESPACE));
	grid->addWidget(whitespace, 0, 1);

	auto *ignoreCase = new QCheckBox(tr("Ignore case"), &dialog);
	ignoreCase->setChecked(mgr->GetBool(OPT_CMP_IGNORE_CASE));
	grid->addWidget(ignoreCase, 1, 0, 1, 2);
	auto *ignoreBlank = new QCheckBox(tr("Ignore blank lines"), &dialog);
	ignoreBlank->setChecked(mgr->GetBool(OPT_CMP_IGNORE_BLANKLINES));
	grid->addWidget(ignoreBlank, 2, 0, 1, 2);
	auto *ignoreEol = new QCheckBox(tr("Ignore carriage return differences"), &dialog);
	ignoreEol->setChecked(mgr->GetBool(OPT_CMP_IGNORE_EOL));
	grid->addWidget(ignoreEol, 3, 0, 1, 2);
	auto *ignoreNumbers = new QCheckBox(tr("Ignore numbers"), &dialog);
	ignoreNumbers->setChecked(mgr->GetBool(OPT_CMP_IGNORE_NUMBERS));
	grid->addWidget(ignoreNumbers, 4, 0, 1, 2);

	grid->addWidget(new QLabel(tr("Diff algorithm:"), &dialog), 5, 0);
	auto *algorithm = new QComboBox(&dialog);
	algorithm->addItems({ tr("Default"), tr("Minimal"), tr("Patience"),
		tr("Histogram"), tr("None") });
	algorithm->setCurrentIndex(mgr->GetInt(OPT_CMP_DIFF_ALGORITHM));
	grid->addWidget(algorithm, 5, 1);

	// like WinMerge's OPT_BACKUP_FILECMP, on by default
	auto *backup = new QCheckBox(
		tr("Back up the original file when saving (.bak)"), &dialog);
	backup->setChecked(QSettings()
		.value(QStringLiteral("Backup/FileCompare"), true).toBool());
	grid->addWidget(backup, 6, 0, 1, 2);

	auto *note = new QLabel(tr("Open comparisons pick the new options up on "
		"Recompare (F5) or when reopened."), &dialog);
	note->setWordWrap(true);
	grid->addWidget(note, 7, 0, 1, 2);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	grid->addWidget(buttons, 8, 0, 1, 2);

	if (dialog.exec() != QDialog::Accepted)
		return;

	mgr->SaveOption(OPT_CMP_IGNORE_WHITESPACE, whitespace->currentIndex());
	mgr->SaveOption(OPT_CMP_IGNORE_CASE, ignoreCase->isChecked());
	mgr->SaveOption(OPT_CMP_IGNORE_BLANKLINES, ignoreBlank->isChecked());
	mgr->SaveOption(OPT_CMP_IGNORE_EOL, ignoreEol->isChecked());
	mgr->SaveOption(OPT_CMP_IGNORE_NUMBERS, ignoreNumbers->isChecked());
	mgr->SaveOption(OPT_CMP_DIFF_ALGORITHM, algorithm->currentIndex());
	mgr->FlushOptions();
	QSettings().setValue(QStringLiteral("Backup/FileCompare"),
		backup->isChecked());
}

void MainWindow::newComparison()
{
	openSelector();
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
	if (auto *view = qobject_cast<FileCompareView *>(page); view != nullptr && view->isModified())
	{
		// like WinMerge's closing dialog: list each modified side and
		// let the user pick what gets saved
		QDialog dialog(this);
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
					QMessageBox::warning(this, tr("LibreMerge"),
						tr("Could not save:\n%1").arg(error));
					return;
				}
			}
		}
	}
	m_tabs->removeTab(index);
	delete page;
}
