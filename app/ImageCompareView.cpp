// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "ImageCompareView.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "Icons.h"
#include "ImagePane.h"
#include "Theme.h"

#include "ImgMergeBuffer.hpp"

namespace
{

// WinIMerge dragging modes
enum
{
	DragNone = 0,
	DragMove = 1,
	DragAdjustOffset = 2,
	DragVerticalWipe = 3,
	DragHorizontalWipe = 4,
	DragRectangleSelect = 5,
	DragMoveImage = 256,
	DragResizeWidth = 257,
	DragResizeHeight = 258,
	DragResizeBoth = 259,
};

// CD Threshold slider: 0..80 linear to 100, 80..100 quadratic to the
// maximum RGB distance (ImgToolWindow's exact mapping)
const double kThresholdCurve = (std::sqrt(3.0 * 255 * 255) - 100.0) / (20.0 * 20.0);

double sliderToThreshold(int v)
{
	return v < 80 ? v * 100.0 / 80
		: 100.0 + kThresholdCurve * (v - 80) * (v - 80);
}

int thresholdToSlider(double d)
{
	return d < 100 ? static_cast<int>(d * 80 / 100)
		: static_cast<int>(std::sqrt((d - 100.0) / kThresholdCurve) + 80);
}

Image::Color toColor(const QColor &color)
{
	return Image::Rgb(color.red(), color.green(), color.blue());
}

QRect preprocessedRect(const CImgMergeBuffer &buffer, int pane)
{
	const Point<unsigned> offset = buffer.GetImageOffset(pane);
	return QRect(offset.x, offset.y,
		buffer.GetPreprocessedImageWidth(pane),
		buffer.GetPreprocessedImageHeight(pane));
}

} // namespace

/** The diff-map thumbnail: the buffer renders the blocks at the widget's
    aspect-fitted size, a frame shows pane 0's viewport, clicking centers
    every pane on that point. */
class DiffMapWidget : public QWidget
{
public:
	explicit DiffMapWidget(ImageCompareView *view, QWidget *parent = nullptr)
		: QWidget(parent), m_view(view)
	{
		setMinimumHeight(48);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	}

	void setBuffer(CImgMergeBuffer *buffer) { m_buffer = buffer; update(); }
	void setPane0(ImagePane *pane) { m_pane0 = pane; update(); }

protected:
	QRect mapRect() const
	{
		if (m_buffer == nullptr || m_buffer->GetPaneCount() == 0)
			return QRect();
		const int w = m_buffer->GetDiffImageWidth();
		const int h = m_buffer->GetDiffImageHeight();
		if (w <= 0 || h <= 0)
			return QRect();
		int mw = width() - 8;
		int mh = mw * h / w;
		if (mh > height() - 8)
		{
			mh = height() - 8;
			mw = mh * w / h;
		}
		if (mw <= 0 || mh <= 0)
			return QRect();
		return QRect((width() - mw) / 2, (height() - mh) / 2, mw, mh);
	}

	void paintEvent(QPaintEvent *) override
	{
		const QRect rc = mapRect();
		if (rc.isEmpty())
			return;
		QPainter painter(this);
		const bool dark = lm::Theme::instance()->dark();
		painter.fillRect(rc, dark ? Qt::black : Qt::white);
		if (Image *map = m_buffer->GetDiffMapImage(rc.width(), rc.height()))
			painter.drawImage(rc.topLeft(), map->qimage());
		if (m_pane0 != nullptr)
		{
			// viewport frame from pane 0's scrollbars (Win32 semantics:
			// content = max + page)
			const QScrollBar *hb = m_pane0->horizontalScrollBar();
			const QScrollBar *vb = m_pane0->verticalScrollBar();
			const int hMax = hb->maximum() + hb->pageStep();
			const int vMax = vb->maximum() + vb->pageStep();
			if ((hb->pageStep() < hMax || vb->pageStep() < vMax)
				&& hMax > 0 && vMax > 0)
			{
				QRect frame(
					rc.left() + rc.width() * hb->value() / hMax,
					rc.top() + rc.height() * vb->value() / vMax,
					rc.width() * hb->pageStep() / hMax,
					rc.height() * vb->pageStep() / vMax);
				painter.setPen(dark ? Qt::black : Qt::white);
				painter.drawRect(frame.intersected(rc.adjusted(0, 0, -1, -1)));
			}
		}
	}

	void mousePressEvent(QMouseEvent *event) override
	{
		const QRect rc = mapRect();
		if (rc.isEmpty() || m_buffer == nullptr)
			return;
		const QPoint pos = event->pos() - rc.topLeft();
		m_view->scrollAllTo(
			pos.x() * m_buffer->GetDiffImageWidth() / rc.width(),
			pos.y() * m_buffer->GetDiffImageHeight() / rc.height(), true);
	}

private:
	ImageCompareView *m_view;
	CImgMergeBuffer *m_buffer = nullptr;
	ImagePane *m_pane0 = nullptr;

	friend class ImageCompareView;
};

ImageCompareView::~ImageCompareView() = default;

ImageCompareView::ImageCompareView(QWidget *parent)
	: QWidget(parent)
	, m_buffer(new CImgMergeBuffer)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	buildToolbar(layout);

	auto *body = new QHBoxLayout;
	body->setContentsMargins(0, 0, 0, 0);
	body->setSpacing(0);
	body->addWidget(buildToolPanel());

	m_splitter = new QSplitter(Qt::Horizontal, this);
	m_splitter->setHandleWidth(4); // WinIMerge's pane gutter
	m_splitter->setChildrenCollapsible(false);
	for (int i = 0; i < 2; ++i)
	{
		auto *column = new QWidget(this);
		auto *columnLayout = new QVBoxLayout(column);
		columnLayout->setContentsMargins(0, 0, 0, 0);
		columnLayout->setSpacing(0);
		m_headers[i] = new QLabel(column);
		m_headers[i]->setContentsMargins(6, 3, 6, 3);
		columnLayout->addWidget(m_headers[i]);
		m_panes[i] = new ImagePane(column);
		columnLayout->addWidget(m_panes[i], 1);
		m_paneStatus[i] = new QLabel(column);
		m_paneStatus[i]->setContentsMargins(6, 2, 6, 2);
		columnLayout->addWidget(m_paneStatus[i]);
		m_splitter->addWidget(column);

		connect(m_panes[i], &ImagePane::mousePressed,
			this, &ImageCompareView::paneMousePressed);
		connect(m_panes[i], &ImagePane::mouseReleased,
			this, &ImageCompareView::paneMouseReleased);
		connect(m_panes[i], &ImagePane::mouseMoved,
			this, &ImageCompareView::paneMouseMoved);
		connect(m_panes[i], &ImagePane::mouseDoubleClicked,
			this, &ImageCompareView::paneDoubleClicked);
		connect(m_panes[i], &ImagePane::wheelTurned,
			this, &ImageCompareView::paneWheel);
		connect(m_panes[i], &ImagePane::keyPressed,
			this, &ImageCompareView::paneKey);
		connect(m_panes[i], &ImagePane::contextMenuAt,
			this, &ImageCompareView::paneContextMenu);
		connect(m_panes[i], &ImagePane::focusReceived,
			this, [this](int pane) { setActivePane(pane); });
		connect(m_panes[i], &ImagePane::focusLost, this, [this](int pane) {
			// like ChildWnd_OnKillFocus: commit an uncommitted paste
			commitFloatingImage(pane);
		});
		connect(m_panes[i], &ImagePane::scrolled, this, [this](int pane) {
			if (m_syncingScroll)
				return;
			m_syncingScroll = true;
			for (int j = 0; j < m_paneCount; ++j)
			{
				if (j == pane)
					continue;
				m_panes[j]->horizontalScrollBar()->setValue(
					m_panes[pane]->horizontalScrollBar()->value());
				m_panes[j]->verticalScrollBar()->setValue(
					m_panes[pane]->verticalScrollBar()->value());
			}
			m_syncingScroll = false;
			if (m_diffMap != nullptr)
				m_diffMap->update();
		});
	}
	body->addWidget(m_splitter, 1);
	layout->addLayout(body, 1);

	m_status = new QLabel(this);
	m_status->setContentsMargins(6, 3, 6, 3);
	layout->addWidget(m_status);

	// blink + overlay-animation refresh (WinIMerge ticks at 25 ms with an
	// effective 50 ms throttle)
	m_animTimer = new QTimer(this);
	m_animTimer->setInterval(50);
	connect(m_animTimer, &QTimer::timeout, this, [this]() {
		m_buffer->RefreshImages();
		for (int i = 0; i < m_paneCount; ++i)
			m_panes[i]->refreshImage();
	});

	// clipboard/selection shortcuts, scoped to this view so text panes in
	// other tabs keep their native handling
	auto addShortcut = [this](const QKeySequence &sequence, auto slot) {
		auto *shortcut = new QShortcut(sequence, this);
		shortcut->setContext(Qt::WidgetWithChildrenShortcut);
		connect(shortcut, &QShortcut::activated, this, slot);
	};
	addShortcut(QKeySequence::Copy, [this]() { editCopy(); });
	addShortcut(QKeySequence::Cut, [this]() { editCut(); });
	addShortcut(QKeySequence::Paste, [this]() { editPaste(); });
	addShortcut(QKeySequence::Delete, [this]() { editDelete(); });
	addShortcut(QKeySequence::SelectAll, [this]() { selectAll(); });

	loadSettings();
	applyDiffColors();
	applyTheme();
	connect(lm::Theme::instance(), &lm::Theme::changed, this, [this]() {
		applyDiffColors();
		applyTheme();
		m_buffer->RefreshImages();
		refreshPanes();
	});
}

