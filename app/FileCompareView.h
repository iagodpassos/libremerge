// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>
#include <vector>

class QPlainTextEdit;
class QLabel;

/**
 * Two-way file comparison view (v0): side-by-side read-only panes with
 * diff-block highlighting and synchronized scrolling, driven by the
 * engine's CDiffWrapper. Inline editing and merge operations are the
 * next Phase 1 milestones.
 */
class FileCompareView : public QWidget
{
	Q_OBJECT
public:
	explicit FileCompareView(QWidget *parent = nullptr);

	/** Run the comparison; false + error message on failure. */
	bool compare(const QString &leftPath, const QString &rightPath, QString *error);

	int diffCount() const { return m_diffCount; }

private:
	struct Block
	{
		int begin[2];
		int end[2]; // inclusive; end < begin means "no lines on this side"
		bool trivial;
	};

	void applyHighlights();
	void syncScroll(int pane, int value);

	QPlainTextEdit *m_panes[2];
	QLabel *m_status;
	std::vector<Block> m_blocks;
	int m_diffCount = 0;
	bool m_syncing = false;
};
