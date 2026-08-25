// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractScrollArea>
#include <QImage>

class CImgMergeBuffer;

/**
 * One image pane of the image comparison, replicating WinIMerge's
 * CImgWindow: a 16 px margin around the scaled image, flat themed
 * background, nearest-neighbour scaling, checkerboard (or a solid back
 * color) behind transparent pixels, and the original's scroll/zoom
 * geometry so panes can stay hard-locked together.
 */
class ImagePane : public QAbstractScrollArea
{
	Q_OBJECT
public:
	static constexpr int Margin = 16; // WinIMerge MARGIN

	explicit ImagePane(QWidget *parent = nullptr);

	void setBuffer(CImgMergeBuffer *buffer, int pane);
	int paneIndex() const { return m_pane; }

	double zoom() const { return m_zoom; }
	void setZoom(double zoom);

	void setUseBackColor(bool use, const QColor &color);

	/** Re-fetch the pane's composited image from the buffer. */
	void refreshImage();

	// coordinate conversions (exact CImgWindow formulas)
	QPoint convertDPtoLP(const QPoint &dp) const;
	QPoint convertLPtoDP(const QPoint &lp) const;
	/** Image coordinates under the mouse cursor. */
	QPoint cursorImagePos() const;

	/** Center on (x, y) if force, else only scroll when off-screen. */
	void scrollTo(int x, int y, bool force = false);
	/** Put image point (lx, ly) at device point (dx, dy). */
	void scrollTo2(int lx, int ly, int dx, int dy);

	// rectangle selection overlay (image coordinates, not normalized)
	void setSelectionStart(const QPoint &pt, bool clamp = true);
	void setSelectionEnd(const QPoint &pt, bool clamp = true);
	QRect selection() const; // normalized
	bool selectionVisible() const { return m_selectionVisible; }
	void clearSelection();

	// floating (pasted / dragged) image overlay
	void startFloatingImage(const QImage &image, const QPoint &pos,
		const QPoint &cursor);
	void restartFloatingDrag(const QPoint &cursor);
	void dragFloatingImage(const QPoint &cursor);
	bool hasFloatingImage() const { return !m_floating.isNull(); }
	const QImage &floatingImage() const { return m_floating; }
	QRect floatingRect() const;
	void clearFloatingImage();

signals:
	void mousePressed(int pane, QMouseEvent *event);
	void mouseReleased(int pane, QMouseEvent *event);
	void mouseMoved(int pane, QMouseEvent *event);
	void mouseDoubleClicked(int pane, QMouseEvent *event);
	void wheelTurned(int pane, QWheelEvent *event);
	void keyPressed(int pane, QKeyEvent *event);
	void focusReceived(int pane);
	void focusLost(int pane);
	void contextMenuAt(int pane, const QPoint &globalPos);
	void scrolled(int pane);

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void focusInEvent(QFocusEvent *event) override;
	void focusOutEvent(QFocusEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void scrollContentsBy(int dx, int dy) override;

private:
	void updateScrollBars();
	void drawDashedXorRect(QPainter &painter, const QRect &deviceRect) const;

	CImgMergeBuffer *m_buffer = nullptr;
	int m_pane = 0;
	QImage m_image;
	double m_zoom = 1.0;
	bool m_useBackColor = true;
	QColor m_backColor = Qt::white;
	QPoint m_selStart, m_selEnd;
	bool m_selectionVisible = false;
	QImage m_floating;
	QPoint m_floatingPos;     // image coords
	QPoint m_floatingCursor;  // last drag cursor, image coords
};
