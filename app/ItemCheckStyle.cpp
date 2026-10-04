// SPDX-License-Identifier: GPL-3.0-or-later
#include "ItemCheckStyle.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleOptionViewItem>

namespace
{

// macOS 27 shows only the first item view check box of a paint pass. Qt's
// macOS style draws them all through one shared native button, and every
// draw after the first leaves nothing behind (Qt 6.8 and 6.11 alike), so
// the other rows of a checkable list have no box at all, ticked or not.

/** Ask the application style for two boxes in one pass and look at the
    second one. */
bool platformSkipsItemCheckBoxes()
{
#ifdef Q_OS_MACOS
	static const bool skips = []() {
		QImage image(64, 24, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		const QRect boxes[2] = { QRect(4, 4, 16, 16), QRect(40, 4, 16, 16) };
		{
			QPainter painter(&image);
			QStyleOptionViewItem option;
			option.state = QStyle::State_Enabled | QStyle::State_On;
			for (const QRect &box : boxes)
			{
				option.rect = box;
				QApplication::style()->drawPrimitive(
					QStyle::PE_IndicatorItemViewItemCheck, &option, &painter, nullptr);
			}
		}
		const auto drawn = [&image](const QRect &box) {
			for (int y = box.top(); y <= box.bottom(); ++y)
				for (int x = box.left(); x <= box.right(); ++x)
					if (qAlpha(image.pixel(x, y)) != 0)
						return true;
			return false;
		};
		return drawn(boxes[0]) && !drawn(boxes[1]);
	}();
	return skips;
#else
	return false;
#endif
}

/** The platform style, except for the item check box, painted here in
    the look of the native one: a rounded box, filled with the accent
    color and marked when ticked. */
class ItemCheckStyle : public QProxyStyle
{
public:
	void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
		QPainter *painter, const QWidget *widget) const override
	{
		if (element != PE_IndicatorItemViewItemCheck)
		{
			QProxyStyle::drawPrimitive(element, option, painter, widget);
			return;
		}
		const bool ticked = option->state & State_On;
		const bool partial = option->state & State_NoChange;
		const qreal side = qMin<qreal>(15.0,
			qMin(option->rect.width(), option->rect.height()) - 1.0);
		QRectF box(0, 0, side, side);
		box.moveCenter(QRectF(option->rect).center());

		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		if (!(option->state & State_Enabled))
			painter->setOpacity(0.5);
		if (ticked || partial)
		{
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
			const QColor accent = option->palette.color(QPalette::Accent);
#else
			const QColor accent = option->palette.color(QPalette::Highlight);
#endif
			painter->setPen(Qt::NoPen);
			painter->setBrush(accent);
			painter->drawRoundedRect(box, 3.5, 3.5);
			// a dark mark on a light accent (yellow), white on the others
			const qreal luma = 0.299 * accent.redF() + 0.587 * accent.greenF()
				+ 0.114 * accent.blueF();
			const QColor mark = luma > 0.65 ? QColor(0, 0, 0, 215) : QColor(Qt::white);
			painter->setPen(QPen(mark, side * 0.13, Qt::SolidLine, Qt::RoundCap,
				Qt::RoundJoin));
			painter->setBrush(Qt::NoBrush);
			QPainterPath path;
			if (ticked)
			{
				path.moveTo(box.left() + side * 0.25, box.top() + side * 0.53);
				path.lineTo(box.left() + side * 0.42, box.top() + side * 0.71);
				path.lineTo(box.left() + side * 0.75, box.top() + side * 0.29);
			}
			else
			{
				path.moveTo(box.left() + side * 0.27, box.center().y());
				path.lineTo(box.right() - side * 0.27, box.center().y());
			}
			painter->drawPath(path);
		}
		else
		{
			// the empty box: a lighter patch on a dark theme, a white one
			// with an edge on a light theme
			const QColor text = option->palette.color(QPalette::Text);
			const bool dark = option->palette.color(QPalette::Base).lightness() < 128;
			QColor fill = dark ? text : QColor(Qt::white);
			if (dark)
				fill.setAlphaF(0.17);
			QColor edge = text;
			edge.setAlphaF(0.28);
			painter->setPen(dark ? QPen(Qt::NoPen) : QPen(edge, 1.0));
			painter->setBrush(fill);
			painter->drawRoundedRect(dark ? box : box.adjusted(0.5, 0.5, -0.5, -0.5),
				3.5, 3.5);
		}
		painter->restore();
	}
};

} // namespace

namespace lm
{

void ensureItemCheckBoxes(QAbstractItemView *view)
{
	if (view == nullptr || !platformSkipsItemCheckBoxes())
		return;
	auto *style = new ItemCheckStyle;
	style->setParent(view); // setStyle does not take it
	view->setStyle(style);
}

bool itemCheckBoxesNeedHelp()
{
	return platformSkipsItemCheckBoxes();
}

} // namespace lm
