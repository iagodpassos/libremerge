// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QAction>
#include <QApplication>
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
	auto *view = new FileCompareView(this);
	QString error;
	if (!view->compare(leftPath, rightPath, &error))
	{
		delete view;
		QMessageBox::warning(this, tr("LibreMerge"),
			tr("Could not compare files:\n%1").arg(error));
		return;
	}
	const QString title = QFileInfo(leftPath).fileName() + QString::fromUtf8(" \xE2\x86\x94 ")
		+ QFileInfo(rightPath).fileName();
	const int index = m_tabs->addTab(view, title);
	m_tabs->setTabToolTip(index, leftPath + QStringLiteral("\n") + rightPath);
	m_tabs->setCurrentIndex(index);
}

void MainWindow::openFolderComparison(const QString &leftDir, const QString &rightDir)
{
	QApplication::setOverrideCursor(Qt::WaitCursor);
	auto *view = new FolderCompareView(this);
	QString error;
	const bool ok = view->compare(leftDir, rightDir, &error);
	QApplication::restoreOverrideCursor();
	if (!ok)
	{
		delete view;
		QMessageBox::warning(this, tr("LibreMerge"),
			tr("Could not compare folders:\n%1").arg(error));
		return;
	}
	connect(view, &FolderCompareView::openFileComparisonRequested,
		this, &MainWindow::openFileComparison);
	const QString title = QFileInfo(leftDir).fileName() + QString::fromUtf8(" \xE2\x86\x94 ")
		+ QFileInfo(rightDir).fileName();
	const int index = m_tabs->addTab(view, title);
	m_tabs->setTabToolTip(index, leftDir + QStringLiteral("\n") + rightDir);
	m_tabs->setCurrentIndex(index);
}

void MainWindow::newComparison()
{
	QDialog dialog(this);
	dialog.setWindowTitle(tr("New Comparison"));
	auto *grid = new QGridLayout(&dialog);
	QLineEdit *leftEdit = addPathRow(grid, 0, tr("Left:"), &dialog);
	QLineEdit *rightEdit = addPathRow(grid, 1, tr("Right:"), &dialog);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Compare"));
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	grid->addWidget(buttons, 2, 0, 1, 4);

	if (dialog.exec() != QDialog::Accepted)
		return;

	const QString left = leftEdit->text().trimmed();
	const QString right = rightEdit->text().trimmed();
	if (left.isEmpty() || right.isEmpty())
		return;

	const QFileInfo leftInfo(left), rightInfo(right);
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
	m_tabs->removeTab(index);
	delete page;
}
