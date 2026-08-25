// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "ImagePane.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include "Theme.h"

#include "ImgMergeBuffer.hpp"

ImagePane::ImagePane(QWidget *parent)
	: QAbstractScrollArea(parent)
{
	setMouseTracking(true);
	viewport()->setMouseTracking(true);
	setFocusPolicy(Qt::ClickFocus);
	// WinIMerge keeps the bars visible but disabled when not needed
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	horizontalScrollBar()->setSingleStep(1);
	verticalScrollBar()->setSingleStep(1);
}

void ImagePane::setBuffer(CImgMergeBuffer *buffer, int pane)
{
	m_buffer = buffer;
	m_pane = pane;
	refreshImage();
}

void ImagePane::refreshImage()
{
	m_image = QImage();
	if (m_buffer != nullptr && m_pane < m_buffer->GetPaneCount())
	{
		if (const Image *img = m_buffer->GetImage(m_pane))
			m_image = img->qimage();
	}
	updateScrollBars();
	viewport()->update();
}

void ImagePane::setZoom(double zoom)
{
	const double oldZoom = m_zoom;
	m_zoom = qMax(zoom, 0.1); // CImgWindow floor
	if (m_zoom == oldZoom)
		return;
	// proportional rescale keeps the top-left anchor (SetZoom parity)
	horizontalScrollBar()->setValue(
		static_cast<int>(horizontalScrollBar()->value() / oldZoom * m_zoom));
	verticalScrollBar()->setValue(
		static_cast<int>(verticalScrollBar()->value() / oldZoom * m_zoom));
	updateScrollBars();
	viewport()->update();
}

void ImagePane::setUseBackColor(bool use, const QColor &color)
{
	m_useBackColor = use;
	m_backColor = color;
	viewport()->update();
}

QPoint ImagePane::convertDPtoLP(const QPoint &dp) const
{
	const QRect rc = viewport()->rect();
	const int w = m_image.width();
	const int h = m_image.height();
	QPoint lp;
	if (rc.width() < w * m_zoom + Margin * 2)
		lp.setX(static_cast<int>(
			(dp.x() - Margin + horizontalScrollBar()->value()) / m_zoom));
	else
		lp.setX(static_cast<int>(
			(dp.x() - (rc.width() / 2 - (w / 2) * m_zoom)) / m_zoom));
	if (rc.height() < h * m_zoom + Margin * 2)
		lp.setY(static_cast<int>(
			(dp.y() - Margin + verticalScrollBar()->value()) / m_zoom));
	else
		lp.setY(static_cast<int>(
			(dp.y() - (rc.height() / 2 - (h / 2) * m_zoom)) / m_zoom));
	return lp;
}

QPoint ImagePane::convertLPtoDP(const QPoint &lp) const
{
	const QRect rc = viewport()->rect();
	const int w = m_image.width();
	const int h = m_image.height();
	QPoint dp;
	if (rc.width() > w * m_zoom + Margin * 2)
		dp.setX(static_cast<int>((rc.width() - w * m_zoom) / 2));
	else
		dp.setX(-horizontalScrollBar()->value() + Margin);
	if (rc.height() > h * m_zoom + Margin * 2)
		dp.setY(static_cast<int>((rc.height() - h * m_zoom) / 2));
	else
		dp.setY(-verticalScrollBar()->value() + Margin);
	dp += QPoint(static_cast<int>(lp.x() * m_zoom),
		static_cast<int>(lp.y() * m_zoom));
	return dp;
}

QPoint ImagePane::cursorImagePos() const
{
	return convertDPtoLP(viewport()->mapFromGlobal(QCursor::pos()));
}

void ImagePane::scrollTo(int x, int y, bool force)
{
	const QRect rc = viewport()->rect();
	const int w = m_image.width();
	const int h = m_image.height();
	if (rc.width() < w * m_zoom + Margin * 2)
	{
		QScrollBar *bar = horizontalScrollBar();
		const double dx = x * m_zoom + Margin;
		if (force || dx < bar->value() || bar->value() + rc.width() < dx)
			bar->setValue(static_cast<int>(dx - rc.width() / 2));
	}
	if (rc.height() < h * m_zoom + Margin * 2)
	{
		QScrollBar *bar = verticalScrollBar();
		const double dy = y * m_zoom + Margin;
		if (force || dy < bar->value() || bar->value() + rc.height() < dy)
			bar->setValue(static_cast<int>(dy - rc.height() / 2));
	}
	viewport()->update();
}

