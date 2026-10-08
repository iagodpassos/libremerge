// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <QHash>
#include <QKeySequence>
#include <QSet>
#include <QStringList>
#include <QTextDocument>
#include <QWidget>
#include <vector>

#include "FileOps.h"

class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QToolButton;
class QVBoxLayout;
class DiffTextEdit;
class DisplayFilterBar;
class LineFilterHelper;
class LocationPane;
class SyntaxHighlighter;

/**
 * File comparison and merge view for 2 or 3 files: editable side-by-side
 * panes driven by the engine's CDiffWrapper. Supports difference
 * navigation, copying diff blocks between sides (undoable; into the
 * middle pane in 3-way mode), free editing with recompare, and saving
 * with the original encoding/EOL preserved. A display filter, typed in
 * the filter bar or added from a pane's context menu, hides the lines
 * it does not find.
 */
class FileCompareView : public QWidget
{
	Q_OBJECT
public:
	explicit FileCompareView(QWidget *parent = nullptr);
	~FileCompareView() override;

	/** Load 2 or 3 files and run the initial comparison. */
	bool compare(const QStringList &paths, QString *error);

	/** Open an empty, editable 2-way comparison (WinMerge's File > New):
	    paste or type text on both sides and recompare; saving asks for
	    a file name. */
	void startBlank();

	/** Tab title: file names, or the untitled captions. */
	QString tabTitle() const;

	/** Sides with unsaved changes (for the closing dialog). */
	QList<int> modifiedSideIndexes() const;
	/** Display name of a side: its path, or the untitled caption. */
	QString sideLabel(int side) const;
	/** Save one side (untitled sides ask for a name). */
	bool saveSideAt(int side, QString *error);
	bool compare(const QString &leftPath, const QString &rightPath, QString *error)
	{
		return compare(QStringList{ leftPath, rightPath }, error);
	}

	bool isModified() const;
	int diffCount() const { return m_diffCount; }
	/** The differences the compare options ignore (shown in their own
	    color, skipped by the navigation). */
	int ignoredDiffCount() const
	{
		int count = 0;
		for (const Block &block : m_blocks)
			if (block.trivial)
				++count;
		return count;
	}
	/** Some pane's file is in another encoding than the first one's
	    (Unicode form, code page or BOM), as WinMerge's Rescan checks for
	    "Ignore codepage differences". */
	bool encodingsDiffer() const;
	int paneCount() const { return m_paneCount; }
	QStringList paths() const;
	/** Top view line of the first pane (for tests). */
	int firstVisibleViewLine() const;
	/** The view line the pane's cursor is on, and a way to put it
	    there (for tests). */
	int cursorViewLineForTest(int side) const;
	void setCursorViewLineForTest(int side, int viewLine);
	/** Select everything in one pane and copy it (for tests). */
	void selectAllAndCopyForTest(int side);
	/** Type text at the start of a view line, as an undoable user edit
	    (for tests). */
	void typeAtForTest(int side, int viewLine, const QString &text);
	/** The side's real content lines, ghosts excluded (for tests). */
	QStringList realLinesForTest(int side) const
	{
		return collectRealLines(side);
	}
	/** The view lines the differences take, ignored and merged ones left
	    out (for tests). */
	QList<int> diffLinesForTest() const;
	/** Zero-length word spans (insertion markers) on one side (for
	    tests). */
	int insertionMarkersForTest(int side) const
	{
		int count = 0;
		for (const WordSpan &span : m_wordSpans)
			if (span.side == side && span.length == 0)
				++count;
		return count;
	}
	/** Word-level highlights on one side (for tests). */
	int wordSpanCountForTest(int side) const
	{
		int count = 0;
		for (const WordSpan &span : m_wordSpans)
			if (span.side == side)
				++count;
		return count;
	}

	/** Mark sides as read-only before compare(): the pane rejects edits
	    and merge operations refuse to target it. */
	void setReadOnlySides(const QList<bool> &readOnly);
	bool isSideReadOnly(int side) const
	{
		return side >= 0 && side < 3 && m_readOnly[side];
	}

