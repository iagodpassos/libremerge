// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QRadioButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * The application options dialog, laid out like WinMerge's (Preferences
 * dialog): a page tree on the left, where a category's pages sit indented
 * under it (General; Compare > General, Folder; Backup Files), and the
 * selected page on the right with its own Defaults button. Every option
 * sits on the page and at the position WinMerge gives it.
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
	/** WinMerge's OPT_CLOSE_WITH_ESC. */
	enum CloseWithEsc
	{
		EscDisabled,
		EscTabOrMainWindow,  ///< the tab; with none left, the app
		EscTabOnly,
		EscMainWindowIfOneTab,
	};
	static CloseWithEsc closeWithEsc();
	/** WinMerge's OPT_PRESERVE_FILETIMES: saving keeps the file's date. */
	static bool preserveFileTime();
	/** WinMerge's OPT_CLOSE_WITH_OK: Compare closes the selection tab. */
	static bool closeSelectorOnCompare();
	/** WinMerge's OPT_AUTO_COMPLETE_SOURCE for the selection's paths. */
	enum AutoCompleteSource
	{
		AutoCompleteDisabled,
		AutoCompleteFileSystem,
		AutoCompleteRecentList,
	};
	static AutoCompleteSource autoCompleteSource();

	/** The pages, in the order the tree lists them. */
	enum Page
	{
		GeneralPage,
		ComparePage, ///< Compare > General
		FolderPage,  ///< Compare > Folder
		BackupPage,
	};
	/** The tree's items in display order, with their depth and the page
	    each opens (for tests). */
	struct CategoryInfo
	{
		QString text;
		int depth;
		int page;
	};
	QList<CategoryInfo> categoriesForTest() const;
	/** Select the n-th tree item in display order (for tests). */
	void selectCategoryForTest(int index);
	int currentPageForTest() const;
	QWidget *pageForTest(Page page) const;
	/** Press the current page's Defaults button (for tests). */
	void restoreDefaultsForTest();

private:
	QWidget *buildGeneralPage();
	QWidget *buildComparePage();
	QWidget *buildFolderPage();
	QWidget *buildBackupPage();
	void selectCategory(QTreeWidgetItem *item);
	void load();
	void save();
	void restoreDefaults(int page);

	QTreeWidget *m_categories;
	QStackedWidget *m_pages;

	// General
	QCheckBox *m_chkScrollFirst;
	QCheckBox *m_chkScrollFirstInline;
	QComboBox *m_cmbCloseWithEsc;
	QCheckBox *m_chkAskClose;
	QCheckBox *m_chkPreserveFileTime;
	QCheckBox *m_chkShowSelector;
	QCheckBox *m_chkCloseSelector;
	QComboBox *m_cmbAutoComplete;
	QComboBox *m_cmbLanguage;
	// Compare > General
	QRadioButton *m_radWhitespace[3];
	QCheckBox *m_chkIgnoreBlank;
	QCheckBox *m_chkIgnoreCase;
	QCheckBox *m_chkIgnoreEol;
	QCheckBox *m_chkIgnoreNumbers;
	QCheckBox *m_chkMovedBlocks;
	QComboBox *m_cmbAlgorithm;
	// Compare > Folder
	QComboBox *m_cmbCompareMethod;
	// Backup Files
	QCheckBox *m_chkBackup;
};
