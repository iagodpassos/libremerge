// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <vector>
#include <QFutureWatcher>
#include <QHash>
#include <QTemporaryDir>
#include <QWidget>
#include "FolderCompareDriver.h"

class DisplayFilterBar;
class QAction;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

/**
 * Two-way folder comparison view: runs the engine comparison on a worker
 * thread with live progress and cancellation, then shows the recursive
 * result as a hierarchical tree (folders as expandable nodes) or a flat
 * list. It goes by the global file filter as it is when the comparison
 * starts (lm::cloneFileFilter), shown on its status bar, and supports
 * multi-selection copy between sides and delete-to-trash. A display
 * filter, typed in the filter bar or made from a column's header menu,
 * hides items of the result without comparing again.
 * Double-clicking a file that exists on both sides asks the main window
 * to open a file comparison.
 */
class FolderCompareView : public QWidget
{
	Q_OBJECT
public:
	explicit FolderCompareView(QWidget *parent = nullptr);
	~FolderCompareView() override;

	/** Start the comparison asynchronously. */
	void start(const QString &leftDir, const QString &rightDir);
	/** Same, for two or three folders (3-way folder compare). */
	void start(const QStringList &dirs);

	/** A file opened from this comparison was saved: refresh its row,
	    WinMerge's CDirDoc::UpdateChangedItem. */
	void updateSavedItem(const QStringList &paths, int significantDiffs);

	/** Whether the background comparison is still running (for tests). */
	bool isComparingForTest() const { return m_job != nullptr; }
	/** Activate a row by file name, like a double click (for tests). */
	void activateRowForTest(const QString &name);
	/** The category role of a row by file name, -1 if absent (for
	    tests). */
	int rowCategoryForTest(const QString &name) const;
	/** The Result column text of a row by file name (for tests). */
	QString rowResultForTest(const QString &name) const;
	/** The compare method shown on the status bar (for tests). */
	QString compareMethodTextForTest() const;
	/** The file filter this comparison ran with, as the status bar shows
	    it. */
	QString fileFilter() const { return m_fileFilter; }

	/** WinMerge's View menu filters (DirViewFilterSettings): which rows
	    the list shows. Each view keeps its own set, loaded from the saved
	    options; toggling one saves it and redisplays this view. */
	enum ShowFilter
	{
		ShowIdentical,
		ShowDifferent,
		ShowUniqueLeft,
		ShowUniqueMiddle,
		ShowUniqueRight,
		ShowSkipped,
		ShowBinaries,
		ShowDifferentLeftOnly,   ///< 3-way: only the left side differs
		ShowDifferentMiddleOnly,
		ShowDifferentRightOnly,
		ShowMissingLeftOnly,     ///< 3-way: missing on the left only
		ShowMissingMiddleOnly,
		ShowMissingRightOnly,
		ShowFilterCount
	};
	bool showFilter(ShowFilter filter) const { return m_show[filter]; }
	void setShowFilter(ShowFilter filter, bool on);
	static bool savedShowFilter(ShowFilter filter);
	int sideCount() const { return m_sides; }
	/** Apply a filter without saving it (for tests). */
	void setShowFilterForTest(ShowFilter filter, bool on);
	/** Whether a row, by file name, is listed (for tests). */
	bool rowShownForTest(const QString &name) const;
	int hiddenRowsForTest() const { return m_hiddenRows; }
	/** Copy one row between sides without the confirmation (for
	    tests). */
	void copyRowForTest(const QString &name, int sourceSide, int targetSide);
	/** Delete one row's sides without the confirmation (for tests). */
	void deleteRowForTest(const QString &name, const QList<int> &sides);

	// --- WinMerge's display filter: the filter bar (CDirFilterBar) and
	// the filter itself (DirViewFilterSettings::displayFilterHelper), both
	// this window's own ---

	/** What Ctrl+Shift+L does (CDirView::OnViewDisplayFilterBar): show
	    the bar, the filter in use in its field, and put the keyboard
	    there. */
	void showDisplayFilterBar();
	/** What the View menu item does (CDirFrame::OnViewDisplayFilterBar):
	    show the bar, or close it. The filter stays in use either way. */
	void toggleDisplayFilterBar();
	bool displayFilterBarShown() const { return m_filterBar != nullptr; }
	/** The bar's Apply (CDirView::OnViewDisplayFilterBarApply): the
	    field's filter is the one the list is shown by from then on. */
	void applyDisplayFilter();
	/** The filter the list is shown by, empty when there is none. */
	QString displayFilter() const { return m_displayFilter.mask(); }
	DisplayFilterBar *displayFilterBarForTest() const { return m_filterBar; }
	/** A column's name in upstream's registry ("Name", "Path", "Status",
	    "Lsize", "Rmtime"...), which says what filters by it. */
	QString columnRegistryName(int column) const;
	/** The header menu of a column as a right click on it builds it;
	    false when the column has nothing to offer (for tests too). */
	bool buildHeaderMenu(QMenu *menu, int column);

