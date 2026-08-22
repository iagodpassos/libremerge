// SPDX-License-Identifier: GPL-3.0-or-later
#include "LocationPane.h"

#include <QMouseEvent>
#include <QPainter>

LocationPane::LocationPane(QWidget *parent)
	: QWidget(parent)
{
	setFixedWidth(26);
	setCursor(Qt::PointingHandCursor);
	setToolTip(tr("Location pane \xE2\x80\x94 click to jump"));
}

void LocationPane::setPaneCount(int count)
{
	m_paneCount = qBound(2, count, 3);
	setFixedWidth(10 + 12 * m_paneCount);
	update();
}

void LocationPane::setBands(std::vector<Band> bands, int totalLines)
{
	m_bands = std::move(bands);
	m_totalLines = qMax(1, totalLines);
	update();
}

void LocationPane::setViewport(int firstVisibleLine, int visibleLines)
{
	m_viewFirst = firstVisibleLine;
	m_viewCount = visibleLines;
	update();
}

void LocationPane::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	const QRect area = rect().adjusted(2, 2, -2, -2);
	painter.fillRect(area, palette().color(QPalette::Base));
	painter.setPen(palette().color(QPalette::Mid));
	painter.drawRect(area);

	const int columnWidth = (area.width() - 1) / m_paneCount;
	const double scale = static_cast<double>(area.height()) / m_totalLines;
	for (const Band &band : m_bands)
	{
		const int y0 = area.top() + static_cast<int>(band.firstLine * scale);
		const int y1 = area.top() + qMax(static_cast<int>((band.lastLine + 1) * scale),
			static_cast<int>(band.firstLine * scale) + 2);
		const int x = area.left() + 1 + band.side * columnWidth;
		painter.fillRect(x, y0, columnWidth - 1, y1 - y0, band.color);
	}

	if (m_viewCount > 0)
	{
		const int y0 = area.top() + static_cast<int>(m_viewFirst * scale);
		const int y1 = area.top() + static_cast<int>((m_viewFirst + m_viewCount) * scale);
		QColor indicator = palette().color(QPalette::Highlight);
		indicator.setAlpha(60);
		painter.fillRect(area.left(), y0, area.width(), qMax(4, y1 - y0), indicator);
		painter.setPen(palette().color(QPalette::Highlight));
		painter.drawRect(area.left(), y0, area.width() - 1, qMax(4, y1 - y0));
	}
}

void LocationPane::mousePressEvent(QMouseEvent *event)
{
	const QRect area = rect().adjusted(2, 2, -2, -2);
	const double ratio = static_cast<double>(event->position().y() - area.top())
		/ qMax(1, area.height());
	emit jumpRequested(qBound(0, static_cast<int>(ratio * m_totalLines), m_totalLines - 1));
}
