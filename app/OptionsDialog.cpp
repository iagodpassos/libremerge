// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "OptionsDialog.h"
#include "EngineOptions.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

// engine
#include "OptionsMgr.h"
#include "OptionsDef.h"

namespace
{
const QString kScrollFirst = QStringLiteral("General/ScrollToFirstDiff");
const QString kScrollFirstInline =
	QStringLiteral("General/ScrollToFirstInlineDiff");
const QString kShowSelector = QStringLiteral("General/ShowSelectorAtStartup");
const QString kAskClose = QStringLiteral("General/AskCloseMultipleTabs");
const QString kBackup = QStringLiteral("Backup/FileCompare");
const QString kLanguage = QStringLiteral("Appearance/Language");

/** A label over its control, the way WinMerge's pages stack them. */
void addLabeled(QVBoxLayout *box, const QString &label, QWidget *control)
{
	box->addWidget(new QLabel(label, control->parentWidget()));
	box->addWidget(control);
}

QLabel *noteLabel(const QString &text, QWidget *page)
{
	auto *note = new QLabel(text, page);
	note->setWordWrap(true);
	return note;
}
} // namespace

bool OptionsDialog::scrollToFirstDiff()
{
	return QSettings().value(kScrollFirst, false).toBool();
}

bool OptionsDialog::scrollToFirstInlineDiff()
{
	return QSettings().value(kScrollFirstInline, false).toBool();
}

bool OptionsDialog::showSelectorAtStartup()
{
	return QSettings().value(kShowSelector, false).toBool();
}

bool OptionsDialog::askBeforeClosingMultipleTabs()
{
	return QSettings().value(kAskClose, false).toBool();
}

OptionsDialog::OptionsDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Options"));
#ifndef Q_OS_MACOS
	// window-modal so Wayland/GNOME centers it over the application
	// window; not on macOS, where a Preferences sheet would be unusual
	setWindowModality(Qt::WindowModal);
#endif
	auto *layout = new QVBoxLayout(this);

	auto *body = new QHBoxLayout;
	// WinMerge's page tree (CPreferencesDlg): a category's pages sit
	// indented under it, always expanded
	m_categories = new QTreeWidget(this);
	m_categories->setHeaderHidden(true);
	m_categories->setRootIsDecorated(false);
	m_categories->setItemsExpandable(false);
	m_categories->setIndentation(16);
	m_categories->setFixedWidth(170);
	const auto addCategory = [this](QTreeWidgetItem *parent,
		const QString &text, int page) {
		auto *item = parent != nullptr ? new QTreeWidgetItem(parent)
			: new QTreeWidgetItem(m_categories);
		item->setText(0, text);
		item->setData(0, Qt::UserRole, page);
		return item;
	};
	addCategory(nullptr, tr("General"), GeneralPage);
	QTreeWidgetItem *compare = addCategory(nullptr, tr("Compare"), -1);
	addCategory(compare, tr("General"), ComparePage);
	addCategory(compare, tr("Folder"), FolderPage);
	addCategory(nullptr, tr("Backup Files"), BackupPage);
	m_categories->expandAll();
	body->addWidget(m_categories);

	auto *pageArea = new QVBoxLayout;
	m_pages = new QStackedWidget(this);
	m_pages->addWidget(buildGeneralPage());
	m_pages->addWidget(buildComparePage());
	m_pages->addWidget(buildFolderPage());
	m_pages->addWidget(buildBackupPage());
	pageArea->addWidget(m_pages, 1);
	// every WinMerge page has its own Defaults button, resetting only it
	auto *defaultsRow = new QHBoxLayout;
	defaultsRow->addStretch(1);
	auto *defaultsButton = new QPushButton(tr("Defaults"), this);
	connect(defaultsButton, &QPushButton::clicked, this,
		[this]() { restoreDefaults(m_pages->currentIndex()); });
	defaultsRow->addWidget(defaultsButton);
	pageArea->addLayout(defaultsRow);
	body->addLayout(pageArea, 1);
	layout->addLayout(body, 1);

	connect(m_categories, &QTreeWidget::currentItemChanged, this,
		[this](QTreeWidgetItem *item) { selectCategory(item); });
	m_categories->setCurrentItem(m_categories->topLevelItem(0));

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		save();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	load();
	resize(640, 460);
}

/** A category opens its first page, and the title names the page's path,
    as CPreferencesDlg::OnSelchangedPages does. */
void OptionsDialog::selectCategory(QTreeWidgetItem *item)
{
	if (item == nullptr)
		return;
	QTreeWidgetItem *pageItem = item;
	while (pageItem->childCount() > 0)
		pageItem = pageItem->child(0);
	m_pages->setCurrentIndex(pageItem->data(0, Qt::UserRole).toInt());
	QString path = pageItem->text(0);
	for (QTreeWidgetItem *parent = pageItem->parent(); parent != nullptr;
		parent = parent->parent())
		path = parent->text(0) + QStringLiteral(" > ") + path;
	setWindowTitle(tr("Options (%1)").arg(path));
}

