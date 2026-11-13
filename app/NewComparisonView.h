// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QStringList>
#include <QWidget>

#include "CommandLine.h"

class FileFilterCombo;
class FileFilterMenu;
class QComboBox;
class QCheckBox;
class QLabel;
class QMenu;
class QPushButton;
class QTimer;
class QToolButton;

/**
 * The "Select Files or Folders" page, mirroring WinMerge's opening
 * screen: three path slots with history, read-only toggles, swap and
 * browse buttons, and the "Folder: Filter" field that sets the file
 * filter of folder comparisons. Accepts files/folders dropped anywhere
 * on it.
 */
class NewComparisonView : public QWidget
{
	Q_OBJECT
public:
	/** What Compare asks the window to open. */
	struct Request
	{
		QStringList paths;
		QList<bool> readOnly;
		/** Folders or archives, compared as folders. */
		bool folders = false;
		/** The window picked from the Compare button's arrow (COpenView's
		    OnCompare with ID_MERGE_COMPARE_*), Automatic for Compare. */
		lm::CommandLine::WindowType windowType = lm::CommandLine::WindowType::Automatic;
		/** The items a folder comparison keeps out of its list, as a
		    project loaded here or the comparison saved from says
		    (OpenFolderParams). */
		QStringList hiddenItems;
	};

	/** What a comparison hands to the screen for File > Save Project
	    (CMainFrame::OnSaveProject fills a COpenDoc with it). */
	struct Prefill
	{
		QStringList paths;
		QList<bool> readOnly;
		QStringList descriptions;
		bool recursive = true;
		QString filter;
		/** 1 text, 2 table, 3 binary, 4 image; -1 when none. */
		int windowType = -1;
		QChar tableDelimiter = QLatin1Char(',');
		QChar tableQuote = QLatin1Char('"');
		bool tableAllowNewlines = false;
		QStringList hiddenItems;
	};

	explicit NewComparisonView(QWidget *parent = nullptr);

	/** Fill the screen with a comparison's paths and settings, to save
	    them as a project. */
	void prefill(const Prefill &state);
	/** COpenView::OnLoadProject: the first comparison of a project file
	    fills the screen, its items as Options > Project's Load column
	    says. */
	void loadProject();
	/** COpenView::OnSaveProject: the screen saved as a project file, its
	    items as the Save column says. */
	void saveProject();
	/** COpenView::AskProjectFileName: the project file to open or to save,
	    picked in the folder of the last one (OPT_PROJECTS_PATH); empty
	    when cancelled. */
	static QString askProjectFileName(QWidget *parent, bool open);
	/** Answer that question without a dialog (for tests); an empty
	    function shows the dialog again. */
	static void setProjectFileNameForTest(std::function<QString(bool open)> answer);
	/** The paths and read-only checks in the fields, and what the screen
	    keeps for a project (for tests). */
	QStringList pathsForTest() const;
	QList<bool> readOnlyForTest() const;
	bool includeSubfoldersForTest() const;
	QStringList hiddenItemsForTest() const { return m_hiddenItems; }
	int windowTypeForTest() const { return m_windowType; }
	int ignoreWhiteForTest() const { return m_ignoreWhite; }

	/** Fill the first empty slots with the given paths. */
	void addPaths(const QStringList &paths);

	/** Put the keyboard focus on the first path field. */
	void focusFirstField();

	/** The path fields' history (WinMerge's Files\Left, Files\Right...),
	    which also feeds the "recent list" auto completion. */
	static QStringList savedHistory();
	/** Forget it, part of WinMerge's "Clear all recent items". */
	static void clearSavedHistory();
	/** Put paths at the top of the history, as WinMerge's addToMru. */
	static void rememberPaths(const QStringList &paths);
	/** Refill the path fields' dropdowns from the saved history. */
	void reloadHistory();

	/** Run the path check now, and read what it showed (for tests). */
	void verifyPathsForTest() { verifyPaths(); }
	/** The "Folder: Filter" field (for tests). */
	FileFilterCombo *filterFieldForTest() const { return m_filterCombo; }
	QString hintForTest() const;
	bool compareEnabledForTest() const;
	/** The menus of the Options and the Compare buttons' arrows, their
	    items named (for tests). */
	QMenu *diffOptionsMenuForTest() const { return m_diffOptionsMenu; }
	QMenu *compareAsMenuForTest() const { return m_compareAsMenu; }
	QToolButton *compareAsButtonForTest() const { return m_compareAsButton; }

