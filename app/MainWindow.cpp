// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDragEnterEvent>
#include <QFileOpenEvent>
#include <QMimeData>
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

	QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
	QAction *newAction = fileMenu->addAction(tr("&New Comparison..."));
	newAction->setShortcut(QKeySequence::New);
	connect(newAction, &QAction::triggered, this, &MainWindow::newComparison);
	fileMenu->addSeparator();
	QAction *quitAction = fileMenu->addAction(tr("&Quit"));
	quitAction->setShortcut(QKeySequence::Quit);
	connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

	QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
	QAction *optionsAction = editMenu->addAction(tr("Comparison &Options..."));
	optionsAction->setMenuRole(QAction::PreferencesRole); // macOS app menu
	connect(optionsAction, &QAction::triggered, this, &MainWindow::showOptions);

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
	QStringList names;
	for (const QString &path : paths)
		names.append(QFileInfo(path).fileName());
	const QString title = names.join(QString::fromUtf8(" \xE2\x86\x94 "));
	const int index = m_tabs->addTab(view, title);
	m_tabs->setTabToolTip(index, paths.join(QStringLiteral("\n")));
	m_tabs->setCurrentIndex(index);
	connect(view, &FileCompareView::modifiedChanged, this, [this, view, title](bool modified) {
		const int tabIndex = m_tabs->indexOf(view);
		if (tabIndex >= 0)
			m_tabs->setTabText(tabIndex, modified
				? QString::fromUtf8("\xE2\x80\xA2 ") + title : title);
	});
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

	auto *note = new QLabel(tr("Open comparisons pick the new options up on "
		"Recompare (F5) or when reopened."), &dialog);
	note->setWordWrap(true);
	grid->addWidget(note, 6, 0, 1, 2);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	grid->addWidget(buttons, 7, 0, 1, 2);

	if (dialog.exec() != QDialog::Accepted)
		return;

	mgr->SaveOption(OPT_CMP_IGNORE_WHITESPACE, whitespace->currentIndex());
	mgr->SaveOption(OPT_CMP_IGNORE_CASE, ignoreCase->isChecked());
	mgr->SaveOption(OPT_CMP_IGNORE_BLANKLINES, ignoreBlank->isChecked());
	mgr->SaveOption(OPT_CMP_IGNORE_EOL, ignoreEol->isChecked());
	mgr->SaveOption(OPT_CMP_IGNORE_NUMBERS, ignoreNumbers->isChecked());
	mgr->SaveOption(OPT_CMP_DIFF_ALGORITHM, algorithm->currentIndex());
	mgr->FlushOptions();
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
		const auto choice = QMessageBox::question(this, tr("LibreMerge"),
			tr("This comparison has unsaved changes. Save before closing?"),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
		if (choice == QMessageBox::Cancel)
			return;
		if (choice == QMessageBox::Save)
		{
			QString error;
			if (!view->saveModified(&error))
			{
				QMessageBox::warning(this, tr("LibreMerge"),
					tr("Could not save:\n%1").arg(error));
				return;
			}
		}
	}
	m_tabs->removeTab(index);
	delete page;
}