	/** Keep extracted-archive temp trees alive for this view's
	    lifetime (WinMerge's CTempPathContext). */
	void adoptTempDirs(std::vector<std::unique_ptr<QTemporaryDir>> dirs)
	{
		m_tempDirs = std::move(dirs);
	}

public slots:
	void recompare();

signals:
	void openFileComparisonRequested(const QString &leftPath, const QString &rightPath);
	/** The file filter pane was clicked: open Tools > Filters. */
	void filtersRequested();
	/** 3-way activation: the item exists on all three sides. */
	void openFileComparison3Requested(const QStringList &paths);

protected:
	/** Esc stops a running comparison, like CDirView; otherwise it goes
	    up to the window (Options > Close windows with 'Esc'). */
	void keyPressEvent(QKeyEvent *event) override;

private slots:
	void applyTheme();
	void itemActivated(QTreeWidgetItem *item, int column);
	void updateProgress();
	void compareFinished();
	void copySelected(int sourceSide, int targetSide);
	void deleteSelected(const QList<int> &sides);

private:
	void populate(const lm::FolderCompareResult &result);
	void rebuildRows();
	void setupColumns();
	QTreeWidgetItem *findRowByName(const QString &name) const;
	void updateStatusLine();
	QString sideName(int side) const;
	void copyRows(const QList<QTreeWidgetItem *> &rows, int sourceSide,
		int targetSide);
	void deleteRows(const QList<QTreeWidgetItem *> &rows,
		const QList<int> &sides);
	void adjustCategoryCounters(lm::FolderCompareItem::Category from,
		lm::FolderCompareItem::Category to);
	void buildContextMenu(QMenu *menu);
	int colSize(int side) const { return 3 + side; }
	int colDate(int side) const { return 3 + m_sides + side; }
	int colCount() const { return 3 + 2 * m_sides; }
	void fillRow(QTreeWidgetItem *row, const lm::FolderCompareItem &item);
	void setRowCategory(QTreeWidgetItem *row,
		lm::FolderCompareItem::Category category,
		lm::FolderCompareItem::ThreeWayInfo threeWay, bool isDir);
	QTreeWidgetItem *folderNode(const QString &folder,
		QHash<QString, QTreeWidgetItem *> &nodes);
	void updateRowFromDisk(QTreeWidgetItem *row);
	void applyShowFilters();
	bool rowShowable(QTreeWidgetItem *row, bool treeMode,
		bool insideShown) const;
	const lm::FolderCompareItem *resultItem(const QTreeWidgetItem *row) const;
	lm::FolderCompareItem *resultItem(const QTreeWidgetItem *row);
	void ensureFilterBar();
	void hideFilterBar();
	void closeDisplayFilterBar();
	void updateActions();
	QString sidePath(QTreeWidgetItem *row, int side) const;
	QString intendedSidePath(QTreeWidgetItem *row, int side) const;

	QString m_roots[3];
	int m_sides = 2;
	int m_rowsRemoved = 0; // rows dropped since the scan (delete ops)
	bool m_show[ShowFilterCount] = {};
	int m_hiddenRows = 0; // result items the View filters leave out
	std::vector<std::unique_ptr<QTemporaryDir>> m_tempDirs;
	lm::FolderCompareResult m_result;
	lm::FolderDisplayFilter m_displayFilter;
	DisplayFilterBar *m_filterBar = nullptr; // exists while it is shown
	QVBoxLayout *m_layout;
	QTreeWidget *m_tree;
	QLabel *m_status;
	// WinMerge's file filter pane: the filter of the last run
	QToolButton *m_filterButton;
	QString m_fileFilter;
	// WinMerge's compare method pane: the method of the last run, a
	// click away from another one (saved, then recompared)
	QToolButton *m_methodButton;
	int m_compareMethod = 0;
	QProgressBar *m_progress;
	QPushButton *m_cancelButton;
	QTimer *m_progressTimer;
	QAction *m_actTreeMode;
	QAction *m_actCopyRight;
	QAction *m_actCopyLeft;
	QAction *m_actDeleteLeft;
	QAction *m_actDeleteRight;
	QAction *m_actDeleteBoth;
	std::shared_ptr<lm::FolderCompareJob> m_job;
	QFutureWatcher<lm::FolderCompareResult> m_watcher;
};
