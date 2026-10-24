// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <QSet>
#include <QStringList>
#include <QWidget>
#include <vector>

#include "FileOps.h"
#include "TableFiles.h"

class DisplayFilterBar;
class FilepathEdit;
class LineFilterHelper;
class QAction;
class QLabel;
class QMenu;
class QTableView;
class QVBoxLayout;
class TableSideModel;

/**
 * Two- or three-way CSV/TSV comparison as side-by-side grids (WinMerge's
 * table compare): the same line diff engine drives the alignment, rows in
 * a difference are colored, and the individual cells that differ are
 * emphasized. Blocks can be copied between sides and saved back with
 * the original encoding and line endings. WinMerge's table is its text
 * window in another dress, so the display filter bar is here as well,
 * with rows for lines and the cells of a row for a filter's columns, and
 * three files compare in three grids, as three texts do there.
 */
class TableCompareView : public QWidget
{
	Q_OBJECT
public:
	explicit TableCompareView(QWidget *parent = nullptr);
	~TableCompareView() override;

	/** Load the two or three files and compare them; an empty path is an
	    untitled, empty side. */
	bool compare(const QStringList &paths, QString *error);
	bool compare(const QString &leftPath, const QString &rightPath,
		QString *error) { return compare(QStringList{ leftPath, rightPath }, error); }
	int paneCount() const { return m_paneCount; }

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
	/** WinMerge's ChangeFile, which the path bar calls: save what was
	    changed (after asking), then open the files again with another
	    one on one side. */
	bool changeFile(int side, const QString &path, const QString &description);
	/** How the changes are saved before (PromptAndSaveIfNeeded); false
	    cancels. */
	void setSavePrompt(std::function<bool()> prompt) { m_savePrompt = std::move(prompt); }
	/** Ctrl+L: edit the active side's path in its bar. */
	void editActivePanePath();
	FilepathEdit *headerForTest(int side) const { return m_headers[side]; }
	bool saveModified(QString *error);

	void gotoFirstDiff();
	void gotoNextDiff();
	void gotoPrevDiff();
	void gotoLastDiff();
	void selectDiffAtCursor();
	/** Copy the current difference from one side to another: the other
	    side of a two-way comparison when none is named. */
	void copyCurrentDiff(int sourceSide, int targetSide = -1);
	void copyAllFrom(int sourceSide, int targetSide = -1);
	/** WinMerge's ID_L2R and ID_R2L: relative to the active grid (its
	    MenuIDtoXY), so the middle one of three pushes into either side. */
	void copyToRight();
	void copyToLeft();
	/** Upstream's typing in its table: a cell of a grid's row takes a new
	    value, only that cell of the line written anew (a row a side does
	    not have becomes one of its file), into the undo history, and the
	    tables are compared again. */
	void editCell(int side, int modelRow, int column, const QString &text);
	/** The Copy All commands: "Copy all differences to other file?"
	    first, as upstream's table view (its text view) asks. */
	void copyAllToRight();
	void copyAllToLeft();
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
	/** CMergeDoc::RefreshOptions: the comparison takes the options as
	    they are, a Yes to the question about line endings going. */
	void refreshDiffOptions() { m_ignoreEol = false; }
	/** The command line's table settings (-table-delimiter,
	    -table-quote, -table-allownewlinesinquotes), over the Table
	    options' for every side, from the next compare on. */
	void setTableOverrides(std::optional<QChar> delimiter, std::optional<QChar> quote,
		std::optional<bool> allowNewlinesInQuotes);
	/** Where a side is saved in place of its file (the command line's
	    -o); the side goes on with that file once saved. */
	void setSaveAsPath(const QString &path) { m_saveAsPath = path; }
	/** Whether a side's file ends its rows in more than one way, and
	    whether this comparison treats line endings as the same (for
	    tests). */
	bool mixedEolForTest(int side) const { return m_sides[side].mixedEol; }
	bool ignoresEolForTest() const { return m_ignoreEol; }
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
	/** CMergeDoc::IgnoreColumnInComparison: a column left out of the
	    comparison by a ColumnRange("!N") line expression in the prediffer,
	    for every file (an empty prefix) or for the files of the prefix
	    ("2" the second); add keeps the columns left out before, as a
	    Ctrl+Click on the menu item does, and the files compare again. */
	void ignoreColumnInComparison(int column, const QString &panePrefix, bool add);
	/** CMergeDoc::ResetIgnoredColumnsInComparison: those expressions go
	    from the prediffer, and the files compare again. */
	void resetIgnoredColumnsInComparison();
	/** The comparison's prediffer pipeline (for tests). */
	QString prediffPipelineForTest() const { return m_prediffPipeline; }
	DisplayFilterBar *displayFilterBarForTest() const { return m_filterBar; }
	QTableView *tableForTest(int side) const { return m_tables[side]; }
	int currentDiffForTest() const { return m_current; }
	/** The lines of the rows on show, the header's row left out, an empty
	    one for a row the side does not have (for tests). */
	QStringList shownRowsForTest(int side) const;
	/** A cell of a grid's row is one of those that differ (for tests). */
	bool cellDiffersForTest(int side, int modelRow, int column) const;

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
	void openAsTextRequested(const QStringList &paths);

private:
	struct Block
	{
		int begin[3];
		int end[3];   // inclusive, real rows; end < begin = empty side
		bool trivial;
		int op = 0;   // the engine's OP_* (which side of three differs)
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
		/** The rows end in more than one way (upstream's
		    CRLFSTYLE::MIXED): each keeps its own ending. */
		bool mixedEol = false;
		bool hadFinalEol = true;
		bool modified = false;
		lm::FileStamp stamp;                  // the file as loaded or last saved
		lm::TableProps props;                 // delimiter and quote, by the Table options
		QStringList rawLines;                 // file content, one per row
		QStringList rowEols;                  // each row's line ending, beside it
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
		int realRow = -1; // an edited cell's row and column, to go back to
		int column = -1;
		QStringList eols; // the rows' line endings
	};