/** WinMerge's General page, in its order. */
QWidget *OptionsDialog::buildGeneralPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	m_chkScrollFirst = new QCheckBox(
		tr("Automatically scroll to first difference"), page);
	box->addWidget(m_chkScrollFirst);
	m_chkScrollFirstInline = new QCheckBox(
		tr("Automatically scroll to first inline difference"), page);
	box->addWidget(m_chkScrollFirstInline);
	m_chkAskClose = new QCheckBox(
		tr("Ask before closing a window with multiple tabs"), page);
	box->addWidget(m_chkAskClose);
	m_chkShowSelector = new QCheckBox(
		tr("Show \"Select Files or Folders\" screen at startup"), page);
	box->addWidget(m_chkShowSelector);

	box->addSpacing(8);
	m_cmbLanguage = new QComboBox(page);
	m_cmbLanguage->addItem(tr("System default"), QString());
	m_cmbLanguage->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
	m_cmbLanguage->addItem(
		QString::fromUtf8("Portugu\xC3\xAAs (Brasil)"), QStringLiteral("pt_BR"));
	addLabeled(box, tr("Language:"), m_cmbLanguage);
	box->addWidget(noteLabel(
		tr("Language changes take effect after restarting LibreMerge."), page));

	box->addStretch(1);
	return page;
}

/** WinMerge's Compare > General page (PropCompare), in its order. */
QWidget *OptionsDialog::buildComparePage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	auto *whitespace = new QGroupBox(tr("Whitespace"), page);
	auto *whitespaceBox = new QVBoxLayout(whitespace);
	const QString whitespaceTexts[3] = { tr("Compare"), tr("Ignore changes"),
		tr("Ignore all") };
	for (int i = 0; i < 3; ++i)
	{
		m_radWhitespace[i] = new QRadioButton(whitespaceTexts[i], whitespace);
		whitespaceBox->addWidget(m_radWhitespace[i]);
	}
	box->addWidget(whitespace);

	m_chkIgnoreBlank = new QCheckBox(tr("Ignore blank lines"), page);
	box->addWidget(m_chkIgnoreBlank);
	m_chkIgnoreCase = new QCheckBox(tr("Ignore case"), page);
	box->addWidget(m_chkIgnoreCase);
	m_chkIgnoreEol = new QCheckBox(
		tr("Ignore carriage return differences"), page);
	box->addWidget(m_chkIgnoreEol);
	m_chkIgnoreNumbers = new QCheckBox(tr("Ignore numbers"), page);
	box->addWidget(m_chkIgnoreNumbers);
	m_chkMovedBlocks = new QCheckBox(tr("Detect moved blocks"), page);
	box->addWidget(m_chkMovedBlocks);

	m_cmbAlgorithm = new QComboBox(page);
	m_cmbAlgorithm->addItems({ tr("Default"), tr("Minimal"), tr("Patience"),
		tr("Histogram"), tr("None") });
	addLabeled(box, tr("Diff algorithm:"), m_cmbAlgorithm);

	box->addSpacing(8);
	box->addWidget(noteLabel(tr("Open comparisons pick the new options up on "
		"Recompare (F5) or when reopened."), page));

	box->addStretch(1);
	return page;
}

/** WinMerge's Compare > Folder page (PropCompareFolder). */
QWidget *OptionsDialog::buildFolderPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	m_cmbCompareMethod = new QComboBox(page);
	for (int method = 0; method < lm::kCompareMethodCount; ++method)
		m_cmbCompareMethod->addItem(lm::compareMethodName(method));
	addLabeled(box, tr("Compare method:"), m_cmbCompareMethod);

	box->addSpacing(8);
	box->addWidget(noteLabel(tr("Open comparisons pick the new options up on "
		"Recompare (F5) or when reopened."), page));
	box->addWidget(noteLabel(tr("The status bar of a folder comparison "
		"shows the method in use; click it to switch and recompare."), page));

	box->addStretch(1);
	return page;
}

/** WinMerge's Backup Files page (PropBackups): the file compare switch;
    folder, name and the folder compare switch are not offered yet. */
QWidget *OptionsDialog::buildBackupPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	box->addWidget(new QLabel(tr("Create backup files for:"), page));
	auto *backupRow = new QHBoxLayout;
	backupRow->addSpacing(16);
	m_chkBackup = new QCheckBox(tr("File compare"), page);
	backupRow->addWidget(m_chkBackup, 1);
	box->addLayout(backupRow);

	box->addSpacing(8);
	box->addWidget(noteLabel(tr("The backup is written next to the original "
		"file, with the .bak extension."), page));

	box->addStretch(1);
	return page;
}

