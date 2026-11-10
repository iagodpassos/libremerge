// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>
#include <string>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class SyntaxColors;

namespace lm
{
class ColorButton;
class SubeditList;
struct TableSettings;
class WildcardDropList;
}

/**
 * The application options dialog, laid out like WinMerge's (Preferences
 * dialog): a page tree on the left, where a category's pages sit indented
 * under it (General; Compare > General, Folder, Table, Binary, Image;
 * Message Boxes; Editor > General, Compare/Merge, Syntax; Colors > Color
 * Schemes, Differences, Syntax, Text, Markers, Folder Compare, System;
 * Backup Files; Codepage), and the
 * selected page on the right with its own Defaults button. Every option
 * sits on the page and at the position WinMerge gives it.
 */
class OptionsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit OptionsDialog(QWidget *parent = nullptr);
	~OptionsDialog() override;
	/** Esc and the window's close do nothing once Cancel is disabled, as
	    a dialog's IDCANCEL then. */
	void reject() override;

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
		TablePage,   ///< Compare > Table
		BinaryPage,  ///< Compare > Binary
		ImagePage,   ///< Compare > Image
		MessageBoxesPage,
		EditorPage,  ///< Editor > General
		EditorCompareMergePage, ///< Editor > Compare/Merge
		EditorSyntaxPage, ///< Editor > Syntax
		ColorSchemesPage, ///< Colors > Color Schemes
		MergeColorsPage,  ///< Colors > Differences
		SyntaxColorsPage, ///< Colors > Syntax
		TextColorsPage,   ///< Colors > Text
		MarkerColorsPage, ///< Colors > Markers
		DirColorsPage,    ///< Colors > Folder Compare
		SysColorsPage,    ///< Colors > System
		BackupPage,
		CodepagePage,
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
	/** Whether a color scheme was imported while the dialog was open:
	    the views take the colors again once it is accepted. */
	bool colorsImported() const { return m_colorsImported; }
	/** Whether a color was changed or a scheme imported while the dialog
	    was open: the views take the colors again once it is accepted. */
	bool colorsChanged() const { return m_colorsChanged || m_colorsImported; }
	/** Whether Cancel still closes the dialog (a scheme imported leaves
	    only OK, for tests). */
	bool cancelEnabledForTest() const;
	/** The Editor > Syntax list (for tests). */
	lm::SubeditList *syntaxListForTest() const { return m_syntaxList; }

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	QWidget *buildGeneralPage();
	QWidget *buildComparePage();
	QWidget *buildFolderPage();
	QWidget *buildTablePage();
	void showTableSettings(const lm::TableSettings &table);
	QWidget *buildBinaryPage();
	QWidget *buildImagePage();
	QWidget *buildMessageBoxesPage();
	void loadMessageBoxes();
	void resetMessageBoxes();
	void syncMessageAnswer(int row);
	QWidget *buildEditorPage();
	QWidget *buildEditorCompareMergePage();
	void updateLineDiffControls();
	QWidget *buildEditorSyntaxPage();
	QWidget *buildColorSchemesPage();
	bool effectiveColorModeDark() const;
	QString currentColorSchemePath() const;
	void reloadColorSchemes();
	void showColorSchemes(const QString &light, const QString &dark);
	void updateColorSchemeControls();
	void writeColorSchemeOptions();
	void updateColorScheme();
	void saveCurrentScheme();
	void deleteCurrentScheme();
	QWidget *buildMergeColorsPage();
	QWidget *buildSyntaxColorsPage();
	QWidget *buildTextColorsPage();
	QWidget *buildMarkerColorsPage();
	QWidget *buildDirColorsPage();
	QWidget *buildSysColorsPage();
	void loadColorPages();
	void saveColorPages();
	void browseColor(lm::ColorButton *button);
	void browseSyntaxColor(int colorIndex, lm::ColorButton *button);
	void updateTextColorButtons();
	void updateTextColorControls();
	void updateSysColorControls();
	void restoreSysColorDefaults();
	QWidget *buildBackupPage();
	QWidget *buildCodepagePage();
	void showCustomCodepage(int codepage);
	void showAutodetectType(int type);
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
	// Compare > Table
	lm::WildcardDropList *m_cmbCsvPatterns = nullptr;
	QLineEdit *m_editCsvDelimiter = nullptr;
	lm::WildcardDropList *m_cmbTsvPatterns = nullptr;
	lm::WildcardDropList *m_cmbDsvPatterns = nullptr;
	QLineEdit *m_editDsvDelimiter = nullptr;
	QCheckBox *m_chkAllowNewlines = nullptr;
	QLineEdit *m_editQuote = nullptr;
	// Compare > Binary
	lm::WildcardDropList *m_cmbBinaryPatterns = nullptr;
	// Compare > Image
	lm::WildcardDropList *m_cmbImagePatterns = nullptr;
	QCheckBox *m_chkImageInFolderCompare = nullptr;
	QComboBox *m_cmbLanguage;
	// Compare > General
	QRadioButton *m_radWhitespace[3];
	QCheckBox *m_chkIgnoreBlank;
	QCheckBox *m_chkIgnoreCase;
	QCheckBox *m_chkIgnoreEol;
	QCheckBox *m_chkIgnoreNumbers;
	QCheckBox *m_chkIgnoreCodepage;
	QCheckBox *m_chkFilterComments;
	QCheckBox *m_chkIgnoreMissingEol;
	QCheckBox *m_chkIgnoreLineBreaks;
	QCheckBox *m_chkMovedBlocks;
	QComboBox *m_cmbAlgorithm;
	QCheckBox *m_chkIndentHeuristic;
	QCheckBox *m_chkBlankOutIgnored;
	// Compare > Folder
	QComboBox *m_cmbCompareMethod;
	// Editor > General
	QCheckBox *m_chkHiliteSyntax = nullptr;
	QComboBox *m_cmbHiliteSyntaxMode = nullptr;
	QCheckBox *m_chkMixedEol = nullptr;
	QLineEdit *m_editTabSize = nullptr;
	QRadioButton *m_radInsertTabs = nullptr;
	QRadioButton *m_radInsertSpaces = nullptr;
	// Editor > Compare/Merge
	QCheckBox *m_chkAutomaticRescan = nullptr;
	QComboBox *m_cmbCopyGranularity = nullptr;
	QCheckBox *m_chkViewLineDiffs = nullptr;
	QRadioButton *m_radCharLevel = nullptr;
	QRadioButton *m_radWordLevel = nullptr;
	QComboBox *m_cmbBreakType = nullptr;
	QLineEdit *m_editBreakChars = nullptr;
	// Editor > Syntax
	lm::SubeditList *m_syntaxList = nullptr;
	// Colors > Color Schemes
	QComboBox *m_cmbColorMode = nullptr;
	QComboBox *m_cmbColorScheme = nullptr;
	QComboBox *m_cmbColorSchemeDark = nullptr;
	QPushButton *m_btnSaveScheme = nullptr;
	QPushButton *m_btnDeleteScheme = nullptr;
	bool m_colorsImported = false;
	QPushButton *m_defaultsButton = nullptr;
	QPushButton *m_cancelButton = nullptr;
	// Colors > Differences, Folder Compare: each button with its option
	struct ColorBinding
	{
		std::string option;
		lm::ColorButton *button;
	};
	QList<ColorBinding> m_mergeColorBindings;
	QList<QPair<QCheckBox *, lm::ColorButton *>> m_mergeTextColors;
	QList<ColorBinding> m_dirColorBindings;
	QCheckBox *m_chkUseDirColors = nullptr;
	// Colors > Syntax, Text and Markers edit a copy of the editor's colors,
	// saved with OK (CPreferencesDlg::m_pSyntaxColors)
	std::unique_ptr<SyntaxColors> m_syntaxColors;
	QHash<int, lm::ColorButton *> m_syntaxButtons;
	QHash<int, QCheckBox *> m_syntaxBolds;
	QCheckBox *m_chkCustomTextColors = nullptr;
	QList<QWidget *> m_textColorLabels;
	QHash<int, lm::ColorButton *> m_textButtons;
	QHash<int, lm::ColorButton *> m_markerButtons;
	// Colors > System
	QCheckBox *m_chkSysColorHook = nullptr;
	QComboBox *m_cmbSysColorName = nullptr;
	lm::ColorButton *m_btnSysColor = nullptr;
	std::array<int, 31> m_sysColors = {};
	QSet<int> m_customSysColors;
	bool m_colorsChanged = false;
	// Message Boxes
	QTreeWidget *m_messageList;
	// the Answer drop-down of each question, by row (null for the
	// information boxes, which only answer OK)
	QList<QComboBox *> m_messageAnswers;
	// Backup Files
	QCheckBox *m_chkBackup;
	// Codepage
	QRadioButton *m_radCodepage[3] = {};
	QComboBox *m_cmbCustomCodepage = nullptr;
	QCheckBox *m_chkDetectCodepage = nullptr;
	QCheckBox *m_chkDetectCodepage2 = nullptr;
	QComboBox *m_cmbAutodetectType = nullptr;
};