void ImageCompareView::buildToolbar(QVBoxLayout *layout)
{
	auto *toolbar = new QToolBar(this);
	toolbar->setIconSize(QSize(16, 16));
	toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	auto addToolAction = [this, toolbar](lm::Icon icon, const QString &text,
		const QString &shortcutHint, auto slot) -> QAction * {
		QAction *action = toolbar->addAction(lm::icon(icon), text);
		action->setData(static_cast<int>(icon));
		action->setToolTip(shortcutHint.isEmpty() ? text
			: QStringLiteral("%1 (%2)").arg(text, shortcutHint));
		connect(action, &QAction::triggered, this, slot);
		return action;
	};
	addToolAction(lm::Icon::FirstDiff, tr("First Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x96"), [this]() { gotoFirstDiff(); });
	addToolAction(lm::Icon::PrevDiff, tr("Previous Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x91"), [this]() { gotoPrevDiff(); });
	addToolAction(lm::Icon::NextDiff, tr("Next Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x93"), [this]() { gotoNextDiff(); });
	addToolAction(lm::Icon::LastDiff, tr("Last Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x98"), [this]() { gotoLastDiff(); });
	toolbar->addSeparator();
	addToolAction(lm::Icon::CopyRight, tr("Copy to Right"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x92"), [this]() { copyCurrentDiff(0); });
	addToolAction(lm::Icon::CopyLeft, tr("Copy to Left"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x90"), [this]() { copyCurrentDiff(1); });
	addToolAction(lm::Icon::CopyAllRight, tr("Copy All to Right"), QString(),
		[this]() { copyAllFrom(0); });
	addToolAction(lm::Icon::CopyAllLeft, tr("Copy All to Left"), QString(),
		[this]() { copyAllFrom(1); });
	toolbar->addSeparator();
	m_actUndo = addToolAction(lm::Icon::Undo, tr("Undo"),
		QString::fromUtf8("\xE2\x8C\x98Z"), [this]() { undo(); });
	m_actUndo->setEnabled(false);
	m_actRedo = addToolAction(lm::Icon::Redo, tr("Redo"),
		QString::fromUtf8("\xE2\x87\xA7\xE2\x8C\x98Z"), [this]() { redo(); });
	m_actRedo->setEnabled(false);
	toolbar->addSeparator();
	addToolAction(lm::Icon::Refresh, tr("Recompare"), QStringLiteral("F5"),
		[this]() { recompare(); });
	m_actSave = addToolAction(lm::Icon::Save, tr("Save"),
		QString::fromUtf8("\xE2\x8C\x98S"),
		[this]() { QString error; saveModified(&error); });
	m_actSave->setEnabled(false);
	layout->addWidget(toolbar);
}

QWidget *ImageCompareView::buildToolPanel()
{
	// the WinIMerge settings panel (WinMerge's image "Location Pane")
	m_panel = new QWidget(this);
	m_panel->setFixedWidth(180);
	auto *panelLayout = new QVBoxLayout(m_panel);
	panelLayout->setContentsMargins(6, 6, 6, 6);
	panelLayout->setSpacing(4);

	auto addSliderRow = [this](QVBoxLayout *box, const QString &text,
		int min, int max, QLabel *&valueLabel, QSlider *&slider) {
		auto *row = new QHBoxLayout;
		row->addWidget(new QLabel(text, m_panel));
		row->addStretch(1);
		valueLabel = new QLabel(m_panel);
		row->addWidget(valueLabel);
		box->addLayout(row);
		slider = new QSlider(Qt::Horizontal, m_panel);
		slider->setRange(min, max);
		box->addWidget(slider);
	};

	auto *diffGroup = new QGroupBox(tr("Diff"), m_panel);
	auto *diffBox = new QVBoxLayout(diffGroup);
	diffBox->setSpacing(3);
	m_chkHighlight = new QCheckBox(tr("Highlight"), diffGroup);
	diffBox->addWidget(m_chkHighlight);
	connect(m_chkHighlight, &QCheckBox::toggled, this, [this](bool on) {
		if (!m_syncingPanel)
			setShowDifferences(on);
	});
	m_chkBlink = new QCheckBox(tr("Blink"), diffGroup);
	diffBox->addWidget(m_chkBlink);
	connect(m_chkBlink, &QCheckBox::toggled, this, [this](bool on) {
		if (m_syncingPanel)
			return;
		m_buffer->SetBlinkDifferences(on);
		saveSettings();
		updateAnimationTimer();
		refreshPanes();
	});
	addSliderRow(diffBox, tr("Block Size"), 1, 64, m_lblBlockSize, m_sldBlockSize);
	connect(m_sldBlockSize, &QSlider::valueChanged, this, [this](int v) {
		if (!m_syncingPanel)
			setBlockSize(v);
	});
	addSliderRow(diffBox, tr("Block Alpha"), 0, 100, m_lblBlockAlpha, m_sldBlockAlpha);
	connect(m_sldBlockAlpha, &QSlider::valueChanged, this, [this](int v) {
		if (m_syncingPanel)
			return;
		m_buffer->SetDiffColorAlpha(v / 100.0);
		saveSettings();
		afterBufferChange();
	});
	addSliderRow(diffBox, tr("CD Threshold"), 0, 100, m_lblThreshold, m_sldThreshold);
	connect(m_sldThreshold, &QSlider::valueChanged, this, [this](int v) {
		if (!m_syncingPanel)
			setColorDistanceThreshold(sliderToThreshold(v));
	});
	diffBox->addWidget(new QLabel(tr("Ins/Del Detection"), diffGroup));
	m_cmbInsDel = new QComboBox(diffGroup);
	m_cmbInsDel->addItems({ tr("None"), tr("Vertical"), tr("Horizontal") });
	diffBox->addWidget(m_cmbInsDel);
	connect(m_cmbInsDel, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (!m_syncingPanel)
			setInsertionDeletionMode(index);
	});
	panelLayout->addWidget(diffGroup);

	auto *overlayGroup = new QGroupBox(tr("Overlay"), m_panel);
	auto *overlayBox = new QVBoxLayout(overlayGroup);
	overlayBox->setSpacing(3);
	m_cmbOverlay = new QComboBox(overlayGroup);
	m_cmbOverlay->addItems({ tr("None"), tr("XOR"), tr("Alpha Blend"),
		tr("Alpha Animation") });
	overlayBox->addWidget(m_cmbOverlay);
	connect(m_cmbOverlay, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (!m_syncingPanel)
			setOverlayMode(index);
	});
	addSliderRow(overlayBox, tr("Alpha"), 0, 100, m_lblOverlayAlpha,
		m_sldOverlayAlpha);
	connect(m_sldOverlayAlpha, &QSlider::valueChanged, this, [this](int v) {
		if (m_syncingPanel)
			return;
		m_buffer->SetOverlayAlpha(v / 100.0);
		saveSettings();
		refreshPanes();
		syncPanel(); // keeps the "(nn)" value label live while dragging
	});
	panelLayout->addWidget(overlayGroup);

	auto *viewGroup = new QGroupBox(m_panel);
	auto *viewBox = new QVBoxLayout(viewGroup);
	viewBox->setSpacing(3);
	addSliderRow(viewBox, tr("Zoom"), -7, 56, m_lblZoom, m_sldZoom);
	connect(m_sldZoom, &QSlider::valueChanged, this, [this](int v) {
		if (!m_syncingPanel)
			setZoom(1.0 + v * 0.125);
	});
	auto *pageRow = new QHBoxLayout;
	pageRow->addWidget(new QLabel(tr("Page:"), viewGroup));
	m_spnPage = new QSpinBox(viewGroup);
	m_spnPage->setRange(1, 1);
	pageRow->addWidget(m_spnPage, 1);
	viewBox->addLayout(pageRow);
	connect(m_spnPage, &QSpinBox::valueChanged, this, [this](int page) {
		if (m_syncingPanel)
			return;
		m_buffer->SetCurrentPageAll(page - 1);
		afterBufferChange();
	});
	panelLayout->addWidget(viewGroup);

	m_diffMap = new DiffMapWidget(this, m_panel);
	panelLayout->addWidget(m_diffMap, 1);

	return m_panel;
}

void ImageCompareView::loadSettings()
{
	// sticky global settings, like WinMerge's Image menu persistence
	QSettings settings;
	settings.beginGroup(QStringLiteral("ImageCompare"));
	m_buffer->SetShowDifferences(
		settings.value(QStringLiteral("ShowDifferences"), true).toBool());
	m_buffer->SetBlinkDifferences(
		settings.value(QStringLiteral("BlinkDifferences"), false).toBool());
	m_buffer->SetDiffBlockSize(
		settings.value(QStringLiteral("DiffBlockSize"), 8).toInt());
	m_buffer->SetDiffColorAlpha(
		settings.value(QStringLiteral("DiffColorAlpha"), 70).toInt() / 100.0);
	m_buffer->SetColorDistanceThreshold(
		settings.value(QStringLiteral("ColorDistanceThreshold"), 0).toInt() / 1000.0);
	m_buffer->SetInsertionDeletionDetectionMode(
		static_cast<CImgDiffBuffer::INSERTION_DELETION_DETECTION_MODE>(
			settings.value(QStringLiteral("InsertionDeletionDetectionMode"), 0).toInt()));
	m_buffer->SetOverlayMode(static_cast<CImgDiffBuffer::OVERLAY_MODE>(
		settings.value(QStringLiteral("OverlayMode"), 0).toInt()));
	m_buffer->SetOverlayAlpha(
		settings.value(QStringLiteral("OverlayAlpha"), 30).toInt() / 100.0);
	m_draggingMode = settings.value(QStringLiteral("DraggingMode"), DragMove).toInt();
	m_zoom = settings.value(QStringLiteral("Zoom"), 1000).toInt() / 1000.0;
	settings.endGroup();
	for (int i = 0; i < 2; ++i)
		m_panes[i]->setZoom(m_zoom);
	updateAnimationTimer();
}

void ImageCompareView::saveSettings()
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("ImageCompare"));
	settings.setValue(QStringLiteral("ShowDifferences"),
		m_buffer->GetShowDifferences());
	settings.setValue(QStringLiteral("BlinkDifferences"),
		m_buffer->GetBlinkDifferences());
	settings.setValue(QStringLiteral("DiffBlockSize"),
		m_buffer->GetDiffBlockSize());
	settings.setValue(QStringLiteral("DiffColorAlpha"),
		static_cast<int>(m_buffer->GetDiffColorAlpha() * 100));
	settings.setValue(QStringLiteral("ColorDistanceThreshold"),
		static_cast<int>(m_buffer->GetColorDistanceThreshold() * 1000));
	settings.setValue(QStringLiteral("InsertionDeletionDetectionMode"),
		static_cast<int>(m_buffer->GetInsertionDeletionDetectionMode()));
	settings.setValue(QStringLiteral("OverlayMode"),
		static_cast<int>(m_buffer->GetOverlayMode()));
	settings.setValue(QStringLiteral("OverlayAlpha"),
		static_cast<int>(m_buffer->GetOverlayAlpha() * 100));
	settings.setValue(QStringLiteral("DraggingMode"), m_draggingMode);
	settings.setValue(QStringLiteral("Zoom"),
		static_cast<int>(m_zoom * 1000));
	settings.endGroup();
}

