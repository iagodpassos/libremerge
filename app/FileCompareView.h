// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <QStringList>
#include <QWidget>
#include <vector>

class QAction;
class QLabel;
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

	/** Mark sides as read-only before compare(): the pane rejects edits
	    and merge operations refuse to target it. */
	void setReadOnlySides(const QList<bool> &readOnly);

	/** Copy the current difference from sourceSide into the merge target
	    (the other side in 2-way mode, the middle pane in 3-way mode). */
	void copyCurrentDiff(int sourceSide);
	void gotoNextDiff();
	void gotoPrevDiff();
	void recompare();
	bool saveModified(QString *error);

signals:
	void modifiedChanged(bool modified);

private:
	struct Block
	{
		int begin[3];
		int end[3]; // inclusive; end < begin means "no lines on this side"
		bool trivial;
	};

	struct WordSpan
	{
		int side;
		int line;      // document line number
		int start;     // UTF-16 offset within the line
		int length;    // UTF-16 length
		int blockIndex;
	};

	struct Side
	{
		QString path;
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
	void computeWordSpans();
	void applyHighlights();
	void updateStatus();
	void gotoDiff(int blockIndex);
	int nextNonTrivial(int from, int direction) const;
	void spliceLines(int side, int firstLine, int lastLine, const QStringList &newLines);
	bool saveSide(int side, QString *error);
	void setSideModified(int side, bool modified);
	void syncScroll(int pane, int value);

	int m_paneCount = 2;
	bool m_readOnly[3] = {};
	DiffTextEdit *m_panes[3];
	std::unique_ptr<SyntaxHighlighter> m_highlighters[3];
	LocationPane *m_locationPane;
	Side m_sides[3];
	QLabel *m_status;
	std::vector<Block> m_blocks;
	std::vector<WordSpan> m_wordSpans;
	int m_diffCount = 0;
	int m_current = -1; // index into m_blocks; -1 = none
	bool m_syncing = false;
	bool m_diffStale = false;
	QAction *m_actSave = nullptr;
	QAction *m_actCopyFromLeft = nullptr;
	QAction *m_actCopyFromRight = nullptr;
};
