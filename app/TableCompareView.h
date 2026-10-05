// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <QSet>
#include <QStringList>
#include <QWidget>
#include <vector>

#include "FileOps.h"

class DisplayFilterBar;
class LineFilterHelper;
class QAction;
class QLabel;
class QMenu;
class QTableView;
class QVBoxLayout;
class TableSideModel;

/**
 * Two-way CSV/TSV comparison as side-by-side grids (WinMerge's table
 * compare): the same line diff engine drives the alignment, rows in a
 * difference are colored, and the individual cells that differ are
 * emphasized. Blocks can be copied between sides and saved back with
 * the original encoding and line endings. WinMerge's table is its text
 * window in another dress, so the display filter bar is here as well,
 * with rows for lines and the cells of a row for a filter's columns.
 */
class TableCompareView : public QWidget
{
	Q_OBJECT
public:
	explicit TableCompareView(QWidget *parent = nullptr);
	~TableCompareView() override;

	bool compare(const QString &leftPath, const QString &rightPath,
		QString *error);

	bool isModified() const;
	int diffCount() const { return m_diffCount; }
	/** The rows the differences take, the header's among them and ignored
	    differences left out (for tests). */
	QList<int> diffRowsForTest() const;
	/** The two files are in different encodings (Unicode form, code
	    page or BOM), as WinMerge's Rescan checks for "Ignore codepage
	    differences". */
	bool encodingsDiffer() const;
	QStringList paths() const;
	QString tabTitle() const;
	/** WinMerge's file description (strDesc): the tab title shows it
	    instead of the file name, as "Original File" in a self-compare. */
	void setSideDescription(int side, const QString &description);
	bool saveModified(QString *error);

	void gotoFirstDiff();
	void gotoNextDiff();
	void gotoPrevDiff();
	void gotoLastDiff();
	void selectDiffAtCursor();
	void copyCurrentDiff(int sourceSide);
	void copyAllFrom(int sourceSide);
	void undo();
	void redo();
	void swapSides();
	void focusNextPane();
	void recompare();
	/** Recompare asked for by the user (F5, the Recompare button),
	    followed by rescanned(): WinMerge's OnRefresh, which reports
	    identical files. */
	void refreshByUser();
	/** WinMerge's OnFileReload, past its save prompt: both files are
	    read from disk again, the undo history goes and the current row
	    is selected again. Nothing is touched when a file cannot be
	    read. */
	bool reload(QString *error);
	/** The first side's file that another application changed since it
	    was loaded or saved here (CheckFileChanged); empty when none. */
	QString changedPathOnDisk() const;

	/** WinMerge's display filter bar, as in the text compare: the rows a
	    line filter does not hold for are hidden on both sides. The View
	    menu's item shows the bar or closes it, its shortcut only shows it;
	    the filter applied stays in use without the bar. */
	void showDisplayFilterBar();
	void toggleDisplayFilterBar();
	bool displayFilterBarShown() const { return m_filterBar != nullptr; }
	/** CMergeDoc::OnViewDisplayFilterBarApply: the bar's field becomes the
	    filter and the files are compared again. */
	void applyDisplayFilter();
	QString displayFilter() const;
	/** The header's context menu (upstream's IDR_POPUP_MERGEVIEWHEADER)
	    for a column of one side, -1 for a click past the last one. */
	void buildHeaderMenu(int side, int column, QMenu *menu);
	/** CMergeDoc::AddColumnToDisplayFilters: ask for a condition on a
	    column of one side, by its text (0), as a number (1) or as a date
	    and time (2), and apply the filter with it. */
	void addColumnToDisplayFilter(int side, int column, int dataType);
	/** Upstream's "Auto-Fit All Columns": every column as wide as its
	    widest cell on either side. */
	void autoFitColumns();
	DisplayFilterBar *displayFilterBarForTest() const { return m_filterBar; }
	QTableView *tableForTest(int side) const { return m_tables[side]; }
	int currentDiffForTest() const { return m_current; }
	/** The lines of the rows on show, the header's row left out, an empty
	    one for a row the side does not have (for tests). */
	QStringList shownRowsForTest(int side) const;

signals:
	/** A recompare asked for by the user is about to run: WinMerge's
	    Rescan first checks the files for changes made elsewhere. */
	void aboutToRescan();
	void rescanned();
	/** A successful save, with the compared paths and the current
	    difference count (for folder-comparison row updates). */
	void fileSaved(const QStringList &paths, int significantDiffs);
	void modifiedChanged(bool modified);
	void pathsChanged();
	void openAsTextRequested(const QString &leftPath, const QString &rightPath);

private:
	struct Block
	{
		int begin[2];
		int end[2];   // inclusive, real rows; end < begin = empty side
		bool trivial;
		int viewBegin = 0;
		int viewEnd = -1;
	};

