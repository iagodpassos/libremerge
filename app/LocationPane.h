// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QWidget>
#include <vector>

/**
 * Miniature map of the whole comparison: one column per side with colored
 * bands where the differences are, plus an indicator for the visible
 * viewport. Clicking jumps to that spot.
 */
class LocationPane : public QWidget
{
	Q_OBJECT
public:
	struct Band
	{
		int side;       // 0 = left, 1 = right
		int firstLine;
		int lastLine;   // inclusive
		QColor color;
	};

	explicit LocationPane(QWidget *parent = nullptr);

	void setBands(std::vector<Band> bands, int totalLines);
	void setViewport(int firstVisibleLine, int visibleLines);

signals:
	void jumpRequested(int line);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;

private:
	std::vector<Band> m_bands;
	int m_totalLines = 1;
	int m_viewFirst = 0;
	int m_viewCount = 0;
};