void ImagePane::scrollTo2(int lx, int ly, int dx, int dy)
{
	horizontalScrollBar()->setValue(
		static_cast<int>(lx * m_zoom + Margin - dx));
	verticalScrollBar()->setValue(
		static_cast<int>(ly * m_zoom + Margin - dy));
	viewport()->update();
}

void ImagePane::setSelectionStart(const QPoint &pt, bool clamp)
{
	m_selStart = m_selEnd = clamp
		? QPoint(qBound(0, pt.x(), m_image.width()),
			qBound(0, pt.y(), m_image.height()))
		: pt;
	m_selectionVisible = true;
	viewport()->update();
}

void ImagePane::setSelectionEnd(const QPoint &pt, bool clamp)
{
	m_selEnd = clamp
		? QPoint(qBound(0, pt.x(), m_image.width()),
			qBound(0, pt.y(), m_image.height()))
		: pt;
	m_selectionVisible = true;
	viewport()->update();
}

QRect ImagePane::selection() const
{
	// half-open pixel box like GetRectangleSelection: width/height are
	// (max - min), so a click without drag yields an empty rect
	if (!m_selectionVisible)
		return QRect();
	const QPoint topLeft(qMin(m_selStart.x(), m_selEnd.x()),
		qMin(m_selStart.y(), m_selEnd.y()));
	return QRect(topLeft, QSize(
		qMax(m_selStart.x(), m_selEnd.x()) - topLeft.x(),
		qMax(m_selStart.y(), m_selEnd.y()) - topLeft.y()));
}

void ImagePane::clearSelection()
{
	m_selectionVisible = false;
	m_selStart = m_selEnd = QPoint();
	viewport()->update();
}

void ImagePane::startFloatingImage(const QImage &image, const QPoint &pos,
	const QPoint &cursor)
{
	m_floating = image;
	m_floatingPos = pos;
	m_floatingCursor = cursor;
	viewport()->update();
}

void ImagePane::restartFloatingDrag(const QPoint &cursor)
{
	m_floatingCursor = cursor;
}

void ImagePane::dragFloatingImage(const QPoint &cursor)
{
	m_floatingPos += cursor - m_floatingCursor;
	m_floatingCursor = cursor;
	viewport()->update();
}

QRect ImagePane::floatingRect() const
{
	if (m_floating.isNull())
		return QRect();
	return QRect(m_floatingPos, m_floating.size());
}

void ImagePane::clearFloatingImage()
{
	m_floating = QImage();
	viewport()->update();
}

void ImagePane::updateScrollBars()
{
	const int contentW =
		static_cast<int>(m_image.width() * m_zoom) + Margin * 2;
	const int contentH =
		static_cast<int>(m_image.height() * m_zoom) + Margin * 2;
	horizontalScrollBar()->setRange(0,
		qMax(0, contentW - viewport()->width()));
	horizontalScrollBar()->setPageStep(viewport()->width());
	verticalScrollBar()->setRange(0,
		qMax(0, contentH - viewport()->height()));
	verticalScrollBar()->setPageStep(viewport()->height());
}