void ImageCompareView::applyDiffColors()
{
	// theme-aware difference palette (WinMerge uses the shared text-diff
	// colors here too)
	const lm::DiffColors &C = lm::diffColors();
	m_buffer->SetDiffColor(toColor(C.diff));
	m_buffer->SetSelDiffColor(toColor(C.selDiff));
	m_buffer->SetDiffDeletedColor(toColor(C.diffDeleted));
	m_buffer->SetSelDiffDeletedColor(toColor(C.selDiffDeleted));
}

void ImageCompareView::applyTheme()
{
	const bool dark = lm::Theme::instance()->dark();
	const QString labelStyle = dark
		? QStringLiteral("QLabel { background: #2c2c2c; color: #b8b8b8; }")
		: QStringLiteral("QLabel { background: #ececec; color: #303030; }");
	m_status->setStyleSheet(labelStyle);
	for (int i = 0; i < 2; ++i)
	{
		m_paneStatus[i]->setStyleSheet(labelStyle);
		updatePaneHeader(i);
	}
	lm::applyToolbarTheme(this);
	for (int i = 0; i < 2; ++i)
		m_panes[i]->viewport()->update();
	if (m_diffMap != nullptr)
		m_diffMap->update();
}

bool ImageCompareView::compare(const QString &leftPath,
	const QString &rightPath, QString *error)
{
	m_paths[0] = leftPath;
	m_paths[1] = rightPath;
	const std::wstring l = leftPath.toStdWString();
	const std::wstring r = rightPath.toStdWString();
	const wchar_t *files[3] = { l.c_str(), r.c_str(), nullptr };
	if (!m_buffer->OpenImages(2, files))
	{
		if (error != nullptr)
			*error = tr("Could not open the files as images.");
		return false;
	}
	m_buffer->CompareImages();
	for (int i = 0; i < 2; ++i)
	{
		m_panes[i]->setBuffer(m_buffer.get(), i);
		m_panes[i]->setZoom(m_zoom);
	}
	m_diffMap->setBuffer(m_buffer.get());
	m_diffMap->setPane0(m_panes[0]);
	m_panes[0]->setFocus();
	afterBufferChange();
	emit pathsChanged();
	return true;
}