	/** Copy the current difference between explicit panes. Like WinMerge,
	    plain copy stays on the merged spot; pass advance=true for the
	    "copy and advance" variant. */
	void copyCurrentDiff(int sourceSide, int targetSide, bool advance);
	/** Compatibility shim: target is the other side (2-way) or the
	    middle pane (3-way). */
	void copyCurrentDiff(int sourceSide, bool advance = false)
	{
		copyCurrentDiff(sourceSide, mergeTargetFor(sourceSide), advance);
	}
	/** Copy every remaining difference between explicit panes. */
	void copyAllFrom(int sourceSide, int targetSide);
	void copyAllFrom(int sourceSide)
	{
		copyAllFrom(sourceSide, mergeTargetFor(sourceSide));
	}
	/** WinMerge's pane-relative merge commands (MenuIDtoXY): the copy
	    happens between the active pane's neighborhood, so in 3-way the
	    middle pane can push into either side. */
	void copyToRight(bool advance = false);
	void copyToLeft(bool advance = false);
	void copyAllToRight();
	void copyAllToLeft();
	void gotoNextDiff();
	void gotoPrevDiff();
	void gotoFirstDiff();
	void gotoLastDiff();
	/** Scroll horizontally to the first word-level difference of the
	    current block (the "scroll to first inline difference" option). */
	void scrollToFirstInlineDiff();
	/** Swap the outer panes (and reload both sides). */
	void swapSides();
	void undoActive();
	void redoActive();
	/** Select the difference under the active pane's cursor (Alt+Enter,
	    upstream's ID_CURDIFF). */
	void selectDiffAtCursor();
	/** Move the focus to the next pane (F6, upstream's ID_NEXT_PANE). */
	void focusNextPane();
	void zoomIn();
	void zoomOut();
	void zoomReset();
	void showFindBar();
	void findNext(bool backward);
	/** WinMerge's Go To dialog (CMergeEditView::OnWMGoto), opened from a
	    pane, the active one when none is given: a line of a file, or a
	    difference by its number. */
	void showGoTo(int fromPane = -1);
	/** Go to's shortcut, upstream's Ctrl+G; on the Mac the Control key,
	    as Command+G finds the next match there. */
	static QKeySequence goToShortcut();
	void recompare();
	/** Recompare asked for by the user (F5, the Recompare button),
	    followed by rescanned(): WinMerge's OnRefresh, which reports
	    identical files. */
	void refreshByUser();
	/** WinMerge's OnFileReload, past its save prompt: every pane is read
	    from disk again (an untitled one emptied), the undo history goes
	    and the cursor returns to the line it was on. Nothing is touched
	    when a file cannot be read. */
	bool reload(QString *error);
	/** The first pane's file that another application changed since it
	    was loaded or saved here (CheckFileChanged); empty when none. */
	QString changedPathOnDisk() const;
	bool saveModified(QString *error);
	/** Header text override for one pane, like WinMerge's display root
	    for files opened out of an archive (the real path stays in use
	    for loading and saving). */
	void setSideCaption(int side, const QString &caption);
	QString sideCaption(int side) const
	{
		return side >= 0 && side < 3 ? m_sides[side].caption : QString();
	}
	/** WinMerge's file description (strDesc): the header and the tab
	    title show it instead of the file name, as "Original File" in a
	    self-compare. */
	void setSideDescription(int side, const QString &description);

	// --- WinMerge's display filter of the file window: the filter bar
	// (CLineFilterBar) and the filter itself
	// (CMergeDoc::m_displayFilterHelper), a text to find in the lines or a
	// line expression behind "le:". The lines it does not find are hidden
	// in every pane ---

