// SPDX-License-Identifier: GPL-3.0-or-later
#include "LocationPane.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QWheelEvent>

namespace
{

// upstream's measures (LocationView.cpp)
const int Y_OFFSET = 5;          // empty frame above and below the bars
const double MAX_LINEPIX = 4.0;  // the most pixels a line takes
const int INDICATOR_MIN_HEIGHT = 2;

const QString kMoveCursorOnClick = QStringLiteral("Settings/LocBarMoveCursorOnClick");

std::function<QAction *(QMenu *)> &menuPresenter()
{
	static std::function<QAction *(QMenu *)> presenter;
	return presenter;
}

/** IsColorDark: no channel reaches the middle. */
bool isDark(const QColor &color)
{
	return color.red() < 0x80 && color.green() < 0x80 && color.blue() < 0x80;
}

/** CEColor::GetDarkenColor */
QColor darken(const QColor &color, double factor)
{
	return QColor(static_cast<int>(color.red() * factor),
		static_cast<int>(color.green() * factor), static_cast<int>(color.blue() * factor));
}

/** CEColor::GetIntermediateColor: ratio parts of a, the rest of b. */
QColor between(const QColor &a, const QColor &b, double ratio)
{
	const auto mix = [ratio](int x, int y) {
		return qBound(0, static_cast<int>((x - y) * ratio) + y, 255);
	};
	return QColor(mix(a.red(), b.red()), mix(a.green(), b.green()), mix(a.blue(), b.blue()));
}

/** Draw3dRect: the top and left edges in one color, the others in another. */
void draw3dRect(QPainter &painter, const QRect &rect, const QColor &topLeft,
	const QColor &bottomRight)
{
	painter.fillRect(rect.left(), rect.top(), rect.width() - 1, 1, topLeft);
	painter.fillRect(rect.left(), rect.top(), 1, rect.height() - 1, topLeft);
	painter.fillRect(rect.right(), rect.top(), 1, rect.height(), bottomRight);
	painter.fillRect(rect.left(), rect.bottom(), rect.width(), 1, bottomRight);
}

} // namespace

LocationPane::LocationPane(QWidget *parent)
	: QWidget(parent)
{
	setPaneCount(2);
	setCursor(Qt::PointingHandCursor);
	setToolTip(tr("Location pane \xE2\x80\x94 click to jump"));
}

void LocationPane::setPaneCount(int count)
{
	m_paneCount = qBound(2, count, 3);
	// upstream's pane opens 40 pixels wide; a third bar takes a bit more
	setFixedWidth(m_paneCount == 3 ? 52 : 40);
	update();
}

void LocationPane::setBands(std::vector<Band> bands, int totalLines)
{
	m_bands = std::move(bands);
	m_totalLines = qMax(1, totalLines);
	update();
}

void LocationPane::setRibbons(std::vector<Ribbon> ribbons, const QColor &color,
	const QColor &currentColor)
{
	m_ribbons = std::move(ribbons);
	m_ribbonColor = color;
	m_currentRibbonColor = currentColor;
	update();
}

void LocationPane::setViewport(int firstVisibleLine, int visibleLines)
{
	m_viewFirst = firstVisibleLine;
	m_viewCount = visibleLines;
	update();
}

bool LocationPane::moveCursorOnClick()
{
	return QSettings().value(kMoveCursorOnClick, true).toBool();
}

void LocationPane::setMoveCursorOnClick(bool move)
{
	QSettings().setValue(kMoveCursorOnClick, move);
}

QAction *LocationPane::execMenu(QMenu *menu, const QPoint &globalPos)
{
	if (menuPresenter())
		return menuPresenter()(menu);
	return menu->exec(globalPos);
}

void LocationPane::setMenuPresenterForTest(std::function<QAction *(QMenu *menu)> presenter)
{
	menuPresenter() = std::move(presenter);
}

/** CalculateBars: a bar per file, as wide as the room between them, and a
    line at most MAX_LINEPIX pixels high. */
LocationPane::Geometry LocationPane::geometry() const
{
	Geometry g;
	const int w = width() / (m_paneCount * 2);
	const int margin = (width() - w * m_paneCount) / (m_paneCount + 1);
	const double hTotal = height() - 2 * Y_OFFSET;
	g.lineInPix = hTotal / m_totalLines;
	g.pixInLines = m_totalLines / qMax(1.0, hTotal);
	if (g.lineInPix > MAX_LINEPIX)
	{
		g.lineInPix = MAX_LINEPIX;
		g.pixInLines = 1 / MAX_LINEPIX;
	}
	const int top = Y_OFFSET - 1;
	const int bottom = static_cast<int>(g.lineInPix * m_totalLines + Y_OFFSET + 1);
	for (int pane = 0; pane < m_paneCount; ++pane)
	{
		const int left = pane * (w + margin) + margin;
		// (upstream's rectangles leave their right and bottom edges out)
		g.bar[pane] = QRect(QPoint(left, top), QPoint(left + w - 1, bottom - 1));
	}
	return g;
}