void OptionsDialog::load()
{
	QSettings settings;
	m_chkScrollFirst->setChecked(settings.value(kScrollFirst, false).toBool());
	m_chkScrollFirstInline->setChecked(
		settings.value(kScrollFirstInline, false).toBool());
	m_chkAskClose->setChecked(settings.value(kAskClose, false).toBool());
	m_chkShowSelector->setChecked(
		settings.value(kShowSelector, false).toBool());
	const int langIndex =
		m_cmbLanguage->findData(settings.value(kLanguage).toString());
	m_cmbLanguage->setCurrentIndex(qMax(0, langIndex));
	m_chkBackup->setChecked(settings.value(kBackup, true).toBool());

	if (COptionsMgr *mgr = GetOptionsMgr())
	{
		const int whitespace = mgr->GetInt(OPT_CMP_IGNORE_WHITESPACE);
		m_radWhitespace[whitespace >= 0 && whitespace < 3 ? whitespace : 0]
			->setChecked(true);
		m_chkIgnoreBlank->setChecked(mgr->GetBool(OPT_CMP_IGNORE_BLANKLINES));
		m_chkIgnoreCase->setChecked(mgr->GetBool(OPT_CMP_IGNORE_CASE));
		m_chkIgnoreEol->setChecked(mgr->GetBool(OPT_CMP_IGNORE_EOL));
		m_chkIgnoreNumbers->setChecked(mgr->GetBool(OPT_CMP_IGNORE_NUMBERS));
		m_chkMovedBlocks->setChecked(mgr->GetBool(OPT_CMP_MOVED_BLOCKS));
		m_cmbAlgorithm->setCurrentIndex(mgr->GetInt(OPT_CMP_DIFF_ALGORITHM));
	}
	m_cmbCompareMethod->setCurrentIndex(lm::currentCompareMethod());
}

void OptionsDialog::save()
{
	QSettings settings;
	settings.setValue(kScrollFirst, m_chkScrollFirst->isChecked());
	settings.setValue(kScrollFirstInline, m_chkScrollFirstInline->isChecked());
	settings.setValue(kAskClose, m_chkAskClose->isChecked());
	settings.setValue(kShowSelector, m_chkShowSelector->isChecked());
	settings.setValue(kLanguage, m_cmbLanguage->currentData().toString());
	settings.setValue(kBackup, m_chkBackup->isChecked());

	if (COptionsMgr *mgr = GetOptionsMgr())
	{
		int whitespace = 0;
		for (int i = 0; i < 3; ++i)
			if (m_radWhitespace[i]->isChecked())
				whitespace = i;
		mgr->SaveOption(OPT_CMP_IGNORE_WHITESPACE, whitespace);
		mgr->SaveOption(OPT_CMP_IGNORE_BLANKLINES,
			m_chkIgnoreBlank->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_CASE, m_chkIgnoreCase->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_EOL, m_chkIgnoreEol->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_NUMBERS,
			m_chkIgnoreNumbers->isChecked());
		mgr->SaveOption(OPT_CMP_MOVED_BLOCKS, m_chkMovedBlocks->isChecked());
		mgr->SaveOption(OPT_CMP_DIFF_ALGORITHM,
			m_cmbAlgorithm->currentIndex());
		mgr->FlushOptions();
	}
	lm::saveCompareMethod(m_cmbCompareMethod->currentIndex());
}

/** One page back to WinMerge's defaults, like its per-page button. */
void OptionsDialog::restoreDefaults(int page)
{
	switch (page)
	{
	case GeneralPage:
		m_chkScrollFirst->setChecked(false);
		m_chkScrollFirstInline->setChecked(false);
		m_chkAskClose->setChecked(false);
		m_chkShowSelector->setChecked(false);
		m_cmbLanguage->setCurrentIndex(0);
		break;
	case ComparePage:
		m_radWhitespace[0]->setChecked(true);
		m_chkIgnoreBlank->setChecked(false);
		m_chkIgnoreCase->setChecked(false);
		m_chkIgnoreEol->setChecked(false);
		m_chkIgnoreNumbers->setChecked(false);
		m_chkMovedBlocks->setChecked(false);
		m_cmbAlgorithm->setCurrentIndex(0);
		break;
	case FolderPage:
		m_cmbCompareMethod->setCurrentIndex(0); // Full Contents
		break;
	case BackupPage:
		m_chkBackup->setChecked(true);
		break;
	}
}

QList<OptionsDialog::CategoryInfo> OptionsDialog::categoriesForTest() const
{
	QList<CategoryInfo> categories;
	for (QTreeWidgetItemIterator it(m_categories); *it != nullptr; ++it)
	{
		int depth = 0;
		for (QTreeWidgetItem *parent = (*it)->parent(); parent != nullptr;
			parent = parent->parent())
			++depth;
		categories.append({ (*it)->text(0), depth,
			(*it)->data(0, Qt::UserRole).toInt() });
	}
	return categories;
}

void OptionsDialog::selectCategoryForTest(int index)
{
	QTreeWidgetItemIterator it(m_categories);
	for (int i = 0; i < index && *it != nullptr; ++i)
		++it;
	if (*it != nullptr)
		m_categories->setCurrentItem(*it);
}

int OptionsDialog::currentPageForTest() const
{
	return m_pages->currentIndex();
}

QWidget *OptionsDialog::pageForTest(Page page) const
{
	return m_pages->widget(page);
}

void OptionsDialog::restoreDefaultsForTest()
{
	restoreDefaults(m_pages->currentIndex());
}
