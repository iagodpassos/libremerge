// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTreeWidget>

namespace lm
{

class WildcardDropList;

/**
 * WinMerge's CSubeditList (Src/SubeditList.cpp), the part its pages use
 * here: a list whose cells a click edits once their row has the focus,
 * with read-only columns and columns edited in a WildcardDropList over
 * the cell, its drop-down open, each cell offering its own default
 * patterns.
 */
class SubeditList : public QTreeWidget
{
	Q_OBJECT
public:
	explicit SubeditList(QWidget *parent = nullptr);

	void setReadOnlyColumn(int column);
	/** EditStyle::WILDCARD_DROP_LIST, with the buttons a row of its
	    drop-down holds (the list's are 4). */
	void setWildcardDropListColumn(int column, int columns = 4);
	void setDropListFixedPattern(int row, int column, const QString &fixedPattern);
	QString dropListFixedPattern(int row, int column) const;

	/** EditSubLabelWildcardDropList: the cell's pattern field over it,
	    its drop-down open; the cell takes the field as it closes. */
	void editSubLabel(int row, int column);
	/** The field of the cell being edited, null when none is (for
	    tests). */
	WildcardDropList *editorForTest() const { return m_editor; }

protected:
	void mousePressEvent(QMouseEvent *event) override;

private:
	QSet<int> m_readOnlyColumns;
	int m_wildcardColumn = -1;
	int m_wildcardColumns = 4;
	QHash<QPair<int, int>, QString> m_fixedPatterns;
	QPointer<WildcardDropList> m_editor;
};

} // namespace lm