	/** COpenDoc::RefreshOptions (COpenView::OnUpdate): the compare options
	    of the Options button's arrow take the saved values again, as the
	    Options dialog is accepted. */
	void refreshOptions();

signals:
	void compareRequested(const NewComparisonView::Request &request);
	/** Compare with a project file as the first path and nothing more:
	    the project opens (COpenView::OnCompare). */
	void openProjectRequested(const QString &path);
	void cancelled();

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	/** Shift+Delete on an open path dropdown forgets the highlighted
	    path, like WinMerge's CSuperComboBox. */
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void compare() { compareAs(lm::CommandLine::WindowType::Automatic); }

private:
	/** COpenView::OnCompare: Compare, or one of the windows of its
	    arrow. */
	void compareAs(lm::CommandLine::WindowType windowType);
	/** The menu of the Options button's arrow
	    (IDR_POPUP_PROJECT_DIFF_OPTIONS), its checks following the
	    screen's own compare options. */
	void buildDiffOptionsMenu();
	/** The menu of the Compare button's arrow (IDR_POPUP_COMPARE). */
	void buildCompareAsMenu();
	/** COpenView::OnUpdateCompare: the windows of the arrow are for
	    files. */
	bool filesToCompareAs() const;
	/** Compare and its arrow go together. */
	void setCompareEnabled(bool enabled);
	/** The screen's compare options saved as the options in use
	    (COpenView::OnCompare). */
	void saveCompareOptions() const;
	/** A project file as the first path with no second one, which
	    Compare opens (UpdateButtonStatesThread's bProject). */
	bool projectInFirstField() const;
	struct Slot
	{
		QComboBox *path;
		QCheckBox *readOnly;
	};

	void swapSlots(int a, int b);
	void browse(int slot, bool folder);
	void setHint(const QString &text, bool error);
	/** WinMerge's "Automatically verify paths" check of the fields. */
	void verifyPaths();
	/** WinMerge's filter group follows the paths' kind. */
	void showFilterGroup(bool visible, bool enabled);
	void applyFileFilter();
	/** Drop one path from the history and from every field's dropdown. */
	void forgetPath(const QString &path);

	Slot m_slots[3];
	QLabel *m_filterTitle = nullptr;
	FileFilterCombo *m_filterCombo = nullptr;
	QToolButton *m_selectFilterButton = nullptr;
	QCheckBox *m_includeSubfolders = nullptr;
	FileFilterMenu *m_filterMenu = nullptr;
	QLabel *m_hint;
	QPushButton *m_compareButton = nullptr;
	QToolButton *m_compareAsButton = nullptr; // Compare's arrow
	QMenu *m_compareAsMenu = nullptr;
	QMenu *m_diffOptionsMenu = nullptr; // the Options button's arrow
	// COpenView's compare options (m_nIgnoreWhite...): the saved ones as
	// the screen opens, changed from the Options button's arrow, saved as
	// Compare runs
	// COpenDoc's values for a project, from one loaded here or the
	// comparison it was saved from: kept for the next save, the hidden
	// items handed to the folder comparison too
	QString m_descriptions[3];
	int m_windowType = -1;
	QChar m_tableDelimiter = QLatin1Char(',');
	QChar m_tableQuote = QLatin1Char('"');
	bool m_tableAllowNewlines = false;
	QStringList m_hiddenItems;
	int m_ignoreWhite = 0;
	bool m_ignoreBlankLines = false;
	bool m_ignoreCase = false;
	bool m_ignoreEol = false;
	bool m_ignoreNumbers = false;
	bool m_ignoreCodepage = false;
	bool m_filterCommentsLines = false;
	bool m_ignoreMissingTrailingEol = false;
	bool m_ignoreLineBreaks = false;
	int m_compareMethod = 0;
	QTimer *m_verifyTimer = nullptr; // coalesces typing into one check
	bool m_compareArmed = false; // a compare already ran this cycle
};
