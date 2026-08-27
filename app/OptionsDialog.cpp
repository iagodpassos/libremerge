// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "OptionsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
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
	auto *layout = new QVBoxLayout(this);

	auto *body = new QHBoxLayout;
	m_categories = new QListWidget(this);
	m_categories->addItems({ tr("General"), tr("Compare") });
	m_categories->setFixedWidth(140);
	body->addWidget(m_categories);
	m_pages = new QStackedWidget(this);
	m_pages->addWidget(buildGeneralPage());
	m_pages->addWidget(buildComparePage());
	body->addWidget(m_pages, 1);
	layout->addLayout(body, 1);
	connect(m_categories, &QListWidget::currentRowChanged,
		m_pages, &QStackedWidget::setCurrentIndex);
	m_categories->setCurrentRow(0);

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	auto *defaultsButton = buttons->addButton(tr("Defaults"),
		QDialogButtonBox::ResetRole);
	connect(defaultsButton, &QPushButton::clicked,
		this, [this]() { restoreDefaults(); });
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		save();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	load();
	resize(560, 420);
}

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
	m_chkShowSelector = new QCheckBox(
		tr("Show \"Select Files or Folders\" screen at startup"), page);
	box->addWidget(m_chkShowSelector);
	m_chkAskClose = new QCheckBox(
		tr("Ask before closing a window with multiple tabs"), page);
	box->addWidget(m_chkAskClose);
	m_chkBackup = new QCheckBox(
		tr("Back up the original file when saving (.bak)"), page);
	box->addWidget(m_chkBackup);

	box->addSpacing(8);
	auto *langRow = new QHBoxLayout;
	langRow->addWidget(new QLabel(tr("Language:"), page));
	m_cmbLanguage = new QComboBox(page);
	m_cmbLanguage->addItem(tr("System default"), QString());
	m_cmbLanguage->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
	m_cmbLanguage->addItem(
		QString::fromUtf8("Portugu\xC3\xAAs (Brasil)"), QStringLiteral("pt_BR"));
	langRow->addWidget(m_cmbLanguage, 1);
	box->addLayout(langRow);
	auto *langNote = new QLabel(
		tr("Language changes take effect after restarting LibreMerge."), page);
	langNote->setWordWrap(true);
	box->addWidget(langNote);

	box->addStretch(1);
	return page;
}

QWidget *OptionsDialog::buildComparePage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	auto *whitespaceRow = new QHBoxLayout;
	whitespaceRow->addWidget(new QLabel(tr("Whitespace:"), page));
	m_cmbWhitespace = new QComboBox(page);
	m_cmbWhitespace->addItems({ tr("Compare"), tr("Ignore changes"),
		tr("Ignore all") });
	whitespaceRow->addWidget(m_cmbWhitespace, 1);
	box->addLayout(whitespaceRow);

	m_chkIgnoreCase = new QCheckBox(tr("Ignore case"), page);
	box->addWidget(m_chkIgnoreCase);
	m_chkIgnoreBlank = new QCheckBox(tr("Ignore blank lines"), page);
	box->addWidget(m_chkIgnoreBlank);
	m_chkIgnoreEol = new QCheckBox(
		tr("Ignore carriage return differences"), page);
	box->addWidget(m_chkIgnoreEol);
	m_chkIgnoreNumbers = new QCheckBox(tr("Ignore numbers"), page);
	box->addWidget(m_chkIgnoreNumbers);

	auto *algorithmRow = new QHBoxLayout;
	algorithmRow->addWidget(new QLabel(tr("Diff algorithm:"), page));
	m_cmbAlgorithm = new QComboBox(page);
	m_cmbAlgorithm->addItems({ tr("Default"), tr("Minimal"), tr("Patience"),
		tr("Histogram"), tr("None") });
	algorithmRow->addWidget(m_cmbAlgorithm, 1);
	box->addLayout(algorithmRow);

	m_chkMovedBlocks = new QCheckBox(tr("Detect moved blocks"), page);
	box->addWidget(m_chkMovedBlocks);

	auto *note = new QLabel(tr("Open comparisons pick the new options up on "
		"Recompare (F5) or when reopened."), page);
	note->setWordWrap(true);
	box->addWidget(note);

	box->addStretch(1);
	return page;
}

