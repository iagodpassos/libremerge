// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
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

// engine
#include "OptionsMgr.h"
#include "OptionsDef.h"

namespace
{

/** One row of the "New Comparison" dialog: path box + browse buttons. */
QLineEdit *addPathRow(QGridLayout *grid, int row, const QString &label, QWidget *parent)
{
	grid->addWidget(new QLabel(label, parent), row, 0);
	auto *edit = new QLineEdit(parent);
	grid->addWidget(edit, row, 1);
	auto *fileButton = new QPushButton(QObject::tr("File..."), parent);
	QObject::connect(fileButton, &QPushButton::clicked, parent, [edit, parent]() {
		const QString path = QFileDialog::getOpenFileName(parent);
		if (!path.isEmpty())
			edit->setText(path);
	});
	grid->addWidget(fileButton, row, 2);
	auto *dirButton = new QPushButton(QObject::tr("Folder..."), parent);
	QObject::connect(dirButton, &QPushButton::clicked, parent, [edit, parent]() {
		const QString path = QFileDialog::getExistingDirectory(parent);
		if (!path.isEmpty())
			edit->setText(path);
	});
	grid->addWidget(dirButton, row, 3);
	return edit;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
{
	setWindowTitle(QStringLiteral("LibreMerge"));
	resize(1100, 700);

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

void MainWindow::openFileComparison(const QStringList &paths)
{
	auto *view = new FileCompareView(this);
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
				? QStringLiteral("\xE2\x80\xA2 ") + title : title);
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
	QDialog dialog(this);
	dialog.setWindowTitle(tr("New Comparison"));
	auto *grid = new QGridLayout(&dialog);
	QLineEdit *leftEdit = addPathRow(grid, 0, tr("Left:"), &dialog);
	QLineEdit *middleEdit = addPathRow(grid, 1, tr("Middle (3-way, optional):"), &dialog);
	QLineEdit *rightEdit = addPathRow(grid, 2, tr("Right:"), &dialog);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Compare"));
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	grid->addWidget(buttons, 3, 0, 1, 4);

	if (dialog.exec() != QDialog::Accepted)
		return;

	const QString left = leftEdit->text().trimmed();
	const QString middle = middleEdit->text().trimmed();
	const QString right = rightEdit->text().trimmed();
	if (left.isEmpty() || right.isEmpty())
		return;

	const QFileInfo leftInfo(left), rightInfo(right);
	if (!middle.isEmpty())
	{
		if (leftInfo.isFile() && QFileInfo(middle).isFile() && rightInfo.isFile())
		{
			openFileComparison(QStringList{ left, middle, right });
			return;
		}
		QMessageBox::warning(this, tr("LibreMerge"),
			tr("3-way comparison needs three files."));
		return;
	}
	if (leftInfo.isDir() && rightInfo.isDir())
	{
		openFolderComparison(left, right);
		return;
	}
	if (leftInfo.isFile() && rightInfo.isFile())
	{
		openFileComparison(left, right);
		return;
	}
	QMessageBox::warning(this, tr("LibreMerge"),
		tr("Select two files or two folders."));
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