	struct Side
	{
		QString path;
		int unicoding = 0;
		int codepage = 65001;
		bool bom = false;
		QString eol = QStringLiteral("\n");
		bool hadFinalEol = true;
		bool modified = false;
		lm::FileStamp stamp;                  // the file as loaded or last saved
		QStringList rawLines;                 // file content, one per row
		std::vector<QStringList> cells;       // parsed per row
	};

	// one side's content before a block copy; undo/redo swap the live
	// state with these snapshots (QStringList shares the row strings, so
	// each entry is cheap)
	struct UndoEntry
	{
		int side;
		QStringList lines;
		bool modified;
		int saveSerial;   // invalid once the side is saved again
		int viewBegin;    // where the change was, to reselect
	};

	class LineProvider;

	bool loadSide(int side, const QString &path, QString *error);
	bool runDiff(QString *error);
	void rebuildModel();
	void hideLines();
	bool blockFiltered(int blockIndex) const;
	bool hasInvisibleLines() const;
	bool endsShownStretch(int viewRow) const;
	int modelRowOf(int viewRow) const;
	void ensureFilterBar();
	void hideFilterBar();
	void closeDisplayFilterBar();
	void computeCellDiffs();
	void updateStatus();
	void gotoDiff(int blockIndex);
	void selectDiffAtModelRow(int modelRow);
	int nextActive(int from, int direction) const;
	UndoEntry captureEntry(int side, int viewBegin) const;
	void applyEntry(const UndoEntry &entry);
	void pushUndo(const UndoEntry &entry);
	void clearHistory();
	void updateUndoActions();
	void applyTheme();
	void setSideModified(int side, bool modified);

	QChar m_delimiter = QChar(',');
	bool m_firstRowIsHeader = true;
	QString m_descriptions[2];
	Side m_sides[2];
	std::vector<Block> m_blocks;
	// view row -> real row per side (-1 = ghost)
	QList<int> m_viewToReal[2];
	// the display filter: the view rows it hides (empty when none), and
	// the rows the grids show, in order, as view rows
	std::unique_ptr<LineFilterHelper> m_displayFilter;
	QList<bool> m_hiddenRows;
	QList<int> m_modelRows;
	DisplayFilterBar *m_filterBar = nullptr;
	QVBoxLayout *m_layout = nullptr;
	// "row,column" pairs whose cells differ between the sides
	QSet<quint64> m_cellDiffs[2];
	QTableView *m_tables[2];
	TableSideModel *m_models[2];
	QLabel *m_status;
	QAction *m_actSave = nullptr;
	QAction *m_actHeader = nullptr;
	QAction *m_actUndo = nullptr;
	QAction *m_actRedo = nullptr;
	QList<UndoEntry> m_undoStack;
	QList<UndoEntry> m_redoStack;
	int m_saveSerial[2] = { 0, 0 };
	int m_diffCount = 0;
	int m_current = -1;
	bool m_syncing = false;

	friend class TableSideModel;
};