	class LineProvider;

	bool loadSide(int side, const QString &path, const lm::TableProps &props,
		QString *error);
	/** The rows a cell of several lines makes taller, as tall on both sides. */
	void fitRowHeights();
	bool runDiff(QString *error);
	/** The line ending a row is saved with: its own in a file of mixed
	    endings, the file's otherwise. */
	QString rowEol(int side, int row) const;
	/** CMergeDoc::OpenDocs' question about files whose lines end
	    otherwise. */
	void suggestIgnoreEol();
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
	void scrollDiffIntoView(int blockIndex);
	void selectDiffAtModelRow(int modelRow);
	int nextActive(int from, int direction) const;
	/** The grid the commands act on: the one with the focus, the one
	    whose grid or bar had it last otherwise. */
	int activeSide() const;
	void setActiveSide(int side);
	void updateHeader(int side);
	/** A side without a file: upstream's "Untitled" description. */
	QString untitledName(int side) const;
	int nextActiveFromRow(int viewRow, int direction) const;
	int diffAtRow(int viewRow) const;
	int searchRow(int offset) const;
	bool diffOnScreen(int blockIndex) const;
	void deselectDiffIfCursorNotInCurrentDiff(int modelRow);
	UndoEntry captureEntry(int side, int viewBegin) const;
	/** The copy of the selected cells' differences, when two or more
	    cells are selected; false when the copy is the whole difference's. */
	bool copySelectedCells(int sourceSide, int targetSide);
	/** A side's row and column current in its grid, as the comparison
	    shows it now. */
	void showCell(int side, int realRow, int column);
	void applyEntry(const UndoEntry &entry);
	void pushUndo(const UndoEntry &entry);
	void clearHistory();
	void updateUndoActions();
	void applyTheme();
	void setSideModified(int side, bool modified);

	bool m_firstRowIsHeader = true;
	int m_paneCount = 2;
	QString m_descriptions[3];
	// a side's column: its path bar over its grid, hidden for a third side
	// a two-way comparison has not
	QWidget *m_columns[3] = {};
	// WinMerge's path bar over each side, and what it was last given
	FilepathEdit *m_headers[3] = {};
	QString m_headerTexts[3];
	int m_activeSide = 0;
	std::function<bool()> m_savePrompt;
	Side m_sides[3];
	// the document's prediffer (CMergeDoc's), where the columns "Ignore
	// This Column" leaves out are
	QString m_prediffPipeline;
	std::vector<Block> m_blocks;
	// view row -> real row per side (-1 = ghost)
	QList<int> m_viewToReal[3];
	// the display filter: the view rows it hides (empty when none), and
	// the rows the grids show, in order, as view rows
	std::unique_ptr<LineFilterHelper> m_displayFilter;
	QList<bool> m_hiddenRows;
	QList<int> m_modelRows;
	DisplayFilterBar *m_filterBar = nullptr;
	QVBoxLayout *m_layout = nullptr;
	// "row,column" pairs whose cells differ between the sides
	QSet<quint64> m_cellDiffs[3];
	QTableView *m_tables[3];
	TableSideModel *m_models[3];
	QLabel *m_status;
	QAction *m_actSave = nullptr;
	QAction *m_actHeader = nullptr;
	QAction *m_actUndo = nullptr;
	QAction *m_actRedo = nullptr;
	QList<UndoEntry> m_undoStack;
	QList<UndoEntry> m_redoStack;
	int m_saveSerial[3] = { 0, 0, 0 };
	int m_diffCount = 0;
	int m_current = -1;
	bool m_syncing = false;
	bool m_ignoreEol = false; // Yes to the question, for this comparison
	std::optional<QChar> m_delimiterOverride;
	std::optional<QChar> m_quoteOverride;
	std::optional<bool> m_newlinesOverride;
	QString m_saveAsPath; // where a side is saved, by the command line

	friend class TableSideModel;
};