bool ImageCompareView::isModified() const
{
	for (int i = 0; i < m_paneCount; ++i)
		if (m_buffer->IsModified(i))
			return true;
	return false;
}

int ImageCompareView::diffCount() const
{
	return m_buffer->GetDiffCount();
}

QStringList ImageCompareView::paths() const
{
	return { m_paths[0], m_paths[1] };
}

QString ImageCompareView::tabTitle() const
{
	return QFileInfo(m_paths[0]).fileName() + QString::fromUtf8(" \xE2\x86\x94 ")
		+ QFileInfo(m_paths[1]).fileName();
}

void ImageCompareView::setReadOnlySides(const QList<bool> &readOnly)
{
	for (int i = 0; i < qMin(2, static_cast<int>(readOnly.size())); ++i)
		m_buffer->SetReadOnly(i, readOnly.at(i));
}

bool ImageCompareView::savePane(int pane, QString *error)
{
	if (!m_buffer->IsModified(pane) || m_buffer->GetReadOnly(pane))
		return true;

	QString target = m_paths[pane];
	if (!m_buffer->IsSaveSupported(pane))
	{
		// Qt cannot re-encode this format (GIF, multi-page): offer PNG
		target = QFileDialog::getSaveFileName(this, tr("Save As"),
			target + QStringLiteral(".png"),
			tr("PNG image (*.png);;All files (*)"));
		if (target.isEmpty())
		{
			if (error != nullptr)
				*error = tr("Saving cancelled.");
			return false;
		}
	}

	// optional .bak backup, same setting the text compare honors
	if (QSettings().value(QStringLiteral("Backup/FileCompare"), true).toBool()
		&& QFile::exists(target))
	{
		const QString bak = target + QStringLiteral(".bak");
		QFile::remove(bak);
		QFile::copy(target, bak);
	}

	if (!m_buffer->SaveImageAs(pane, target.toStdWString().c_str()))
	{
		if (error != nullptr)
			*error = tr("Could not save %1").arg(target);
		return false;
	}
	if (target != m_paths[pane])
	{
		m_paths[pane] = target;
		emit pathsChanged();
	}
	updatePaneHeader(pane);
	updateActions();
	return true;
}

bool ImageCompareView::saveModified(QString *error)
{
	for (int i = 0; i < m_paneCount; ++i)
		if (!savePane(i, error))
			return false;
	afterBufferChange();
	return true;
}

void ImageCompareView::gotoFirstDiff()
{
	m_buffer->FirstDiff();
	afterDiffNavigation();
}

void ImageCompareView::gotoNextDiff()
{
	m_buffer->NextDiff();
	afterDiffNavigation();
}

void ImageCompareView::gotoPrevDiff()
{
	m_buffer->PrevDiff();
	afterDiffNavigation();
}

void ImageCompareView::gotoLastDiff()
{
	m_buffer->LastDiff();
	afterDiffNavigation();
}

void ImageCompareView::selectDiffAtCursor()
{
	const QPoint pt = m_panes[m_activePane]->cursorImagePos();
	m_buffer->SelectDiff(m_buffer->GetDiffIndexFromPoint(pt.x(), pt.y()));
	afterDiffNavigation();
}

void ImageCompareView::copyCurrentDiff(int sourceSide)
{
	const int diffIndex = m_buffer->GetCurrentDiffIndex();
	if (diffIndex < 0)
		return;
	m_buffer->CopyDiff(diffIndex, sourceSide, 1 - sourceSide);
	afterBufferChange();
}

void ImageCompareView::copyAllFrom(int sourceSide)
{
	if (m_buffer->GetDiffCount() == 0)
		return;
	m_buffer->CopyDiffAll(sourceSide, 1 - sourceSide);
	afterBufferChange();
}

void ImageCompareView::undo()
{
	cancelSelection();
	if (m_buffer->Undo())
		afterBufferChange();
}

void ImageCompareView::redo()
{
	cancelSelection();
	if (m_buffer->Redo())
		afterBufferChange();
}

void ImageCompareView::focusNextPane()
{
	setActivePane((m_activePane + 1) % m_paneCount);
}

void ImageCompareView::recompare()
{
	if (!isModified())
	{
		// reload from disk (WinMerge's Ctrl+F5 semantics)
		const std::wstring l = m_paths[0].toStdWString();
		const std::wstring r = m_paths[1].toStdWString();
		const wchar_t *files[3] = { l.c_str(), r.c_str(), nullptr };
		m_buffer->OpenImages(2, files);
	}
	m_buffer->CompareImages();
	afterBufferChange();
}

void ImageCompareView::zoomIn() { setZoom(m_zoom + 0.1); }
void ImageCompareView::zoomOut() { setZoom(m_zoom - 0.1); }
void ImageCompareView::zoomReset() { setZoom(1.0); }

bool ImageCompareView::showDifferences() const
{
	return m_buffer->GetShowDifferences();
}

int ImageCompareView::blockSize() const
{
	return m_buffer->GetDiffBlockSize();
}

double ImageCompareView::colorDistanceThreshold() const
{
	return m_buffer->GetColorDistanceThreshold();
}

int ImageCompareView::insertionDeletionMode() const
{
	return static_cast<int>(m_buffer->GetInsertionDeletionDetectionMode());
}

int ImageCompareView::overlayMode() const
{
	return static_cast<int>(m_buffer->GetOverlayMode());
}

int ImageCompareView::maxPageCount() const
{
	return m_buffer->GetMaxPageCount();
}

void ImageCompareView::setShowDifferences(bool show)
{
	m_buffer->SetShowDifferences(show);
	saveSettings();
	afterBufferChange();
}

void ImageCompareView::setBlockSize(int size)
{
	m_buffer->SetDiffBlockSize(size);
	saveSettings();
	afterBufferChange();
}

void ImageCompareView::setColorDistanceThreshold(double threshold)
{
	m_buffer->SetColorDistanceThreshold(threshold);
	saveSettings();
	afterBufferChange();
}

void ImageCompareView::setInsertionDeletionMode(int mode)
{
	m_buffer->SetInsertionDeletionDetectionMode(
		static_cast<CImgDiffBuffer::INSERTION_DELETION_DETECTION_MODE>(mode));
	saveSettings();
	afterBufferChange();
}