void OptionsDialog::load()
{
	QSettings settings;
	m_chkScrollFirst->setChecked(settings.value(kScrollFirst, false).toBool());
	m_chkScrollFirstInline->setChecked(
		settings.value(kScrollFirstInline, false).toBool());
	m_chkShowSelector->setChecked(
		settings.value(kShowSelector, false).toBool());
	m_chkAskClose->setChecked(settings.value(kAskClose, false).toBool());
	m_chkBackup->setChecked(settings.value(kBackup, true).toBool());
	const int langIndex =
		m_cmbLanguage->findData(settings.value(kLanguage).toString());
	m_cmbLanguage->setCurrentIndex(qMax(0, langIndex));

	if (COptionsMgr *mgr = GetOptionsMgr())
	{
		m_cmbWhitespace->setCurrentIndex(mgr->GetInt(OPT_CMP_IGNORE_WHITESPACE));
		m_chkIgnoreCase->setChecked(mgr->GetBool(OPT_CMP_IGNORE_CASE));
		m_chkIgnoreBlank->setChecked(mgr->GetBool(OPT_CMP_IGNORE_BLANKLINES));
		m_chkIgnoreEol->setChecked(mgr->GetBool(OPT_CMP_IGNORE_EOL));
		m_chkIgnoreNumbers->setChecked(mgr->GetBool(OPT_CMP_IGNORE_NUMBERS));
		m_cmbAlgorithm->setCurrentIndex(mgr->GetInt(OPT_CMP_DIFF_ALGORITHM));
		m_chkMovedBlocks->setChecked(mgr->GetBool(OPT_CMP_MOVED_BLOCKS));
	}
}

void OptionsDialog::save()
{
	QSettings settings;
	settings.setValue(kScrollFirst, m_chkScrollFirst->isChecked());
	settings.setValue(kScrollFirstInline, m_chkScrollFirstInline->isChecked());
	settings.setValue(kShowSelector, m_chkShowSelector->isChecked());
	settings.setValue(kAskClose, m_chkAskClose->isChecked());
	settings.setValue(kBackup, m_chkBackup->isChecked());
	settings.setValue(kLanguage, m_cmbLanguage->currentData().toString());

	if (COptionsMgr *mgr = GetOptionsMgr())
	{
		mgr->SaveOption(OPT_CMP_IGNORE_WHITESPACE,
			m_cmbWhitespace->currentIndex());
		mgr->SaveOption(OPT_CMP_IGNORE_CASE, m_chkIgnoreCase->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_BLANKLINES,
			m_chkIgnoreBlank->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_EOL, m_chkIgnoreEol->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_NUMBERS,
			m_chkIgnoreNumbers->isChecked());
		mgr->SaveOption(OPT_CMP_DIFF_ALGORITHM,
			m_cmbAlgorithm->currentIndex());
		mgr->SaveOption(OPT_CMP_MOVED_BLOCKS, m_chkMovedBlocks->isChecked());
		mgr->FlushOptions();
	}
}

void OptionsDialog::restoreDefaults()
{
	m_chkScrollFirst->setChecked(false);
	m_chkScrollFirstInline->setChecked(false);
	m_chkShowSelector->setChecked(false);
	m_chkAskClose->setChecked(false);
	m_chkBackup->setChecked(true);
	m_cmbLanguage->setCurrentIndex(0);
	m_cmbWhitespace->setCurrentIndex(0);
	m_chkIgnoreCase->setChecked(false);
	m_chkIgnoreBlank->setChecked(false);
	m_chkIgnoreEol->setChecked(false);
	m_chkIgnoreNumbers->setChecked(false);
	m_cmbAlgorithm->setCurrentIndex(0);
	m_chkMovedBlocks->setChecked(false);
}