QRect LocationPane::barForTest(int side) const
{
	return side >= 0 && side < m_paneCount ? geometry().bar[side] : QRect();
}

/** IsInsideBar */
LocationPane::Area LocationPane::areaAt(const QPoint &point) const
{
	const Geometry g = geometry();
	for (int pane = 0; pane < m_paneCount; ++pane)
		if (g.bar[pane].contains(point))
			return static_cast<Area>(Bar0 + pane);
	if (point.y() > g.bar[0].top() && point.y() <= g.bar[0].bottom() + 1)
		return BarHeight;
	return NoBar;
}

/** The line a height stands for, past the last one below the bars. */
int LocationPane::rawLineAt(const Geometry &g, int y) const
{
	return static_cast<int>(g.pixInLines * (y - Y_OFFSET));
}

/** GetLineFromYPos, within the lines. Upstream then takes one off, as if
    the line it found were numbered from 1: the line above the pointer is
    the one it reports and goes to. Here it is the line under it. */
int LocationPane::lineAt(int y) const
{
	return qBound(0, rawLineAt(geometry(), y), m_totalLines - 1);
}

void LocationPane::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	const Geometry g = geometry();

	// the pane a shade apart from the files' background, the bars in it
	const QColor whitespace = palette().color(QPalette::Base);
	const QColor face = isDark(whitespace)
		? QColor(qMin(whitespace.red() + 20, 255), qMin(whitespace.green() + 20, 255),
			qMin(whitespace.blue() + 32, 255))
		: QColor(qMax(whitespace.red() - 24, 0), qMax(whitespace.green() - 24, 0),
			qMax(whitespace.blue() - 12, 0));
	painter.fillRect(rect(), face);
	const QColor shadow = palette().color(QPalette::Mid);
	const QColor shadow2 = between(face, shadow, 0.9);
	const QColor shadow3 = between(face, shadow2, 0.5);
	const QColor shadow4 = between(face, shadow3, 0.5);
	const QColor shadow5 = between(face, shadow4, 0.5);
	for (int pane = 0; pane < m_paneCount; ++pane)
	{
		painter.fillRect(g.bar[pane], whitespace);
		draw3dRect(painter, g.bar[pane].adjusted(-1, -1, 1, 1), shadow5, shadow4);
		draw3dRect(painter, g.bar[pane], shadow3, shadow2);
	}

	// the differences (DrawRect): a pixel narrower than the bar, at least
	// a pixel high, with a darker first and last row
	for (const Band &band : m_bands)
	{
		if (band.side < 0 || band.side >= m_paneCount)
			continue;
		const QRect &bar = g.bar[band.side];
		const int y0 = static_cast<int>(band.firstLine * g.lineInPix + Y_OFFSET);
		int y1 = static_cast<int>((band.lastLine + 1) * g.lineInPix + Y_OFFSET);
		if (y1 - y0 < 1)
			y1 = y0 + 1;
		const QRect r(QPoint(bar.left() + 1, y0), QPoint(bar.right() - 1, y1 - 1));
		painter.fillRect(r, band.color);
		painter.fillRect(r.left(), r.top(), r.width(), 1, darken(band.color, 0.96));
		painter.fillRect(r.left(), r.bottom(), r.width(), 1, darken(band.color, 0.91));
	}

	// moved blocks (DrawConnectLines): from a block's lines on its bar to
	// where they went on the next bar
	painter.setRenderHint(QPainter::Antialiasing);
	for (const Ribbon &ribbon : m_ribbons)
	{
		if (ribbon.side < 0 || ribbon.side + 1 >= m_paneCount)
			continue;
		const QColor color = ribbon.current ? m_currentRibbonColor : m_ribbonColor;
		const int top = static_cast<int>(ribbon.firstLine * g.lineInPix + Y_OFFSET);
		const int height = static_cast<int>((ribbon.firstLine + ribbon.lines) * g.lineInPix
			+ Y_OFFSET) - top;
		const QPointF leftUpper(g.bar[ribbon.side].right(), top);
		const QPointF leftLower(g.bar[ribbon.side].right(), top + height - 1);
		const double otherTop = static_cast<int>(ribbon.otherFirstLine * g.lineInPix + Y_OFFSET);
		const QPointF rightUpper(g.bar[ribbon.side + 1].left(), otherTop);
		const QPointF rightLower(g.bar[ribbon.side + 1].left(), otherTop + height - 1);
		const double middle = (leftUpper.x() + rightUpper.x()) / 2;
		painter.setPen(QPen(color, 1));
		QPainterPath path(leftUpper);
		if (leftLower.y() - leftUpper.y() <= 1)
		{
			path.cubicTo(QPointF(middle, leftUpper.y()), QPointF(middle, rightLower.y()),
				rightUpper);
			painter.setBrush(Qt::NoBrush);
			painter.drawPath(path);
			continue;
		}
		path.cubicTo(QPointF(middle, leftUpper.y()), QPointF(middle, rightUpper.y()), rightUpper);
		path.lineTo(rightLower);
		path.cubicTo(QPointF(middle, rightLower.y()), QPointF(middle, leftLower.y()), leftLower);
		path.closeSubpath();
		painter.setBrush(color);
		painter.drawPath(path);
	}
	painter.setRenderHint(QPainter::Antialiasing, false);

	// the lines the panes show (DrawVisibleAreaRect), across the pane
	if (m_viewCount > 0)
	{
		int top = static_cast<int>(Y_OFFSET + m_viewFirst * g.lineInPix);
		int bottom = static_cast<int>(Y_OFFSET + (m_viewFirst + m_viewCount) * g.lineInPix);
		const int barBottom = static_cast<int>(qMin(m_totalLines / g.pixInLines + Y_OFFSET,
			static_cast<double>(height() - Y_OFFSET)));
		bottom = qMin(bottom, barBottom);
		if (bottom - top < INDICATOR_MIN_HEIGHT)
		{
			if (top < Y_OFFSET + 20)
				bottom += INDICATOR_MIN_HEIGHT - (bottom - top);
			else
				bottom = top + INDICATOR_MIN_HEIGHT;
		}
		const QRect area(QPoint(2, top), QPoint(width() - 3, bottom - 1));
		QColor indicator = palette().color(QPalette::Highlight);
		indicator.setAlpha(60);
		painter.fillRect(area, indicator);
		painter.setPen(palette().color(QPalette::Highlight));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(area.adjusted(0, 0, -1, -1));
	}
}

