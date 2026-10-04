// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

class QCheckBox;
class QComboBox;
class QRadioButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * The application options dialog, laid out like WinMerge's (Preferences
 * dialog): a page tree on the left, where a category's pages sit indented
 * under it (General; Compare > General, Folder; Message Boxes; Backup
 * Files), and the
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
	/** WinMerge's OPT_VERIFY_OPEN_PATHS: the selection checks its paths
	    as they are typed. */
	static bool verifyOpenPaths();
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
	/** WinMerge's OPT_AUTO_RELOAD_MODIFIED_FILES: when a comparison
	    notices by itself that another application changed its files. */
	enum AutoReload
	{
		AutoReloadDisabled,
		AutoReloadOnWindowActivated, ///< the default
		AutoReloadImmediately,
	};
	static AutoReload autoReloadModifiedFiles();

	/** The pages, in the order the tree lists them. */
	enum Page
	{
		GeneralPage,
		ComparePage, ///< Compare > General
		FolderPage,  ///< Compare > Folder
		MessageBoxesPage,
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
	/** The Message Boxes list: (message, hidden) pairs, the answer a
	    hidden one shows, and its Reset button (for tests). */
	QList<QPair<QString, bool>> messageBoxesForTest() const;
	void setMessageBoxHiddenForTest(int row, bool hidden);
	QString messageBoxAnswerForTest(int row) const;
	/** Pick the n-th answer of a hidden question's drop-down. */
	void setMessageBoxAnswerForTest(int row, int answerIndex);
	void resetMessageBoxesForTest() { resetMessageBoxes(); }
	void saveForTest() { save(); }

private:
	QWidget *buildGeneralPage();
	QWidget *buildComparePage();
	QWidget *buildFolderPage();
	QWidget *buildMessageBoxesPage();
	void loadMessageBoxes();
	void resetMessageBoxes();
	void syncMessageAnswer(int row);
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
	QCheckBox *m_chkVerifyPaths;
	QCheckBox *m_chkAskClose;
	QCheckBox *m_chkPreserveFileTime;
	QCheckBox *m_chkShowSelector;
	QCheckBox *m_chkCloseSelector;
	QComboBox *m_cmbAutoComplete;
	QComboBox *m_cmbAutoReload;
	QComboBox *m_cmbTheme;
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
	// Message Boxes
	QTreeWidget *m_messageList;
	// the Answer drop-down of each question, by row (null for the
	// information boxes, which only answer OK)
	QList<QComboBox *> m_messageAnswers;
	// Backup Files
	QCheckBox *m_chkBackup;
};