void ImageCompareView::setOverlayMode(int mode)
{
	m_buffer->SetOverlayMode(static_cast<CImgDiffBuffer::OVERLAY_MODE>(mode));
	saveSettings();
	updateAnimationTimer();
	refreshPanes();
	syncPanel();
}

void ImageCompareView::setDraggingMode(int mode)
{
	m_draggingMode = mode;
	saveSettings();
	// cursor shape parity with SetDraggingMode
	Qt::CursorShape shape = Qt::ArrowCursor;
	if (mode == DragHorizontalWipe)
		shape = Qt::SizeHorCursor;
	else if (mode == DragVerticalWipe)
		shape = Qt::SizeVerCursor;
	else if (mode == DragRectangleSelect)
		shape = Qt::CrossCursor;
	for (int i = 0; i < m_paneCount; ++i)
		m_panes[i]->viewport()->setCursor(shape);
}

void ImageCompareView::setZoom(double zoom)
{
	m_zoom = qMax(zoom, 0.1);
	for (int i = 0; i < m_paneCount; ++i)
		m_panes[i]->setZoom(m_zoom);
	saveSettings();
	syncPanel();
	for (int i = 0; i < m_paneCount; ++i)
		updatePaneStatus(i);
	if (m_diffMap != nullptr)
		m_diffMap->update();
}

void ImageCompareView::rotateActivePane(int direction)
{
	// rotate right = angle - 90 (the angle axis is counterclockwise)
	m_buffer->SetRotation(m_activePane,
		m_buffer->GetRotation(m_activePane) - direction * 90.f);
	afterBufferChange();
}

void ImageCompareView::flipActivePaneHorizontal()
{
	m_buffer->SetHorizontalFlip(m_activePane,
		!m_buffer->GetHorizontalFlip(m_activePane));
	afterBufferChange();
}

void ImageCompareView::flipActivePaneVertical()
{
	m_buffer->SetVerticalFlip(m_activePane,
		!m_buffer->GetVerticalFlip(m_activePane));
	afterBufferChange();
}

void ImageCompareView::nextPage()
{
	m_buffer->SetCurrentPageAll(m_buffer->GetCurrentMaxPage() + 1);
	afterBufferChange();
}

void ImageCompareView::prevPage()
{
	m_buffer->SetCurrentPageAll(m_buffer->GetCurrentMaxPage() - 1);
	afterBufferChange();
}

void ImageCompareView::nextPageActivePane()
{
	m_buffer->SetCurrentPage(m_activePane,
		m_buffer->GetCurrentPage(m_activePane) + 1);
	afterBufferChange();
}

void ImageCompareView::prevPageActivePane()
{
	m_buffer->SetCurrentPage(m_activePane,
		m_buffer->GetCurrentPage(m_activePane) - 1);
	afterBufferChange();
}

void ImageCompareView::editCopy()
{
	ImagePane *pane = m_panes[m_activePane];
	if (!pane->selectionVisible() || pane->selection().isEmpty())
		return;
	const QRect rc = realRect(m_activePane, pane->selection());
	Image image;
	m_buffer->CopySubImage(m_activePane, rc.left(), rc.top(),
		rc.left() + rc.width(), rc.top() + rc.height(), image);
	QApplication::clipboard()->setImage(image.qimage());
}

void ImageCompareView::editCut()
{
	if (m_buffer->GetReadOnly(m_activePane))
		return;
	editCopy();
	editDelete();
}

void ImageCompareView::editDelete()
{
	ImagePane *pane = m_panes[m_activePane];
	if (!pane->selectionVisible() || pane->selection().isEmpty()
		|| m_buffer->GetReadOnly(m_activePane))
		return;
	const QRect rc = realRect(m_activePane, pane->selection());
	cancelSelection();
	m_buffer->DeleteRectangle(m_activePane, rc.left(), rc.top(),
		rc.left() + rc.width(), rc.top() + rc.height());
	afterBufferChange();
}

void ImageCompareView::editPaste()
{
	if (m_buffer->GetReadOnly(m_activePane))
		return;
	QImage image = QApplication::clipboard()->image();
	if (image.isNull())
		return;
	cancelSelection();
	image = image.convertToFormat(QImage::Format_ARGB32);
	// grow the pane's image so the paste fits, then float it at (0,0)
	const int newW = qMax<int>(m_buffer->GetImageWidth(m_activePane), image.width());
	const int newH = qMax<int>(m_buffer->GetImageHeight(m_activePane), image.height());
	m_buffer->Resize(m_activePane, newW, newH);
	m_panes[m_activePane]->startFloatingImage(image, QPoint(0, 0), QPoint(0, 0));
	afterBufferChange();
}

void ImageCompareView::selectAll()
{
	const QRect rc = preprocessedRect(*m_buffer, m_activePane);
	m_panes[m_activePane]->setSelectionStart(rc.topLeft());
	m_panes[m_activePane]->setSelectionEnd(rc.bottomRight() + QPoint(1, 1));
}

void ImageCompareView::cancelSelection()
{
	for (int i = 0; i < m_paneCount; ++i)
	{
		m_panes[i]->clearSelection();
		m_panes[i]->clearFloatingImage();
	}
}

QRect ImageCompareView::realRect(int pane, const QRect &viewRect) const
{
	int x1, y1, x2, y2;
	m_buffer->ConvertToRealPos(pane, viewRect.left(), viewRect.top(), x1, y1);
	m_buffer->ConvertToRealPos(pane, viewRect.left() + viewRect.width(),
		viewRect.top() + viewRect.height(), x2, y2);
	return QRect(QPoint(x1, y1), QSize(qMax(0, x2 - x1), qMax(0, y2 - y1)));
}

void ImageCompareView::scrollAllTo(int x, int y, bool force)
{
	m_syncingScroll = true;
	for (int i = 0; i < m_paneCount; ++i)
		m_panes[i]->scrollTo(x, y, force);
	m_syncingScroll = false;
	if (m_diffMap != nullptr)
		m_diffMap->update();
}

void ImageCompareView::setActivePane(int pane)
{
	m_activePane = pane;
	if (!m_panes[pane]->hasFocus())
		m_panes[pane]->setFocus();
	for (int i = 0; i < m_paneCount; ++i)
		updatePaneHeader(i);
}

void ImageCompareView::commitFloatingImage(int pane)
{
	ImagePane *view = m_panes[pane];
	if (!view->hasFloatingImage())
		return;
	const QRect rc = view->floatingRect();
	int rx, ry;
	m_buffer->ConvertToRealPos(pane, rc.left(), rc.top(), rx, ry, false);
	m_buffer->PasteImage(pane, rx, ry, Image(view->floatingImage()));
	view->clearFloatingImage();
	afterBufferChange();
}

/** First drag movement of a selection: turn it into the floating image
    (cut, or copy while Ctrl is held). */
void ImageCompareView::cutOrCopySelectionToFloating(int pane, bool copy)
{
	ImagePane *view = m_panes[pane];
	const QRect sel = view->selection();
	if (sel.isEmpty())
		return;
	const QRect rc = realRect(pane, sel);
	Image image;
	m_buffer->CopySubImage(pane, rc.left(), rc.top(),
		rc.left() + rc.width(), rc.top() + rc.height(), image);
	view->clearSelection();
	view->startFloatingImage(image.qimage(), sel.topLeft(),
		view->cursorImagePos());
	if (!copy && !m_buffer->GetReadOnly(pane))
	{
		m_buffer->DeleteRectangle(pane, rc.left(), rc.top(),
			rc.left() + rc.width(), rc.top() + rc.height());
		afterBufferChange();
	}
}

