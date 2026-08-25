// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <QStringList>
#include <QWidget>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QMouseEvent;
class QSlider;
class QSpinBox;
class QSplitter;
class QTimer;
class CImgMergeBuffer;
class ImagePane;
class DiffMapWidget;

/**
 * Two-way image comparison, porting WinMerge's image compare (WinIMerge):
 * side-by-side panes over the block-based pixel diff engine, with the
 * original's settings panel (highlight/blink, block size and alpha, color
 * distance threshold, insertion/deletion detection, overlay modes, zoom,
 * multi-page), diff navigation, copy between sides with undo/redo, wipe
 * and rectangle-select dragging modes, rotation/flips and saving.
 */
class ImageCompareView : public QWidget
{
	Q_OBJECT
public:
	explicit ImageCompareView(QWidget *parent = nullptr);
	~ImageCompareView() override;

	/** Load 2 or 3 images and run the initial comparison. */
	bool compare(const QStringList &paths, QString *error);
	bool compare(const QString &leftPath, const QString &rightPath,
		QString *error)
	{
		return compare(QStringList{ leftPath, rightPath }, error);
	}

	bool isModified() const;
	int diffCount() const;
	int paneCount() const { return m_paneCount; }
	QStringList paths() const;
	QString tabTitle() const;
	bool saveModified(QString *error);
	void setReadOnlySides(const QList<bool> &readOnly);

	void gotoFirstDiff();
	void gotoNextDiff();
	void gotoPrevDiff();
	void gotoLastDiff();
	void selectDiffAtCursor();
	/** Copy the current difference from sourceSide into the merge target
	    (the other side in 2-way mode, the middle pane in 3-way mode). */
	void copyCurrentDiff(int sourceSide);
	void copyAllFrom(int sourceSide);
	void undo();
	void redo();
	void focusNextPane();
	void recompare();
	void zoomIn();
	void zoomOut();
	void zoomReset();

	// Image-menu surface (also driven by the settings panel)
	bool showDifferences() const;
	int blockSize() const;
	double colorDistanceThreshold() const;
	int insertionDeletionMode() const;
	int overlayMode() const;
	int draggingMode() const { return m_draggingMode; }
	int maxPageCount() const;
	void setShowDifferences(bool show);
	void setBlockSize(int size);
	void setColorDistanceThreshold(double threshold);
	void setInsertionDeletionMode(int mode);
	void setOverlayMode(int mode);
	void setDraggingMode(int mode);
	void setZoom(double zoom);
	void rotateActivePane(int direction); // +1 = right (angle - 90)
	void flipActivePaneHorizontal();
	void flipActivePaneVertical();
	void nextPage();
	void prevPage();
	void nextPageActivePane();
	void prevPageActivePane();

	// clipboard / selection (Edit menu)
	void editCopy();
	void editCut();
	void editPaste();
	void editDelete();
	void selectAll();
	void cancelSelection();

signals:
	void modifiedChanged(bool modified);
	void pathsChanged();

private:
	void buildToolbar(class QVBoxLayout *layout);
	QWidget *buildToolPanel();
	void loadSettings();
	void saveSettings();
	void applyDiffColors();
	void applyTheme();
	void syncPanel();
	void refreshPanes();
	void afterDiffNavigation();
	void afterBufferChange();
	void updatePaneHeader(int pane);
	void updatePaneStatus(int pane);
	void updateDiffStatus();
	void updateActions();
	void updateAnimationTimer();

	// interaction state machine (ChildWnd_On* parity)
	void paneMousePressed(int pane, QMouseEvent *event);
	void paneMouseReleased(int pane, QMouseEvent *event);
	void paneMouseMoved(int pane, QMouseEvent *event);
	void paneDoubleClicked(int pane, QMouseEvent *event);
	void paneWheel(int pane, class QWheelEvent *event);
	void paneKey(int pane, class QKeyEvent *event);
	void paneContextMenu(int pane, const QPoint &globalPos);
	void commitFloatingImage(int pane);
	void cutOrCopySelectionToFloating(int pane, bool copy);
	QRect realRect(int pane, const QRect &viewRect) const;
	void scrollAllTo(int x, int y, bool force);
	void setActivePane(int pane);
	bool savePane(int pane, QString *error);

	int mergeTarget(int sourceSide) const
	{
		return m_paneCount == 3 ? 1 : 1 - sourceSide;
	}

	std::unique_ptr<CImgMergeBuffer> m_buffer;
	QString m_paths[3];
	int m_paneCount = 2;
	int m_activePane = 0;
	double m_zoom = 1.0;

	ImagePane *m_panes[3] = {};
	QWidget *m_columns[3] = {};
	QLabel *m_headers[3] = {};
	QLabel *m_paneStatus[3] = {};
	QSplitter *m_splitter = nullptr;
	QLabel *m_status = nullptr;
	DiffMapWidget *m_diffMap = nullptr;
	QTimer *m_animTimer = nullptr;

	// settings panel widgets (synced from the buffer)
	QCheckBox *m_chkHighlight = nullptr;
	QCheckBox *m_chkBlink = nullptr;
	QSlider *m_sldBlockSize = nullptr;
	QSlider *m_sldBlockAlpha = nullptr;
	QSlider *m_sldThreshold = nullptr;
	QSlider *m_sldOverlayAlpha = nullptr;
	QSlider *m_sldZoom = nullptr;
	QComboBox *m_cmbInsDel = nullptr;
	QComboBox *m_cmbOverlay = nullptr;
	QSpinBox *m_spnPage = nullptr;
	QLabel *m_lblBlockSize = nullptr;
	QLabel *m_lblBlockAlpha = nullptr;
	QLabel *m_lblThreshold = nullptr;
	QLabel *m_lblOverlayAlpha = nullptr;
	QLabel *m_lblZoom = nullptr;
	QWidget *m_panel = nullptr;
	bool m_syncingPanel = false;
	bool m_syncingScroll = false;

	QAction *m_actUndo = nullptr;
	QAction *m_actRedo = nullptr;
	QAction *m_actSave = nullptr;

	int m_draggingMode = 1; // MOVE (WinMerge default)
	int m_draggingCurrent = 0;
	bool m_dragging = false;
	QPoint m_dragOrigin;    // device coords in the originating pane
	bool m_wasModified = false;

	friend class DiffMapWidget;
};
