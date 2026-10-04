// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "OptionsDialog.h"
#include "EngineOptions.h"
#include "ItemCheckStyle.h"
#include "MessageBoxes.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
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
// defaults: WinMerge's OptionsInit
const QString kCloseWithEsc = QStringLiteral("General/CloseWithEsc");
const QString kVerifyPaths = QStringLiteral("General/VerifyOpenPaths");
const QString kPreserveFileTime = QStringLiteral("General/PreserveFileTime");
const QString kCloseSelector = QStringLiteral("General/CloseSelectorOnCompare");
const QString kAutoComplete = QStringLiteral("General/AutoCompleteSource");
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

OptionsDialog::CloseWithEsc OptionsDialog::closeWithEsc()
{
	const int mode = QSettings().value(kCloseWithEsc, EscTabOrMainWindow).toInt();
	return mode >= EscDisabled && mode <= EscMainWindowIfOneTab
		? static_cast<CloseWithEsc>(mode) : EscTabOrMainWindow;
}

bool OptionsDialog::verifyOpenPaths()
{
	return QSettings().value(kVerifyPaths, true).toBool();
}

bool OptionsDialog::preserveFileTime()
{
	return QSettings().value(kPreserveFileTime, false).toBool();
}

bool OptionsDialog::closeSelectorOnCompare()
{
	return QSettings().value(kCloseSelector, false).toBool();
}

OptionsDialog::AutoCompleteSource OptionsDialog::autoCompleteSource()
{
	const int source = QSettings().value(kAutoComplete, AutoCompleteFileSystem).toInt();
	return source >= AutoCompleteDisabled && source <= AutoCompleteRecentList
		? static_cast<AutoCompleteSource>(source) : AutoCompleteFileSystem;
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
	addCategory(nullptr, tr("Message Boxes"), MessageBoxesPage);
	addCategory(nullptr, tr("Backup Files"), BackupPage);
	m_categories->expandAll();
	body->addWidget(m_categories);

	auto *pageArea = new QVBoxLayout;
	m_pages = new QStackedWidget(this);
	m_pages->addWidget(buildGeneralPage());
	m_pages->addWidget(buildComparePage());
	m_pages->addWidget(buildFolderPage());
	m_pages->addWidget(buildMessageBoxesPage());
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
	// WinMerge's MDI child windows are this window's tabs
	m_cmbCloseWithEsc = new QComboBox(page);
	m_cmbCloseWithEsc->addItems({ tr("Disabled"), tr("Tab or main window"),
		tr("Tab only"), tr("Close main window if only one tab") });
	addLabeled(box, tr("Close windows with 'Esc':"), m_cmbCloseWithEsc);

	box->addSpacing(4);
	m_chkVerifyPaths = new QCheckBox(tr("Automatically verify paths in the "
		"\"Select Files or Folders\" screen"), page);
	box->addWidget(m_chkVerifyPaths);
	box->addSpacing(4);
	m_chkAskClose = new QCheckBox(
		tr("Ask before closing a window with multiple tabs"), page);
	box->addWidget(m_chkAskClose);
	m_chkPreserveFileTime = new QCheckBox(
		tr("Preserve file time in file compare"), page);
	box->addWidget(m_chkPreserveFileTime);
	m_chkShowSelector = new QCheckBox(
		tr("Show \"Select Files or Folders\" screen at startup"), page);
	box->addWidget(m_chkShowSelector);
	m_chkCloseSelector = new QCheckBox(
		tr("Close \"Select Files or Folders\" screen on clicking the Compare button"),
		page);
	box->addWidget(m_chkCloseSelector);

	m_cmbAutoComplete = new QComboBox(page);
	m_cmbAutoComplete->addItems({ tr("Disabled"), tr("From file system"),
		tr("From Most Recently Used list") });
	addLabeled(box, tr("\"Select Files or Folders\" auto completion:"),
		m_cmbAutoComplete);

	// the theme (LibreMerge's own) sits with the language, closing the page
	box->addSpacing(8);
	m_cmbTheme = new QComboBox(page);
	m_cmbTheme->setObjectName(QStringLiteral("theme"));
	m_cmbTheme->addItem(tr("System default"), static_cast<int>(lm::ThemeMode::System));
	m_cmbTheme->addItem(tr("Light"), static_cast<int>(lm::ThemeMode::Light));
	m_cmbTheme->addItem(tr("Dark"), static_cast<int>(lm::ThemeMode::Dark));
	addLabeled(box, tr("Theme:"), m_cmbTheme);
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

/** WinMerge's Message Boxes page (PropMessageBoxes): the messages a
    "Don't display this message again" can hide, a tick for each hidden
    one with its remembered answer, and Reset to show them all again. */
QWidget *OptionsDialog::buildMessageBoxesPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	auto *intro = new QHBoxLayout;
	intro->addWidget(noteLabel(tr("Some messages can be hidden by the user. "
		"Press Reset to make all messages visible again."), page), 1);
	auto *reset = new QPushButton(tr("Reset"), page);
	connect(reset, &QPushButton::clicked, this, [this]() {
		resetMessageBoxes();
		lm::showInformation(this, tr("All message boxes are now displayed again."));
	});
	intro->addWidget(reset, 0, Qt::AlignTop);
	box->addLayout(intro);

	m_messageList = new QTreeWidget(page);
	lm::ensureItemCheckBoxes(m_messageList);
	m_messageList->setRootIsDecorated(false);
	m_messageList->setHeaderLabels({ tr("Message"), tr("Answer") });
	m_messageList->header()->setStretchLastSection(false);
	m_messageList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	for (const lm::HideableMessage &message : lm::hideableMessages())
	{
		auto *item = new QTreeWidgetItem(m_messageList);
		item->setText(0, QString(message.text).replace(QLatin1Char('\n'), QLatin1Char(' ')));
		item->setData(0, Qt::UserRole, message.key);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(0, Qt::Unchecked);
	}
	// a ticked message shows its answer, an unticked one none
	// (OnLVNItemChanged); these are information boxes: OK
	connect(m_messageList, &QTreeWidget::itemChanged, this,
		[this](QTreeWidgetItem *item, int column) {
			if (column == 0)
				item->setText(1, item->checkState(0) == Qt::Checked
					? tr("OK") : QString());
		});
	box->addWidget(m_messageList, 1);
	return page;
}