void ImagePane::paintEvent(QPaintEvent *)
{
	QPainter painter(viewport());

	// flat background (CImgWindow colors)
	const bool dark = lm::Theme::instance()->dark();
	painter.fillRect(viewport()->rect(),
		dark ? QColor(40, 40, 60) : QColor(206, 215, 230));

	if (m_image.isNull())
		return;

	const QPoint origin = convertLPtoDP(QPoint(0, 0));
	const QRect target(origin,
		QSize(static_cast<int>(m_image.width() * m_zoom),
			static_cast<int>(m_image.height() * m_zoom)));

	// behind transparent pixels: solid back color, or the classic
	// checkerboard (FreeImage's 8 px 0x99/0x66 cells, unscaled)
	if (m_image.hasAlphaChannel())
	{
		const QRect visible = target.intersected(viewport()->rect());
		if (m_useBackColor)
			painter.fillRect(visible, m_backColor);
		else
		{
			painter.setClipRect(visible);
			const QColor light(0x99, 0x99, 0x99), darkCell(0x66, 0x66, 0x66);
			for (int y = visible.top() - (visible.top() - target.top()) % 16;
				y < visible.bottom() + 16; y += 8)
			{
				for (int x = visible.left()
						- (visible.left() - target.left()) % 16;
					x < visible.right() + 16; x += 8)
				{
					const bool odd =
						(((x - target.left()) / 8) + ((y - target.top()) / 8)) & 1;
					painter.fillRect(QRect(x, y, 8, 8), odd ? darkCell : light);
				}
			}
			painter.setClipping(false);
		}
	}

	// nearest-neighbour scaling, drawing only the visible part so huge
	// zoom factors stay cheap (CImgWindow's GDI-overflow guard)
	painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
	const QRect visibleTarget = target.intersected(viewport()->rect()
		.adjusted(-static_cast<int>(m_zoom) - 1, -static_cast<int>(m_zoom) - 1,
			static_cast<int>(m_zoom) + 1, static_cast<int>(m_zoom) + 1));
	if (!visibleTarget.isEmpty())
	{
		const QRectF source(
			(visibleTarget.left() - target.left()) / m_zoom,
			(visibleTarget.top() - target.top()) / m_zoom,
			visibleTarget.width() / m_zoom,
			visibleTarget.height() / m_zoom);
		painter.drawImage(QRectF(visibleTarget), m_image, source);
	}

	// floating (pasted) image + its dashed frame
	if (!m_floating.isNull())
	{
		const QPoint fOrigin = convertLPtoDP(m_floatingPos);
		const QRect fTarget(fOrigin,
			QSize(static_cast<int>(m_floating.width() * m_zoom),
				static_cast<int>(m_floating.height() * m_zoom)));
		painter.drawImage(QRectF(fTarget), m_floating,
			QRectF(m_floating.rect()));
		drawDashedXorRect(painter, fTarget);
	}

	// rectangle selection (marching-ants style dashes; degenerate rect
	// draws the wipe guide line)
	if (m_selectionVisible)
	{
		const QPoint a = convertLPtoDP(m_selStart);
		const QPoint b = convertLPtoDP(m_selEnd);
		const QRect rect(QPoint(qMin(a.x(), b.x()), qMin(a.y(), b.y())),
			QPoint(qMax(a.x(), b.x()), qMax(a.y(), b.y())));
		drawDashedXorRect(painter, rect);
	}
}

void ImagePane::drawDashedXorRect(QPainter &painter, const QRect &rect) const
{
	painter.save();
	painter.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
	QPen pen(Qt::white, 1, Qt::CustomDashLine);
	pen.setDashPattern({ 1.0, 1.0 }); // WinIMerge's 1 px dot pattern
	pen.setCosmetic(true);
	painter.setPen(pen);
	painter.setBrush(Qt::NoBrush);
	if (rect.width() == 0 || rect.height() == 0)
		painter.drawLine(rect.topLeft(), rect.bottomRight());
	else
		painter.drawRect(rect);
	painter.restore();
}

void ImagePane::resizeEvent(QResizeEvent *event)
{
	QAbstractScrollArea::resizeEvent(event);
	updateScrollBars();
}

void ImagePane::scrollContentsBy(int, int)
{
	viewport()->update();
	emit scrolled(m_pane);
}

void ImagePane::mousePressEvent(QMouseEvent *event)
{
	emit mousePressed(m_pane, event);
}

void ImagePane::mouseReleaseEvent(QMouseEvent *event)
{
	emit mouseReleased(m_pane, event);
}

void ImagePane::mouseMoveEvent(QMouseEvent *event)
{
	emit mouseMoved(m_pane, event);
}

void ImagePane::mouseDoubleClickEvent(QMouseEvent *event)
{
	emit mouseDoubleClicked(m_pane, event);
}

void ImagePane::wheelEvent(QWheelEvent *event)
{
	emit wheelTurned(m_pane, event);
	event->accept();
}

void ImagePane::keyPressEvent(QKeyEvent *event)
{
	emit keyPressed(m_pane, event);
	if (!event->isAccepted())
		QAbstractScrollArea::keyPressEvent(event);
}

void ImagePane::focusInEvent(QFocusEvent *event)
{
	QAbstractScrollArea::focusInEvent(event);
	emit focusReceived(m_pane);
}

void ImagePane::focusOutEvent(QFocusEvent *event)
{
	QAbstractScrollArea::focusOutEvent(event);
	emit focusLost(m_pane);
}

void ImagePane::contextMenuEvent(QContextMenuEvent *event)
{
	emit contextMenuAt(m_pane, event->globalPos());
	event->accept();
}
