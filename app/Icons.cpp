// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "Icons.h"

#include <functional>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

namespace lm
{

namespace
{

// SF-Symbols-like line glyphs: a single adaptive stroke color with the
// occasional accent from the application icon's palette
const qreal kStroke = 2.4;
const QColor kGold(0xff, 0xb3, 0x3c);
const QColor kGreen(0x58, 0xb8, 0x78);
const QColor kRed(0xe0, 0x5d, 0x58);

QColor baseColor()
{
	// follows the window chrome (light glyphs on a dark toolbar)
	return qApp->palette().color(QPalette::WindowText);
}

QPen linePen(const QColor &color, qreal width = kStroke)
{
	return QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

/** Render one drawing (authored in a 32x32 unit space) at the sizes and
    device pixel ratios a toolbar may ask for. */
QIcon renderIcon(const std::function<void(QPainter &)> &draw)
{
	QIcon icon;
	for (const int size : { 16, 24, 32 })
	{
		for (const qreal dpr : { 1.0, 2.0 })
		{
			QPixmap pm(static_cast<int>(size * dpr), static_cast<int>(size * dpr));
			pm.setDevicePixelRatio(dpr);
			pm.fill(Qt::transparent);
			QPainter painter(&pm);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.scale(size / 32.0, size / 32.0);
			draw(painter);
			painter.end();
			icon.addPixmap(pm);
		}
	}
	return icon;
}

/** Vertical line arrow with a chevron head at the pointing end. */
void drawArrowV(QPainter &p, bool up, qreal x, qreal fromY, qreal toY)
{
	p.setPen(linePen(baseColor()));
	p.setBrush(Qt::NoBrush);
	p.drawLine(QPointF(x, fromY), QPointF(x, toY));
	const qreal head = 5.5;
	const qreal dir = up ? 1.0 : -1.0;
	p.drawLine(QPointF(x - head, toY + dir * head), QPointF(x, toY));
	p.drawLine(QPointF(x + head, toY + dir * head), QPointF(x, toY));
}

/** Horizontal line arrow with a chevron head at the pointing end. */
void drawArrowH(QPainter &p, bool right, qreal y, qreal fromX, qreal toX,
	const QColor &color)
{
	p.setPen(linePen(color));
	p.setBrush(Qt::NoBrush);
	p.drawLine(QPointF(fromX, y), QPointF(toX, y));
	const qreal head = 5.5;
	const qreal dir = right ? -1.0 : 1.0;
	p.drawLine(QPointF(toX + dir * head, y - head), QPointF(toX, y));
	p.drawLine(QPointF(toX + dir * head, y + head), QPointF(toX, y));
}

void drawGoldBar(QPainter &p, qreal y)
{
	p.setPen(linePen(kGold, 3.2));
	p.drawLine(QPointF(9, y), QPointF(23, y));
}

void drawPaneOutline(QPainter &p, const QRectF &rect,
	const QColor &color = QColor())
{
	p.setPen(linePen(color.isValid() ? color : baseColor(), 2.0));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(rect, 2.5, 2.5);
}

void drawCross(QPainter &p, qreal cx, qreal cy, qreal r)
{
	p.setPen(linePen(kRed, 2.6));
	p.drawLine(QPointF(cx - r, cy - r), QPointF(cx + r, cy + r));
	p.drawLine(QPointF(cx + r, cy - r), QPointF(cx - r, cy + r));
}

/** U-turn arrow (undo/redo), looping over the top. */
void drawUturn(QPainter &p, bool backward)
{
	QPainterPath path;
	if (backward)
	{
		path.moveTo(24.5, 26);
		path.lineTo(24.5, 15);
		path.arcTo(QRectF(8.5, 6.5, 16, 17), 0, 180);
		path.lineTo(8.5, 19);
	}
	else
	{
		path.moveTo(7.5, 26);
		path.lineTo(7.5, 15);
		path.arcTo(QRectF(7.5, 6.5, 16, 17), 180, -180);
		path.lineTo(23.5, 19);
	}
	p.setPen(linePen(baseColor()));
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);
	const qreal x = backward ? 8.5 : 23.5;
	p.drawLine(QPointF(x - 5, 15.5), QPointF(x, 20.5));
	p.drawLine(QPointF(x + 5, 15.5), QPointF(x, 20.5));
}

} // namespace

QIcon icon(Icon id)
{
	switch (id)
	{
	case Icon::FirstDiff:
		return renderIcon([](QPainter &p) {
			drawGoldBar(p, 6);
			drawArrowV(p, true, 16, 27, 12);
		});
	case Icon::PrevDiff:
		return renderIcon([](QPainter &p) {
			drawArrowV(p, true, 16, 27, 6);
		});
	case Icon::NextDiff:
		return renderIcon([](QPainter &p) {
			drawArrowV(p, false, 16, 5, 26);
		});
	case Icon::LastDiff:
		return renderIcon([](QPainter &p) {
			drawGoldBar(p, 26);
			drawArrowV(p, false, 16, 5, 20);
		});
	case Icon::CopyRight:
		return renderIcon([](QPainter &p) {
			drawPaneOutline(p, QRectF(4.5, 8, 9, 16));
			drawArrowH(p, true, 16, 17, 28, kGold);
		});
	case Icon::CopyLeft:
		return renderIcon([](QPainter &p) {
			drawPaneOutline(p, QRectF(18.5, 8, 9, 16));
			drawArrowH(p, false, 16, 15, 4, kGold);
		});
	case Icon::CopyAllRight:
		return renderIcon([](QPainter &p) {
			drawPaneOutline(p, QRectF(4.5, 8, 9, 16));
			p.setPen(linePen(kGold));
			p.drawLine(QPointF(17, 10), QPointF(22.5, 16));
			p.drawLine(QPointF(17, 22), QPointF(22.5, 16));
			p.drawLine(QPointF(23, 10), QPointF(28.5, 16));
			p.drawLine(QPointF(23, 22), QPointF(28.5, 16));
		});
	case Icon::CopyAllLeft:
		return renderIcon([](QPainter &p) {
			drawPaneOutline(p, QRectF(18.5, 8, 9, 16));
			p.setPen(linePen(kGold));
			p.drawLine(QPointF(15, 10), QPointF(9.5, 16));
			p.drawLine(QPointF(15, 22), QPointF(9.5, 16));
			p.drawLine(QPointF(9, 10), QPointF(3.5, 16));
			p.drawLine(QPointF(9, 22), QPointF(3.5, 16));
		});
	case Icon::Undo:
		return renderIcon([](QPainter &p) {
			drawUturn(p, true);
		});
	case Icon::Redo:
		return renderIcon([](QPainter &p) {
			drawUturn(p, false);
		});
	case Icon::Swap:
		return renderIcon([](QPainter &p) {
			drawArrowH(p, true, 11, 6, 26, baseColor());
			drawArrowH(p, false, 21, 26, 6, baseColor());
		});
	case Icon::Refresh:
		return renderIcon([](QPainter &p) {
			p.setPen(linePen(kGreen));
			p.setBrush(Qt::NoBrush);
			p.drawArc(QRectF(7, 7, 18, 18), 55 * 16, 285 * 16);
			// chevron head at the arc's start (upper right)
			p.drawLine(QPointF(21.5, 3.5), QPointF(22.4, 9.6));
			p.drawLine(QPointF(27.6, 8.0), QPointF(22.4, 9.6));
		});
	case Icon::Save:
		return renderIcon([](QPainter &p) {
			p.setPen(linePen(baseColor()));
			p.setBrush(Qt::NoBrush);
			QPainterPath tray;
			tray.moveTo(6.5, 17);
			tray.lineTo(6.5, 24.5);
			tray.lineTo(25.5, 24.5);
			tray.lineTo(25.5, 17);
			p.drawPath(tray);
			drawArrowV(p, false, 16, 5, 17.5);
		});
	case Icon::Options:
		return renderIcon([](QPainter &p) {
			p.setPen(linePen(baseColor(), 2.0));
			p.drawLine(QPointF(6, 9), QPointF(26, 9));
			p.drawLine(QPointF(6, 16), QPointF(26, 16));
			p.drawLine(QPointF(6, 23), QPointF(26, 23));
			p.setPen(Qt::NoPen);
			p.setBrush(kGold);
			p.drawEllipse(QPointF(12, 9), 3.0, 3.0);
			p.drawEllipse(QPointF(21, 16), 3.0, 3.0);
			p.drawEllipse(QPointF(9, 23), 3.0, 3.0);
		});
	case Icon::Find:
		return renderIcon([](QPainter &p) {
			p.setPen(linePen(baseColor()));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(QPointF(13.5, 13.5), 7.0, 7.0);
			p.drawLine(QPointF(18.8, 18.8), QPointF(26, 26));
		});
	case Icon::TreeView:
		return renderIcon([](QPainter &p) {
			p.setPen(linePen(baseColor(), 2.0));
			p.drawLine(QPointF(8, 7), QPointF(8, 24));
			p.drawLine(QPointF(8, 8), QPointF(13, 8));
			p.drawLine(QPointF(8, 16), QPointF(13, 16));
			p.drawLine(QPointF(8, 24), QPointF(13, 24));
			p.setPen(Qt::NoPen);
			p.setBrush(kGold);
			p.drawRoundedRect(QRectF(15.5, 5.5, 11, 5), 1.5, 1.5);
			p.drawRoundedRect(QRectF(15.5, 13.5, 11, 5), 1.5, 1.5);
			p.drawRoundedRect(QRectF(15.5, 21.5, 11, 5), 1.5, 1.5);
		});
	case Icon::DeleteLeft:
		return renderIcon([](QPainter &p) {
			drawPaneOutline(p, QRectF(4.5, 8, 10, 16));
			drawPaneOutline(p, QRectF(17.5, 8, 10, 16));
			drawCross(p, 9.5, 16, 3.4);
		});
	case Icon::DeleteRight:
		return renderIcon([](QPainter &p) {
			drawPaneOutline(p, QRectF(4.5, 8, 10, 16));
			drawPaneOutline(p, QRectF(17.5, 8, 10, 16));
			drawCross(p, 22.5, 16, 3.4);
		});
	case Icon::DeleteBoth:
		return renderIcon([](QPainter &p) {
			drawPaneOutline(p, QRectF(4.5, 8, 10, 16));
			drawPaneOutline(p, QRectF(17.5, 8, 10, 16));
			drawCross(p, 9.5, 16, 3.4);
			drawCross(p, 22.5, 16, 3.4);
		});
	case Icon::DiffPane:
		return renderIcon([](QPainter &p) {
			drawPaneOutline(p, QRectF(4.5, 6.5, 23, 19));
			p.setPen(linePen(baseColor(), 2.0));
			p.drawLine(QPointF(4.5, 18.5), QPointF(27.5, 18.5));
			p.setPen(Qt::NoPen);
			p.setBrush(kGold);
			p.drawRoundedRect(QRectF(7.5, 21, 17, 2.6), 1.3, 1.3);
		});
	}
	return QIcon();
}

} // namespace lm