	/** What Ctrl+Shift+L does (CMergeDoc::OnViewDisplayFilterBar): show
	    the bar, the filter in use in its field, and put the keyboard
	    there. */
	void showDisplayFilterBar();
	/** What the View menu item does
	    (CMergeEditFrame::OnViewDisplayFilterBar): show the bar, or close
	    it. The filter stays in use either way. */
	void toggleDisplayFilterBar();
	bool displayFilterBarShown() const { return m_filterBar != nullptr; }
	/** The bar's Apply (CMergeDoc::OnViewDisplayFilterBarApply): the
	    field's filter is the one in use, and the files are compared again
	    as upstream does, which hides the lines. */
	void applyDisplayFilter();
	/** The filter in use, empty when there is none. */
	QString displayFilter() const;
	DisplayFilterBar *displayFilterBarForTest() const { return m_filterBar; }
	/** CMergeDoc::AddToDisplayFilters: the lines must also contain the
	    text; the bar comes up with the filter, which is applied. */
	void addToDisplayFilter(const QString &text);
	/** CMergeEditView::OnAddToDisplayFilters for a pane: its selection
	    when that is within one line, otherwise the word at the cursor. */
	void addSelectionToDisplayFilter(int side);
	/** A pane's context menu with this view's items in it (for tests
	    too). */
	void buildPaneMenu(int side, QMenu *menu);
	/** Whether a view line is hidden, and the texts of the lines a pane
	    shows, ghost lines left out (for tests). */
	bool lineHiddenForTest(int viewLine) const;
	QStringList shownLinesForTest(int side) const;
	int currentDiffForTest() const { return m_current; }
	DiffTextEdit *paneForTest(int side) const { return m_panes[side]; }
	LocationPane *locationPaneForTest() const { return m_locationPane; }
	int activePaneForTest() const { return m_activePane; }

signals:
	/** A recompare asked for by the user is about to run: WinMerge's
	    Rescan first checks the files for changes made elsewhere. */
	void aboutToRescan();
	void rescanned();
	void modifiedChanged(bool modified);
	/** A successful save, with the compared paths and the significant
	    difference count (WinMerge's UpdateChangedItem notification). */
	void fileSaved(const QStringList &paths, int significantDiffs);
	void pathsChanged();
	void optionsRequested();

private:
	class LineProvider;

	struct Block
	{
		int begin[3];
		int end[3]; // inclusive, real lines; end < begin means "no lines on this side"
		bool trivial;
		int op = 0;            // the engine's OP_* of the difference
		bool resolved = false; // merged in place since the last recompare
		// view coordinates (shared by all panes once ghost-aligned)
		int viewBegin = 0;
		int viewEnd = -1;
	};

	struct WordSpan
	{
		int side;
		int line;      // real line number on this side
		int start;     // UTF-16 offset within the line
		int length;    // UTF-16 length
		int blockIndex;
		bool oneSided; // content exists only on this side
	};

	struct Side
	{
		QString path;
		QString caption;     // user override for the header text
		bool described = false; // the caption names the tab too
		int unicoding = 0;   // ucr::UNICODESET
		int codepage = 65001;
		bool bom = false;
		QString eol = QStringLiteral("\n");
		bool hadFinalEol = true;
		bool modified = false;
		lm::FileStamp stamp; // the file as loaded or last saved here
	};

	enum class SaveResult
	{
		Saved,
		Declined, ///< the user kept another application's newer file
		Failed,
	};

	int mergeTargetFor(int sourceSide) const
	{
		return m_paneCount == 3 ? 1 : 1 - sourceSide;
	}

	static bool canLoad(const QString &path, QString *error);
	bool loadSide(int side, const QString &path, QString *error);
	bool runDiff(QString *error);
	void rebuildAlignment();
	void refreshSideMaps(int side);
	QStringList collectRealLines(int side, QList<bool> *ghostFlags = nullptr) const;
	void computeWordSpans();
	void applyHighlights();
	void updateStatus();
	void updateDiffPane();
	void updatePaneStatus(int side);
	void applyTheme();
	void applyZoom(qreal pointSize);
	void updateHeader(int side);
	void updateHeaderStyles();
	void gotoDiff(int blockIndex, bool center = true);
	void selectDiffAtViewLine(int viewLine);
	int nextActive(int from, int direction) const;
	void applyBlockCopy(int blockIndex, int sourceSide, int targetSide,
		bool joinUndo);
	bool applyAlignmentEdits(int side, const QStringList &oldLines,
		const QList<bool> &oldGhosts, const QList<bool> &rawFlags,
		const QStringList &newLines, const QList<bool> &newGhosts);
	void applyGhostFlags(int side, const QList<bool> &flags);
	void tagLastCommand(int side, bool alignment,
		const QList<bool> &flagsBefore, const QList<bool> &flagsAfter);
	void resetUndoHistory();
	void refreshAfterUndoRedo(const int countsBefore[3]);
	void replaceOne();
	void replaceAll();
	void showHeaderMenu(int side);
	void editCaption(int side);
	void changeSideFile(int side, const QString &path);
	SaveResult saveSide(int side, QString *error);
	void setSideModified(int side, bool modified);
	void paneScrolled(int pane);
	void gotoLine(int line, bool realLine, int pane, bool moveAnchor);
	void centerShownLine(int shownLine);
	void showLocationMenu(const QPoint &globalPos, int side, int line);
	void setMovedBlocks(bool detect);
	int viewLineOfShown(int shownLine) const;
	int shownLineOfView(int viewLine) const;
	int realLineOfView(int side, int viewLine) const;
	void syncScroll(int pane, int value);
	void syncHScroll(int pane, int value);
	void hideLines();
	void applyHiddenLines();
	bool blockFiltered(int blockIndex) const;
	bool hasInvisibleLines() const;
	void updateLocationViewport();
	bool findInPane(DiffTextEdit *pane, const QString &needle,
		QTextDocument::FindFlags flags);
	void ensureFilterBar();
	void hideFilterBar();
	void closeDisplayFilterBar();

