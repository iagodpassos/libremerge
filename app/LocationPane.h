// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QColor>
#include <QWidget>
#include <vector>

class QMenu;
class QAction;

/**
 * WinMerge's location pane (CLocationView): a bar per file, scaled to the
 * whole comparison, with the differences drawn as colored blocks, the
 * lines the panes show marked, and, with moved blocks on, a ribbon from
 * each moved block to where it went on the next file.
 *
 * The mouse moves through the comparison as upstream's does. The left
 * button brings the line under it to the middle of the panes, the cursor
 * too with "Move Cursor on Click" (the default), and dragging keeps doing
 * so; a double click puts the cursor on the line whatever that option
 * says; the wheel scrolls the active pane; the right button asks for the
 * menu, which the file window builds (upstream's IDR_POPUP_LOCATIONBAR).
 *
 * Lines here are the lines the panes show: the hidden lines of the
 * display filter take no room, as upstream draws by its views' lines.
 */
class LocationPane : public QWidget
{
	Q_OBJECT
public:
	struct Band
	{
		int side;       // the file's bar
		int firstLine;
		int lastLine;   // inclusive
		QColor color;
	};
	/** A moved block (upstream's MovedLine): its lines on a file, and
	    where they went on the next file. */
	struct Ribbon
	{
		int side;           // joins this file's bar to the next one's
		int firstLine;
		int otherFirstLine;
		int lines;
		bool current;       // of the current difference
	};
	/** What a point of the pane is over (IsInsideBar): a file's bar, the
	    height of the bars outside them, or neither. */
	enum Area
	{
		NoBar = -1,
		Bar0,
		Bar1,
		Bar2,
		BarHeight,
	};

	explicit LocationPane(QWidget *parent = nullptr);

	void setPaneCount(int count);
	void setBands(std::vector<Band> bands, int totalLines);
	void setRibbons(std::vector<Ribbon> ribbons, const QColor &color,
		const QColor &currentColor);
	void setViewport(int firstVisibleLine, int visibleLines);

	Area areaAt(const QPoint &point) const;
	/** The line at a height (GetLineFromYPos), within the lines. */
	int lineAt(int y) const;

	/** "Move Cursor on Click" (OPT_LOCBAR_MOVECURSOR_ONCLICK, on by
	    default), shared by every location pane. */
	static bool moveCursorOnClick();
	static void setMoveCursorOnClick(bool move);

	/** Open a menu at a point and give back what was picked, or hand it to
	    the test's presenter in place of opening it. */
	static QAction *execMenu(QMenu *menu, const QPoint &globalPos);
	static void setMenuPresenterForTest(std::function<QAction *(QMenu *menu)> presenter);

	int viewFirstForTest() const { return m_viewFirst; }
	int viewCountForTest() const { return m_viewCount; }
	const std::vector<Ribbon> &ribbonsForTest() const { return m_ribbons; }
	const std::vector<Band> &bandsForTest() const { return m_bands; }
	/** A bar's rectangle (for tests). */
	QRect barForTest(int side) const;

signals:
	/** Put the cursor on a line of a file (GotoLocation): the anchor
	    stays where it was when Shift was held. */
	void gotoRequested(int line, int side, bool moveAnchor);
	/** Bring a line to the middle of the panes (a press, and each move
	    of a drag). */
	void centerRequested(int line);
	/** The wheel turned over the pane: the active pane is to scroll. */
	void wheelTurned(QWheelEvent *event);
	/** The menu, for a point: side NoBar when the point is outside the
	    bars' height, the line then meaningless. */
	void contextMenuRequested(const QPoint &globalPos, int side, int line);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

private:
	struct Geometry
	{
		QRect bar[3];
		double lineInPix = 1;  // pixels a line takes
		double pixInLines = 1; // lines a pixel stands for
	};
	Geometry geometry() const;
	int rawLineAt(const Geometry &g, int y) const;

	std::vector<Band> m_bands;
	std::vector<Ribbon> m_ribbons;
	QColor m_ribbonColor;
	QColor m_currentRibbonColor;
	int m_paneCount = 2;
	int m_totalLines = 1;
	int m_viewFirst = 0;
	int m_viewCount = 0;
	bool m_dragging = false;
};