// ---------------------------------------------------------------- events

void ImageCompareView::paneMousePressed(int pane, QMouseEvent *event)
{
	setActivePane(pane);
	if (event->button() != Qt::LeftButton)
		return;

	m_dragging = true;
	m_draggingCurrent = m_draggingMode;
	m_dragOrigin = event->pos();
	ImagePane *view = m_panes[pane];
	const QPoint pt = view->convertDPtoLP(event->pos());

	if (view->hasFloatingImage() && !view->floatingRect().contains(pt))
		commitFloatingImage(pane);
	if (view->selectionVisible() && !view->selection().contains(pt))
		cancelSelection();

	const QRect img = preprocessedRect(*m_buffer, pane);
	const QRect rightBox(img.right() + 1, img.top(), 8, img.height());
	const QRect bottomBox(img.left(), img.bottom() + 1, img.width(), 8);
	const QRect cornerBox(img.right() + 1, img.bottom() + 1, 8, 8);
	auto selectWholeImage = [view, img]() {
		view->setSelectionStart(img.topLeft());
		view->setSelectionEnd(img.bottomRight() + QPoint(1, 1));
	};

	if (cornerBox.contains(pt))
	{
		selectWholeImage();
		m_draggingCurrent = DragResizeBoth;
	}
	else if (rightBox.contains(pt))
	{
		selectWholeImage();
		m_draggingCurrent = DragResizeWidth;
	}
	else if (bottomBox.contains(pt))
	{
		selectWholeImage();
		m_draggingCurrent = DragResizeHeight;
	}
	else if (view->hasFloatingImage() && view->floatingRect().contains(pt))
	{
		if (event->modifiers() & Qt::ControlModifier)
		{
			// Ctrl-drag duplicates: commit a copy and keep floating
			const QRect rc = view->floatingRect();
			int rx, ry;
			m_buffer->ConvertToRealPos(pane, rc.left(), rc.top(), rx, ry, false);
			m_buffer->PasteImage(pane, rx, ry, Image(view->floatingImage()));
			afterBufferChange();
		}
		view->restartFloatingDrag(pt);
		m_draggingCurrent = DragMoveImage;
	}
	else if (view->selectionVisible() && view->selection().contains(pt))
	{
		m_draggingCurrent = DragMoveImage;
	}
	else if (m_draggingCurrent == DragVerticalWipe)
	{
		view->setSelectionStart(QPoint(0, pt.y()));
		view->setSelectionEnd(QPoint(m_buffer->GetDiffImageWidth(), pt.y()));
		m_buffer->SetWipeModePosition(CImgDiffBuffer::WIPE_VERTICAL, pt.y());
		refreshPanes();
	}
	else if (m_draggingCurrent == DragHorizontalWipe)
	{
		view->setSelectionStart(QPoint(pt.x(), 0));
		view->setSelectionEnd(QPoint(pt.x(), m_buffer->GetDiffImageHeight()));
		m_buffer->SetWipeModePosition(CImgDiffBuffer::WIPE_HORIZONTAL, pt.x());
		refreshPanes();
	}
	else if (m_draggingCurrent == DragRectangleSelect)
	{
		view->setSelectionStart(pt);
	}
}

void ImageCompareView::paneMouseReleased(int pane, QMouseEvent *event)
{
	if (!m_dragging)
		return;
	m_dragging = false;
	ImagePane *view = m_panes[pane];
	const QPoint pt = view->convertDPtoLP(event->pos());

	switch (m_draggingCurrent)
	{
	case DragAdjustOffset:
		cancelSelection();
		m_buffer->AddImageOffset(pane,
			static_cast<int>((event->pos().x() - m_dragOrigin.x()) / m_zoom),
			static_cast<int>((event->pos().y() - m_dragOrigin.y()) / m_zoom));
		afterBufferChange();
		break;
	case DragVerticalWipe:
	case DragHorizontalWipe:
		m_buffer->SetWipeMode(CImgDiffBuffer::WIPE_NONE);
		cancelSelection();
		m_buffer->RefreshImages();
		refreshPanes();
		break;
	case DragRectangleSelect:
		if (view->selection().isEmpty())
			cancelSelection();
		break;
	case DragResizeWidth:
	case DragResizeHeight:
	case DragResizeBoth:
	{
		cancelSelection();
		const QRect img = preprocessedRect(*m_buffer, pane);
		int width = m_buffer->GetImageWidth(pane);
		int height = m_buffer->GetImageHeight(pane);
		if (m_draggingCurrent != DragResizeHeight)
			width += pt.x() - (img.right() + 1);
		if (m_draggingCurrent != DragResizeWidth)
			height += pt.y() - (img.bottom() + 1);
		if (width > 0 && height > 0 && !m_buffer->GetReadOnly(pane))
		{
			m_buffer->Resize(pane, width, height);
			afterBufferChange();
		}
		break;
	}
	default:
		break;
	}
	m_draggingCurrent = DragNone;
}

void ImageCompareView::paneMouseMoved(int pane, QMouseEvent *event)
{
	ImagePane *view = m_panes[pane];
	const QPoint pt = view->convertDPtoLP(event->pos());

	if (!m_dragging)
	{
		// hover cursor per hit test (ChildWnd_OnMouseMove parity)
		const QRect img = preprocessedRect(*m_buffer, pane);
		Qt::CursorShape shape = Qt::ArrowCursor;
		if ((view->selectionVisible() && view->selection().contains(pt))
			|| (view->hasFloatingImage() && view->floatingRect().contains(pt)))
			shape = Qt::SizeAllCursor;
		else if (QRect(img.right() + 1, img.bottom() + 1, 8, 8).contains(pt))
			shape = Qt::SizeFDiagCursor;
		else if (QRect(img.right() + 1, img.top(), 8, img.height()).contains(pt))
			shape = Qt::SizeHorCursor;
		else if (QRect(img.left(), img.bottom() + 1, img.width(), 8).contains(pt))
			shape = Qt::SizeVerCursor;
		else if (m_draggingMode == DragHorizontalWipe)
			shape = Qt::SizeHorCursor;
		else if (m_draggingMode == DragVerticalWipe)
			shape = Qt::SizeVerCursor;
		else if (m_draggingMode == DragRectangleSelect)
			shape = Qt::CrossCursor;
		view->viewport()->setCursor(shape);
		for (int i = 0; i < m_paneCount; ++i)
			updatePaneStatus(i);
		return;
	}

	switch (m_draggingCurrent)
	{
	case DragMove:
	{
		// pan: delta × zoom, fanned to every pane through the scroll sync
		QScrollBar *hb = view->horizontalScrollBar();
		QScrollBar *vb = view->verticalScrollBar();
		hb->setValue(hb->value() + static_cast<int>(
			(m_dragOrigin.x() - event->pos().x()) * m_zoom));
		vb->setValue(vb->value() + static_cast<int>(
			(m_dragOrigin.y() - event->pos().y()) * m_zoom));
		m_dragOrigin = event->pos();
		break;
	}
	case DragAdjustOffset:
	{
		// dashed ghost of where the image will land
		const QRect img = preprocessedRect(*m_buffer, pane);
		const int dx = static_cast<int>((event->pos().x() - m_dragOrigin.x()) / m_zoom);
		const int dy = static_cast<int>((event->pos().y() - m_dragOrigin.y()) / m_zoom);
		view->setSelectionStart(img.topLeft() + QPoint(dx, dy), false);
		view->setSelectionEnd(img.bottomRight() + QPoint(1, 1) + QPoint(dx, dy),
			false);
		break;
	}
	case DragVerticalWipe:
		view->setSelectionStart(QPoint(0, pt.y()));
		view->setSelectionEnd(QPoint(m_buffer->GetDiffImageWidth(), pt.y()));
		m_buffer->SetWipePosition(pt.y());
		refreshPanes();
		break;
	case DragHorizontalWipe:
		view->setSelectionStart(QPoint(pt.x(), 0));
		view->setSelectionEnd(QPoint(pt.x(), m_buffer->GetDiffImageHeight()));
		m_buffer->SetWipePosition(pt.x());
		refreshPanes();
		break;
	case DragRectangleSelect:
		view->setSelectionEnd(pt);
		break;
	case DragMoveImage:
		if (view->selectionVisible())
			cutOrCopySelectionToFloating(pane,
				event->modifiers() & Qt::ControlModifier);
		else if (view->hasFloatingImage())
			view->dragFloatingImage(pt);
		break;
	case DragResizeWidth:
	{
		const QRect img = preprocessedRect(*m_buffer, pane);
		view->setSelectionEnd(QPoint(pt.x(), img.bottom() + 1), false);
		break;
	}
	case DragResizeHeight:
	{
		const QRect img = preprocessedRect(*m_buffer, pane);
		view->setSelectionEnd(QPoint(img.right() + 1, pt.y()), false);
		break;
	}
	case DragResizeBoth:
		view->setSelectionEnd(pt, false);
		break;
	default:
		break;
	}
	for (int i = 0; i < m_paneCount; ++i)
		updatePaneStatus(i);
}

