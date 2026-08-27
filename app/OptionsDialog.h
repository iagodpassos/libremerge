// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QListWidget;
class QStackedWidget;

/**
 * The application options dialog, WinMerge style: a category list on the
 * left (General, Compare) and the matching page on the right. General
 * holds app-wide behavior (scroll to first difference, startup screen,
 * closing confirmation, backups, language); Compare holds the engine's
 * comparison options.
 */
class OptionsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit OptionsDialog(QWidget *parent = nullptr);

	// QSettings keys for the General options (defaults follow WinMerge)
	static bool scrollToFirstDiff();
	static bool scrollToFirstInlineDiff();
	static bool showSelectorAtStartup();
	static bool askBeforeClosingMultipleTabs();

private:
	QWidget *buildGeneralPage();
	QWidget *buildComparePage();
	void load();
	void save();
	void restoreDefaults();

	QListWidget *m_categories;
	QStackedWidget *m_pages;

	// General
	QCheckBox *m_chkScrollFirst;
	QCheckBox *m_chkScrollFirstInline;
	QCheckBox *m_chkShowSelector;
	QCheckBox *m_chkAskClose;
	QCheckBox *m_chkBackup;
	QComboBox *m_cmbLanguage;

	// Compare
	QComboBox *m_cmbWhitespace;
	QCheckBox *m_chkIgnoreCase;
	QCheckBox *m_chkIgnoreBlank;
	QCheckBox *m_chkIgnoreEol;
	QCheckBox *m_chkIgnoreNumbers;
	QComboBox *m_cmbAlgorithm;
	QCheckBox *m_chkMovedBlocks;
};