	int m_paneCount = 2;
	bool m_readOnly[3] = {};
	DiffTextEdit *m_panes[3];
	QLabel *m_headers[3];
	QWidget *m_headerRows[3] = {};
	QToolButton *m_headerButtons[3] = {};
	QLabel *m_posLabels[3];
	QLabel *m_encLabels[3];
	std::unique_ptr<SyntaxHighlighter> m_highlighters[3];
	LocationPane *m_locationPane;
	Side m_sides[3];
	QLabel *m_status;
	std::vector<Block> m_blocks;
	std::vector<WordSpan> m_wordSpans;
	QStringList m_realLines[3];      // side's real lines as of the last diff run
	QSet<int> m_movedLines[3];       // real lines inside moved blocks
	QHash<int, int> m_movedRight;    // the first file's moved lines -> the second's
	std::vector<int> m_realToView[3]; // real line -> view line, ditto
	QList<int> m_lineNumbers[3];      // view line -> 1-based real number, -1 ghost
	struct UndoRef
	{
		int side = 0;
		int viewLine = 0; // where the edit happened, to reselect after undo
		// alignment commands are the ghost-line edits of a recompare:
		// they replay silently around the user's own edits, like
		// WinMerge's rescan (whose ghost operations bypass undo)
		bool alignment = false;
		// a block-copy command: undoing it re-selects the difference,
		// like WinMerge's OnEditUndo (CE_ACTION_MERGE -> OnCurdiff);
		// undone typing selects nothing there
		bool merge = false;
		// ghost flags of the whole pane before/after a programmatic
		// command (alignment or merge); undo/redo restores them, since
		// Qt does not bring block user data back. Empty for user edits.
		QList<bool> flagsBefore;
		QList<bool> flagsAfter;
	};
	QList<UndoRef> m_undoOrder;       // chronological edit order across panes
	QList<UndoRef> m_redoOrder;
	// the display filter, the view lines it hides (as of the last
	// comparison; empty when it hides none) and, while some are hidden,
	// how many lines show before each view line
	std::unique_ptr<LineFilterHelper> m_displayFilter;
	QList<bool> m_hiddenLines;
	std::vector<int> m_shownBefore;
	DisplayFilterBar *m_filterBar = nullptr; // exists while it is shown
	QVBoxLayout *m_layout = nullptr;
	int m_diffCount = 0;
	int m_current = -1; // index into m_blocks; -1 = none
	int m_activePane = 0;
	bool m_syncing = false;
	bool m_diffStale = false;
	bool m_recordingAlignment = false;
	QAction *m_actSave = nullptr;
	QAction *m_actCopyFromLeft = nullptr;
	QAction *m_actCopyFromRight = nullptr;
	QAction *m_actDiffPane = nullptr;
	QWidget *m_diffPaneWidget = nullptr;
	QPlainTextEdit *m_diffPaneEdits[3] = {};
	QWidget *m_findBar = nullptr;
	QLineEdit *m_findEdit = nullptr;
	QLineEdit *m_replaceEdit = nullptr;
	QCheckBox *m_findCase = nullptr;
	QLabel *m_findStatus = nullptr;
};