void OptionsDialog::loadMessageBoxes()
{
	for (int i = 0; i < m_messageList->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem *item = m_messageList->topLevelItem(i);
		item->setCheckState(0, lm::messageHidden(item->data(0, Qt::UserRole).toString())
			? Qt::Checked : Qt::Unchecked);
	}
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
	m_cmbCloseWithEsc->setCurrentIndex(closeWithEsc());
	m_chkVerifyPaths->setChecked(verifyOpenPaths());
	m_chkAskClose->setChecked(settings.value(kAskClose, false).toBool());
	m_chkPreserveFileTime->setChecked(preserveFileTime());
	m_chkShowSelector->setChecked(
		settings.value(kShowSelector, false).toBool());
	m_chkCloseSelector->setChecked(closeSelectorOnCompare());
	m_cmbAutoComplete->setCurrentIndex(autoCompleteSource());
	m_cmbTheme->setCurrentIndex(qMax(0, m_cmbTheme->findData(
		static_cast<int>(lm::Theme::instance()->mode()))));
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
	loadMessageBoxes();
}

void OptionsDialog::save()
{
	QSettings settings;
	settings.setValue(kScrollFirst, m_chkScrollFirst->isChecked());
	settings.setValue(kScrollFirstInline, m_chkScrollFirstInline->isChecked());
	settings.setValue(kCloseWithEsc, m_cmbCloseWithEsc->currentIndex());
	settings.setValue(kVerifyPaths, m_chkVerifyPaths->isChecked());
	settings.setValue(kAskClose, m_chkAskClose->isChecked());
	settings.setValue(kPreserveFileTime, m_chkPreserveFileTime->isChecked());
	settings.setValue(kShowSelector, m_chkShowSelector->isChecked());
	settings.setValue(kCloseSelector, m_chkCloseSelector->isChecked());
	settings.setValue(kAutoComplete, m_cmbAutoComplete->currentIndex());
	settings.setValue(kLanguage, m_cmbLanguage->currentData().toString());
	// takes effect at once, unlike the language
	lm::Theme::instance()->setMode(
		static_cast<lm::ThemeMode>(m_cmbTheme->currentData().toInt()));
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
	for (int i = 0; i < m_messageList->topLevelItemCount(); ++i)
	{
		const QTreeWidgetItem *item = m_messageList->topLevelItem(i);
		lm::setMessageHidden(item->data(0, Qt::UserRole).toString(),
			item->checkState(0) == Qt::Checked);
	}
}

/** One page back to WinMerge's defaults, like its per-page button. */
void OptionsDialog::restoreDefaults(int page)
{
	switch (page)
	{
	case GeneralPage:
		m_chkScrollFirst->setChecked(false);
		m_chkScrollFirstInline->setChecked(false);
		m_cmbCloseWithEsc->setCurrentIndex(EscTabOrMainWindow);
		m_chkVerifyPaths->setChecked(true);
		m_chkAskClose->setChecked(false);
		m_chkPreserveFileTime->setChecked(false);
		m_chkShowSelector->setChecked(false);
		m_chkCloseSelector->setChecked(false);
		m_cmbAutoComplete->setCurrentIndex(AutoCompleteFileSystem);
		m_cmbTheme->setCurrentIndex(0); // follow the system
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
	case MessageBoxesPage:
		for (int i = 0; i < m_messageList->topLevelItemCount(); ++i)
			m_messageList->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
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

QList<QPair<QString, bool>> OptionsDialog::messageBoxesForTest() const
{
	QList<QPair<QString, bool>> rows;
	for (int i = 0; i < m_messageList->topLevelItemCount(); ++i)
	{
		const QTreeWidgetItem *item = m_messageList->topLevelItem(i);
		rows.append({ item->text(0), item->checkState(0) == Qt::Checked });
	}
	return rows;
}

void OptionsDialog::setMessageBoxHiddenForTest(int row, bool hidden)
{
	if (QTreeWidgetItem *item = m_messageList->topLevelItem(row))
		item->setCheckState(0, hidden ? Qt::Checked : Qt::Unchecked);
}

/** Reset acts at once, like CMessageBoxDialog::ResetMessageBoxes. */
void OptionsDialog::resetMessageBoxes()
{
	lm::resetHiddenMessages();
	loadMessageBoxes();
}