/** OnLButtonDown: the line under the pointer, the cursor on it with "Move
    Cursor on Click", and the panes brought to it, as the drag goes on to
    do (the mouse is held by the pane until the button is let go). */
void LocationPane::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
	{
		QWidget::mousePressEvent(event);
		return;
	}
	m_dragging = true;
	const QPoint point = event->position().toPoint();
	if (moveCursorOnClick())
	{
		const Area area = areaAt(point);
		if (area != NoBar)
			emit gotoRequested(lineAt(point.y()), area == BarHeight ? 0 : area,
				!(event->modifiers() & Qt::ShiftModifier));
	}
	mouseMoveEvent(event);
}

/** OnMouseMove while the mouse is held: the line under the pointer in the
    middle of the panes, never above the bars. */
void LocationPane::mouseMoveEvent(QMouseEvent *event)
{
	if (!m_dragging)
		return;
	const int y = qMax(event->position().toPoint().y(), Y_OFFSET);
	emit centerRequested(rawLineAt(geometry(), y));
}

void LocationPane::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
		m_dragging = false;
	QWidget::mouseReleaseEvent(event);
}

/** OnLButtonDblClk: the cursor on the line, whatever "Move Cursor on
    Click" says. A double click holds no mouse: no drag follows it. */
void LocationPane::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;
	m_dragging = false;
	const QPoint point = event->position().toPoint();
	const Area area = areaAt(point);
	if (area != NoBar)
		emit gotoRequested(lineAt(point.y()), area == BarHeight ? 0 : area,
			!(event->modifiers() & Qt::ShiftModifier));
}

/** OnMouseWheel: the active pane scrolls. */
void LocationPane::wheelEvent(QWheelEvent *event)
{
	emit wheelTurned(event);
	event->accept();
}

/** OnContextMenu: from the keyboard, the menu opens near the corner. */
void LocationPane::contextMenuEvent(QContextMenuEvent *event)
{
	QPoint point = event->pos();
	QPoint global = event->globalPos();
	if (event->reason() == QContextMenuEvent::Keyboard)
	{
		point = QPoint(5, 5);
		global = mapToGlobal(point);
	}
	const Area area = areaAt(point);
	const int side = area == NoBar ? NoBar : (area == BarHeight ? 0 : area);
	emit contextMenuRequested(global, side, side == NoBar ? -1 : lineAt(point.y()));
	event->accept();
}
