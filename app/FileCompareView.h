// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>
#include <vector>

class QAction;
class QLabel;
class QPlainTextEdit;

/**
 * Two-way file comparison and merge view: editable side-by-side panes
 * driven by the engine's CDiffWrapper. Supports difference navigation,
 * copying diff blocks between sides (undoable), free editing with
 * recompare, and saving with the original encoding/EOL preserved.
 */
class FileCompareView : public QWidget
{
	Q_OBJECT
public:
	explicit FileCompareView(QWidget *parent = nullptr);

	/** Load both files and run the initial comparison. */
	bool compare(const QString &leftPath, const QString &rightPath, QString *error);

	bool isModified() const;
	int diffCount() const { return m_diffCount; }

	/** Copy the current difference into the other side. 0 = left→right, 1 = right→left source side. */
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
		int begin[2];
		int end[2]; // inclusive; end < begin means "no lines on this side"
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

	QPlainTextEdit *m_panes[2];
	Side m_sides[2];
	QLabel *m_status;
	std::vector<Block> m_blocks;
	std::vector<WordSpan> m_wordSpans;
	int m_diffCount = 0;
	int m_current = -1; // index into m_blocks; -1 = none
	bool m_syncing = false;
	bool m_diffStale = false;
	QAction *m_actSave = nullptr;
};