void ImageCompareView::paneDoubleClicked(int pane, QMouseEvent *event)
{
	// double click selects (or deselects) the difference under the cursor
	const QPoint pt = m_panes[pane]->convertDPtoLP(event->pos());
	m_buffer->SelectDiff(m_buffer->GetDiffIndexFromPoint(pt.x(), pt.y()));
	afterDiffNavigation();
}

void ImageCompareView::paneWheel(int pane, QWheelEvent *event)
{
	ImagePane *view = m_panes[pane];
	const int delta = event->angleDelta().y() != 0
		? event->angleDelta().y() : event->angleDelta().x();
	if (event->modifiers() & Qt::ControlModifier)
	{
		// cursor-anchored zoom: the image point under the cursor keeps its
		// device position in every pane
		const QPoint dp = event->position().toPoint();
		const QPoint lp = view->convertDPtoLP(dp);
		setZoom(m_zoom + (delta > 0 ? 0.1 : -0.1));
		m_syncingScroll = true;
		for (int i = 0; i < m_paneCount; ++i)
			m_panes[i]->scrollTo2(lp.x(), lp.y(), dp.x(), dp.y());
		m_syncingScroll = false;
		if (m_diffMap != nullptr)
			m_diffMap->update();
	}
	else if (event->modifiers() & Qt::ShiftModifier)
	{
		QScrollBar *bar = view->horizontalScrollBar();
		bar->setValue(bar->value() - delta / 7); // 17 px per 120 notch
	}
	else
	{
		QScrollBar *bar = view->verticalScrollBar();
		bar->setValue(bar->value() - delta / 7);
	}
}

void ImageCompareView::paneKey(int pane, QKeyEvent *event)
{
	event->accept();
	switch (event->key())
	{
	case Qt::Key_PageUp:
		m_panes[pane]->verticalScrollBar()->triggerAction(
			QAbstractSlider::SliderPageStepSub);
		return;
	case Qt::Key_PageDown:
		m_panes[pane]->verticalScrollBar()->triggerAction(
			QAbstractSlider::SliderPageStepAdd);
		return;
	case Qt::Key_Escape:
		cancelSelection();
		return;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		commitFloatingImage(pane);
		return;
	default:
		break;
	}
	if (event->modifiers() & Qt::ShiftModifier)
	{
		const int step = (event->modifiers() & Qt::ControlModifier) ? 8 : 1;
		int dx = 0, dy = 0;
		if (event->key() == Qt::Key_Left) dx = -step;
		else if (event->key() == Qt::Key_Right) dx = step;
		else if (event->key() == Qt::Key_Up) dy = -step;
		else if (event->key() == Qt::Key_Down) dy = step;
		if (dx != 0 || dy != 0)
		{
			m_buffer->AddImageOffset(m_activePane, dx, dy);
			afterBufferChange();
			return;
		}
	}
	event->ignore();
}

void ImageCompareView::paneContextMenu(int pane, const QPoint &globalPos)
{
	setActivePane(pane);
	QMenu menu(this);
	menu.addAction(tr("Rotate Right 90\xC2\xB0"),
		[this]() { rotateActivePane(1); });
	menu.addAction(tr("Rotate Left 90\xC2\xB0"),
		[this]() { rotateActivePane(-1); });
	menu.addAction(tr("Flip Vertically"),
		[this]() { flipActivePaneVertical(); });
	menu.addAction(tr("Flip Horizontally"),
		[this]() { flipActivePaneHorizontal(); });
	menu.addSeparator();
	QAction *prev = menu.addAction(tr("Previous Page"),
		[this]() { prevPageActivePane(); });
	prev->setEnabled(m_buffer->GetCurrentPage(pane) > 0);
	QAction *next = menu.addAction(tr("Next Page"),
		[this]() { nextPageActivePane(); });
	next->setEnabled(
		m_buffer->GetCurrentPage(pane) < m_buffer->GetPageCount(pane) - 1);
	menu.addSeparator();
	const struct { int mode; QString text; } modes[] = {
		{ DragMove, tr("Move") },
		{ DragAdjustOffset, tr("Adjust Offset") },
		{ DragVerticalWipe, tr("Vertical Wipe") },
		{ DragHorizontalWipe, tr("Horizontal Wipe") },
		{ DragRectangleSelect, tr("Rectangle Select") },
	};
	for (const auto &m : modes)
	{
		QAction *action = menu.addAction(m.text,
			[this, mode = m.mode]() { setDraggingMode(mode); });
		action->setCheckable(true);
		action->setChecked(m_draggingMode == m.mode);
	}
	menu.exec(globalPos);
}

// ------------------------------------------------------------- refresh

void ImageCompareView::refreshPanes()
{
	for (int i = 0; i < m_paneCount; ++i)
		m_panes[i]->refreshImage();
	if (m_diffMap != nullptr)
		m_diffMap->update();
}

void ImageCompareView::afterDiffNavigation()
{
	const int current = m_buffer->GetCurrentDiffIndex();
	if (const DiffInfo *info = m_buffer->GetDiffInfo(current))
		scrollAllTo(info->rc.left * m_buffer->GetDiffBlockSize(),
			info->rc.top * m_buffer->GetDiffBlockSize(), false);
	refreshPanes();
	updateDiffStatus();
}

