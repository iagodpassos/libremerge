// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSet>
#include <QStringList>
#include <QWidget>
#include <vector>

class QAction;
class QLabel;
class QTableView;
class TableSideModel;

/**
 * Two-way CSV/TSV comparison as side-by-side grids (WinMerge's table
 * compare): the same line diff engine drives the alignment, rows in a
 * difference are colored, and the individual cells that differ are
 * emphasized. Blocks can be copied between sides and saved back with
 * the original encoding and line endings.
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
	QStringList paths() const;
	QString tabTitle() const;
	bool saveModified(QString *error);

	void gotoFirstDiff();
	void gotoNextDiff();
	void gotoPrevDiff();
	void gotoLastDiff();
	void selectDiffAtCursor();
	void copyCurrentDiff(int sourceSide);
	void copyAllFrom(int sourceSide);
	void swapSides();
	void focusNextPane();
	void recompare();

signals:
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
		QStringList rawLines;                 // file content, one per row
		std::vector<QStringList> cells;       // parsed per row
	};

	bool loadSide(int side, const QString &path, QString *error);
	bool runDiff(QString *error);
	void rebuildModel();
	void computeCellDiffs();
	void updateStatus();
	void gotoDiff(int blockIndex);
	void selectDiffAtModelRow(int modelRow);
	int nextActive(int from, int direction) const;
	void applyTheme();
	void setSideModified(int side, bool modified);

	QChar m_delimiter = QChar(',');
	bool m_firstRowIsHeader = true;
	Side m_sides[2];
	std::vector<Block> m_blocks;
	// view row -> real row per side (-1 = ghost)
	QList<int> m_viewToReal[2];
	// "row,column" pairs whose cells differ between the sides
	QSet<quint64> m_cellDiffs[2];
	QTableView *m_tables[2];
	TableSideModel *m_models[2];
	QLabel *m_status;
	QAction *m_actSave = nullptr;
	QAction *m_actHeader = nullptr;
	int m_diffCount = 0;
	int m_current = -1;
	bool m_syncing = false;

	friend class TableSideModel;
};
