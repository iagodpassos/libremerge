// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "Icons.h"

#include <functional>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace lm
{

namespace
{

// palette shared by the icon set
const QColor kGold(239, 203, 5);
const QColor kGoldEdge(180, 150, 0);
const QColor kBlue(45, 100, 190);
const QColor kBlueEdge(25, 65, 140);
const QColor kGreen(60, 150, 60);
const QColor kRed(205, 55, 50);
const QColor kGray(130, 130, 130);
const QColor kPaper(252, 252, 252);

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

void drawDiffBar(QPainter &p, int y)
{
	p.setPen(QPen(kGoldEdge, 1.2));
	p.setBrush(kGold);
	p.drawRect(5, y, 22, 6);
}

void drawArrowVertical(QPainter &p, bool up, int dy = 0)
{
	QPainterPath path;
	if (up)
	{
		path.moveTo(16, 4 + dy);
		path.lineTo(25, 13 + dy);
		path.lineTo(20, 13 + dy);
		path.lineTo(20, 20 + dy);
		path.lineTo(12, 20 + dy);
		path.lineTo(12, 13 + dy);
		path.lineTo(7, 13 + dy);
	}
	else
	{
		path.moveTo(16, 28 + dy);
		path.lineTo(25, 19 + dy);
		path.lineTo(20, 19 + dy);
		path.lineTo(20, 12 + dy);
		path.lineTo(12, 12 + dy);
		path.lineTo(12, 19 + dy);
		path.lineTo(7, 19 + dy);
	}
	path.closeSubpath();
	p.setPen(QPen(kBlueEdge, 1.2));
	p.setBrush(kBlue);
	p.drawPath(path);
}

void drawArrowHorizontal(QPainter &p, bool right, int x0, int x1)
{
	// arrow body from x0 to x1 at mid height, head at the pointing end
	QPainterPath path;
	const int head = 7;
	if (right)
	{
		path.moveTo(x1, 16);
		path.lineTo(x1 - head, 8);
		path.lineTo(x1 - head, 12);
		path.lineTo(x0, 12);
		path.lineTo(x0, 20);
		path.lineTo(x1 - head, 20);
		path.lineTo(x1 - head, 24);
	}
	else
	{
		path.moveTo(x0, 16);
		path.lineTo(x0 + head, 8);
		path.lineTo(x0 + head, 12);
		path.lineTo(x1, 12);
		path.lineTo(x1, 20);
		path.lineTo(x0 + head, 20);
		path.lineTo(x0 + head, 24);
	}
	path.closeSubpath();
	p.setPen(QPen(kBlueEdge, 1.2));
	p.setBrush(kBlue);
	p.drawPath(path);
}

void drawPage(QPainter &p, int x, int y, int w, int h)
{
	p.setPen(QPen(kGray, 1.4));
	p.setBrush(kPaper);
	p.drawRect(x, y, w, h);
}

void drawCross(QPainter &p, int cx, int cy, int r)
{
	p.setPen(QPen(kRed, 3.4, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(cx - r, cy - r, cx + r, cy + r);
	p.drawLine(cx + r, cy - r, cx - r, cy + r);
}

} // namespace

QIcon icon(Icon id)
{
	switch (id)
	{
	case Icon::FirstDiff:
		return renderIcon([](QPainter &p) {
			drawDiffBar(p, 3);
			drawArrowVertical(p, true, 8);
		});
	case Icon::PrevDiff:
		return renderIcon([](QPainter &p) {
			drawDiffBar(p, 23);
			drawArrowVertical(p, true);
		});
	case Icon::NextDiff:
		return renderIcon([](QPainter &p) {
			drawDiffBar(p, 3);
			drawArrowVertical(p, false);
		});
	case Icon::LastDiff:
		return renderIcon([](QPainter &p) {
			drawDiffBar(p, 23);
			drawArrowVertical(p, false, -8);
		});
	case Icon::CopyRight:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kGoldEdge, 1.2));
			p.setBrush(kGold);
			p.drawRect(4, 7, 10, 18);
			drawArrowHorizontal(p, true, 16, 29);
		});
	case Icon::CopyLeft:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kGoldEdge, 1.2));
			p.setBrush(kGold);
			p.drawRect(18, 7, 10, 18);
			drawArrowHorizontal(p, false, 3, 16);
		});
	case Icon::CopyAllRight:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kGoldEdge, 1.2));
			p.setBrush(kGold);
			p.drawRect(4, 4, 10, 9);
			p.drawRect(4, 19, 10, 9);
			drawArrowHorizontal(p, true, 16, 29);
		});
	case Icon::CopyAllLeft:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kGoldEdge, 1.2));
			p.setBrush(kGold);
			p.drawRect(18, 4, 10, 9);
			p.drawRect(18, 19, 10, 9);
			drawArrowHorizontal(p, false, 3, 16);
		});
	case Icon::Undo:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kBlue, 3.4, Qt::SolidLine, Qt::FlatCap));
			p.setBrush(Qt::NoBrush);
			p.drawArc(QRectF(7, 9, 18, 18), 0 * 16, 200 * 16);
			QPainterPath head;
			head.moveTo(2.5, 18);
			head.lineTo(12, 15);
			head.lineTo(8, 24);
			head.closeSubpath();
			p.setPen(Qt::NoPen);
			p.setBrush(kBlue);
			p.drawPath(head);
		});
	case Icon::Redo:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kBlue, 3.4, Qt::SolidLine, Qt::FlatCap));
			p.setBrush(Qt::NoBrush);
			p.drawArc(QRectF(7, 9, 18, 18), 180 * 16, -200 * 16);
			QPainterPath head;
			head.moveTo(29.5, 18);
			head.lineTo(20, 15);
			head.lineTo(24, 24);
			head.closeSubpath();
			p.setPen(Qt::NoPen);
			p.setBrush(kBlue);
			p.drawPath(head);
		});
	case Icon::Swap:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kBlueEdge, 1.2));
			p.setBrush(kBlue);
			QPainterPath top;
			top.moveTo(28, 10);
			top.lineTo(20, 3);
			top.lineTo(20, 7);
			top.lineTo(4, 7);
			top.lineTo(4, 13);
			top.lineTo(20, 13);
			top.lineTo(20, 17);
			top.closeSubpath();
			p.drawPath(top);
			QPainterPath bottom;
			bottom.moveTo(4, 22);
			bottom.lineTo(12, 15);
			bottom.lineTo(12, 19);
			bottom.lineTo(28, 19);
			bottom.lineTo(28, 25);
			bottom.lineTo(12, 25);
			bottom.lineTo(12, 29);
			bottom.closeSubpath();
			p.drawPath(bottom);
		});
	case Icon::Options:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kGray, 2.6, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(4, 8, 28, 8);
			p.drawLine(4, 16, 28, 16);
			p.drawLine(4, 24, 28, 24);
			p.setPen(QPen(kGoldEdge, 1.2));
			p.setBrush(kGold);
			p.drawEllipse(QPointF(11, 8), 3.6, 3.6);
			p.drawEllipse(QPointF(21, 16), 3.6, 3.6);
			p.drawEllipse(QPointF(8, 24), 3.6, 3.6);
		});
	case Icon::Find:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kBlue, 3.0));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(QPointF(13, 13), 7.5, 7.5);
			p.setPen(QPen(kBlueEdge, 4.0, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(QPointF(19, 19), QPointF(27, 27));
		});
	case Icon::Refresh:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kGreen, 3.6, Qt::SolidLine, Qt::FlatCap));
			p.setBrush(Qt::NoBrush);
			p.drawArc(QRectF(6, 6, 20, 20), 30 * 16, 280 * 16);
			QPainterPath head;
			head.moveTo(29, 12);
			head.lineTo(20, 8);
			head.lineTo(26, 2.5);
			head.closeSubpath();
			p.setPen(Qt::NoPen);
			p.setBrush(kGreen);
			p.drawPath(head);
		});
	case Icon::Save:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kBlueEdge, 1.4));
			p.setBrush(kBlue);
			QPainterPath body;
			body.moveTo(5, 5);
			body.lineTo(23, 5);
			body.lineTo(27, 9);
			body.lineTo(27, 27);
			body.lineTo(5, 27);
			body.closeSubpath();
			p.drawPath(body);
			p.setPen(Qt::NoPen);
			p.setBrush(kPaper);
			p.drawRect(10, 5, 11, 8);   // shutter
			p.setBrush(kBlue);
			p.drawRect(17, 6, 3, 6);    // shutter slot
			p.setBrush(kPaper);
			p.drawRect(9, 17, 14, 10);  // label
		});
	case Icon::TreeView:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kGray, 2.0));
			p.drawLine(8, 6, 8, 24);
			p.drawLine(8, 8, 14, 8);
			p.drawLine(8, 16, 14, 16);
			p.drawLine(8, 24, 14, 24);
			p.setPen(QPen(kGoldEdge, 1.2));
			p.setBrush(kGold);
			p.drawRect(15, 4, 12, 7);
			p.drawRect(15, 12, 12, 7);
			p.drawRect(15, 20, 12, 7);
		});
	case Icon::DeleteLeft:
		return renderIcon([](QPainter &p) {
			drawPage(p, 4, 7, 11, 18);
			drawPage(p, 18, 7, 11, 18);
			drawCross(p, 9, 16, 5);
		});
	case Icon::DeleteRight:
		return renderIcon([](QPainter &p) {
			drawPage(p, 4, 7, 11, 18);
			drawPage(p, 18, 7, 11, 18);
			drawCross(p, 23, 16, 5);
		});
	case Icon::DeleteBoth:
		return renderIcon([](QPainter &p) {
			drawPage(p, 4, 7, 11, 18);
			drawPage(p, 18, 7, 11, 18);
			drawCross(p, 9, 16, 5);
			drawCross(p, 23, 16, 5);
		});
	case Icon::DiffPane:
		return renderIcon([](QPainter &p) {
			p.setPen(QPen(kGray, 1.6));
			p.setBrush(kPaper);
			p.drawRect(4, 5, 24, 22);
			p.setPen(Qt::NoPen);
			p.setBrush(kGold);
			p.drawRect(5.2, 20, 21.6, 6);
			p.setPen(QPen(kGray, 1.6));
			p.setBrush(Qt::NoBrush);
			p.drawLine(4, 19, 28, 19);
		});
	}
	return QIcon();
}

} // namespace lm