void ImageCompareView::afterBufferChange()
{
	refreshPanes();
	syncPanel();
	for (int i = 0; i < m_paneCount; ++i)
	{
		updatePaneHeader(i);
		updatePaneStatus(i);
	}
	updateDiffStatus();
	updateActions();
	const bool modified = isModified();
	if (modified != m_wasModified)
	{
		m_wasModified = modified;
		emit modifiedChanged(modified);
	}
}

void ImageCompareView::updatePaneHeader(int pane)
{
	if (m_headers[pane] == nullptr)
		return;
	QString text = QFileInfo(m_paths[pane]).fileName();
	if (text.isEmpty())
		text = pane == 0 ? tr("Untitled Left") : tr("Untitled Right");
	if (m_buffer->IsModified(pane))
		text.prepend(QStringLiteral("* "));
	m_headers[pane]->setText(text);
	m_headers[pane]->setToolTip(m_paths[pane]);
	const bool dark = lm::Theme::instance()->dark();
	const bool active = pane == m_activePane;
	// active pane highlighted in the header, like WinMerge's file bar
	QString bg = dark ? (active ? QStringLiteral("#3d5a86")
			: QStringLiteral("#2c2c2c"))
		: (active ? QStringLiteral("#c8d8ee") : QStringLiteral("#ececec"));
	QString fg = dark ? QStringLiteral("#d4d4d4") : QStringLiteral("#303030");
	m_headers[pane]->setStyleSheet(QStringLiteral(
		"QLabel { background: %1; color: %2; font-weight: %3; }")
		.arg(bg, fg, active ? QStringLiteral("bold") : QStringLiteral("normal")));
}

void ImageCompareView::updatePaneStatus(int pane)
{
	if (m_paneStatus[pane] == nullptr || m_buffer->GetPaneCount() == 0)
		return;
	QString text;

	// cursor readout when the pointer is over one of the panes
	QPoint pt(-1, -1);
	for (int i = 0; i < m_paneCount; ++i)
	{
		if (m_panes[i]->viewport()->underMouse())
		{
			pt = m_panes[i]->cursorImagePos();
			break;
		}
	}
	if (pt.x() >= 0)
	{
		int rx, ry;
		if (m_buffer->ConvertToRealPos(pane, pt.x(), pt.y(), rx, ry))
		{
			const Image::Color color = m_buffer->GetPixelColor(pane, pt.x(), pt.y());
			text += tr("Pt: (%1, %2)  RGBA: (%3, %4, %5, %6)  ")
				.arg(rx).arg(ry)
				.arg(Image::valueR(color)).arg(Image::valueG(color))
				.arg(Image::valueB(color)).arg(Image::valueA(color));
			text += tr("Dist: %1  ").arg(
				m_buffer->GetColorDistance(0, 1, pt.x(), pt.y()), 0, 'g', 4);
		}
	}
	if (m_panes[pane]->selectionVisible()
		&& !m_panes[pane]->selection().isEmpty())
	{
		const QRect sel = m_panes[pane]->selection();
		text += tr("Rc: (%1, %2)  ").arg(sel.width()).arg(sel.height());
	}
	text += tr("Page: %1/%2  Zoom: %3%  %4x%5px  %6bpp  ")
		.arg(m_buffer->GetCurrentPage(pane) + 1)
		.arg(m_buffer->GetPageCount(pane))
		.arg(static_cast<int>(m_zoom * 100))
		.arg(m_buffer->GetImageWidth(pane))
		.arg(m_buffer->GetImageHeight(pane))
		.arg(m_buffer->GetImageBitsPerPixel(pane));
	if (m_buffer->GetVerticalFlip(pane) || m_buffer->GetHorizontalFlip(pane))
	{
		QString flip;
		if (m_buffer->GetVerticalFlip(pane))
			flip += QLatin1Char('V');
		if (m_buffer->GetHorizontalFlip(pane))
			flip += QLatin1Char('H');
		text += tr("Flipped: %1  ").arg(flip);
	}
	if (m_buffer->GetRotation(pane) > 0)
		text += tr("Rotated: %1  ")
			.arg(static_cast<int>(m_buffer->GetRotation(pane)));
	m_paneStatus[pane]->setText(text);
}

void ImageCompareView::updateDiffStatus()
{
	const int count = m_buffer->GetDiffCount();
	const int current = m_buffer->GetCurrentDiffIndex();
	QString text;
	if (count == 0)
		text = tr("Identical");
	else if (current >= 0)
		text = tr("Difference %1 of %2").arg(current + 1).arg(count);
	else if (count == 1)
		text = tr("1 Difference Found");
	else
		text = tr("%1 Differences Found").arg(count);
	m_status->setText(text);
}

void ImageCompareView::updateActions()
{
	if (m_actUndo != nullptr)
		m_actUndo->setEnabled(m_buffer->IsUndoable());
	if (m_actRedo != nullptr)
		m_actRedo->setEnabled(m_buffer->IsRedoable());
	if (m_actSave != nullptr)
		m_actSave->setEnabled(isModified());
}

void ImageCompareView::updateAnimationTimer()
{
	const bool animate = m_buffer->GetBlinkDifferences()
		|| m_buffer->GetOverlayMode() == CImgDiffBuffer::OVERLAY_ALPHABLEND_ANIM;
	if (animate && !m_animTimer->isActive())
		m_animTimer->start();
	else if (!animate && m_animTimer->isActive())
	{
		m_animTimer->stop();
		m_buffer->RefreshImages();
		refreshPanes();
	}
}

void ImageCompareView::syncPanel()
{
	m_syncingPanel = true;
	m_chkHighlight->setChecked(m_buffer->GetShowDifferences());
	m_chkBlink->setChecked(m_buffer->GetBlinkDifferences());
	m_sldBlockSize->setValue(m_buffer->GetDiffBlockSize());
	m_lblBlockSize->setText(QStringLiteral("(%1)")
		.arg(m_buffer->GetDiffBlockSize()));
	m_sldBlockAlpha->setValue(
		static_cast<int>(m_buffer->GetDiffColorAlpha() * 100));
	m_lblBlockAlpha->setText(QStringLiteral("(%1)")
		.arg(static_cast<int>(m_buffer->GetDiffColorAlpha() * 100)));
	m_sldThreshold->setValue(
		thresholdToSlider(m_buffer->GetColorDistanceThreshold()));
	m_lblThreshold->setText(QStringLiteral("(%1)")
		.arg(static_cast<int>(m_buffer->GetColorDistanceThreshold() + 0.5)));
	m_cmbInsDel->setCurrentIndex(
		static_cast<int>(m_buffer->GetInsertionDeletionDetectionMode()));
	m_cmbOverlay->setCurrentIndex(static_cast<int>(m_buffer->GetOverlayMode()));
	m_sldOverlayAlpha->setValue(
		static_cast<int>(m_buffer->GetOverlayAlpha() * 100));
	m_lblOverlayAlpha->setText(QStringLiteral("(%1)")
		.arg(static_cast<int>(m_buffer->GetOverlayAlpha() * 100)));
	m_sldZoom->setValue(static_cast<int>(m_zoom * 8 - 8));
	m_lblZoom->setText(QStringLiteral("(%1%)")
		.arg(static_cast<int>(m_zoom * 100)));
	m_spnPage->setRange(1, qMax(1, m_buffer->GetMaxPageCount()));
	m_spnPage->setValue(m_buffer->GetCurrentMaxPage() + 1);
	m_spnPage->setEnabled(m_buffer->GetMaxPageCount() > 1);
	m_syncingPanel = false;
}
