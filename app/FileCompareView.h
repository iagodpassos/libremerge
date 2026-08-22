// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <QStringList>
#include <QWidget>
#include <vector>

class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;
class DiffTextEdit;
class LocationPane;
class SyntaxHighlighter;

/**
 * File comparison and merge view for 2 or 3 files: editable side-by-side
 * panes driven by the engine's CDiffWrapper. Supports difference
 * navigation, copying diff blocks between sides (undoable; into the
 * middle pane in 3-way mode), free editing with recompare, and saving
 * with the original encoding/EOL preserved.
 */
class FileCompareView : public QWidget
{
	Q_OBJECT
public:
	explicit FileCompareView(QWidget *parent = nullptr);
	~FileCompareView() override;

	/** Load 2 or 3 files and run the initial comparison. */
	bool compare(const QStringList &paths, QString *error);
	bool compare(const QString &leftPath, const QString &rightPath, QString *error)
	{
		return compare(QStringList{ leftPath, rightPath }, error);
	}

	bool isModified() const;
	int diffCount() const { return m_diffCount; }
	int paneCount() const { return m_paneCount; }
	QStringList paths() const;

	/** Mark sides as read-only before compare(): the pane rejects edits
	    and merge operations refuse to target it. */
	void setReadOnlySides(const QList<bool> &readOnly);

	/** Copy the current difference from sourceSide into the merge target
	    (the other side in 2-way mode, the middle pane in 3-way mode). */
	void copyCurrentDiff(int sourceSide);
	/** Copy every remaining difference from sourceSide at once. */
	void copyAllFrom(int sourceSide);
	void gotoNextDiff();
	void gotoPrevDiff();
	void gotoFirstDiff();
	void gotoLastDiff();
	/** Swap the outer panes (and reload both sides). */
	void swapSides();
	void undoActive();
	void redoActive();
	void showFindBar();
	void findNext(bool backward);
	void recompare();
	bool saveModified(QString *error);

signals:
	void modifiedChanged(bool modified);
	void pathsChanged();
	void optionsRequested();

private:
	struct Block
	{
		int begin[3];
		int end[3]; // inclusive, real lines; end < begin means "no lines on this side"
		bool trivial;
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
		int unicoding = 0;   // ucr::UNICODESET
		int codepage = 65001;
		bool bom = false;
		QString eol = QStringLiteral("\n");
		bool hadFinalEol = true;
		bool modified = false;
	};

	int mergeTarget(int sourceSide) const
	{
		return m_paneCount == 3 ? 1 : 1 - sourceSide;
	}

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
	void updateHeader(int side);
	void updateHeaderStyles();
	void gotoDiff(int blockIndex);
	int nextActive(int from, int direction) const;
	void applyBlockCopy(int blockIndex, int sourceSide, bool joinUndo);
	void replaceOne();
	void replaceAll();
	void showHeaderMenu(int side);
	void editCaption(int side);
	void changeSideFile(int side, const QString &path);
	bool saveSide(int side, QString *error);
	void setSideModified(int side, bool modified);
	void syncScroll(int pane, int value);
	void syncHScroll(int pane, int value);

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
	std::vector<int> m_realToView[3]; // real line -> view line, ditto
	QList<int> m_lineNumbers[3];      // view line -> 1-based real number, -1 ghost
	int m_diffCount = 0;
	int m_current = -1; // index into m_blocks; -1 = none
	int m_activePane = 0;
	bool m_syncing = false;
	bool m_diffStale = false;
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
