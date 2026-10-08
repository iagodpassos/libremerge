// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "FileCompareView.h"

#include <QAction>
#include <climits>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include "Dialogs.h"
#include <QProcess>
#include <QScrollBar>
#include <QWheelEvent>
#include <QSettings>
#include <QSplitter>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "DiffTextEdit.h"
#include "DisplayFilterBar.h"
#include "EngineOptions.h"
#include "FileFilterCombo.h"
#include "FileFilters.h"
#include "Icons.h"
#include "GoToDialog.h"
#include "PaneSearch.h"
#include "LocationPane.h"
#include "SyntaxHighlighter.h"
#include "FileOps.h"
#include "MessageBoxes.h"
#include "OptionsDialog.h"
#include "Theme.h"

// engine
#include "DiffWrapper.h"
#include "DiffList.h"
#include "FilterEngine/FilterExpression.h"
#include "FilterEngine/ILineDataProvider.h"
#include "FilterList.h"
#include "LineFilterHelper.h"
#include "MovedLines.h"
#include "OptionsDef.h"
#include "OptionsMgr.h"
#include "PathContext.h"
#include "UniFile.h"
#include "unicoder.h"
#include "stringdiffs.h"

namespace
{

// upstream breaks words at punctuation too (OPT_BREAK_TYPE default 1)
constexpr int kBreakType = 1;
// skip intra-line marks for pathologically large blocks
constexpr int kMaxWordDiffBlockBytes = 256 * 1024;

/** QLabel that elides its text in the middle to fit the available width. */
class ElidedLabel : public QLabel
{
public:
	explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent)
	{
		setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	}

	void setFullText(const QString &text)
	{
		m_fullText = text;
		setToolTip(text);
		updateElision();
	}

protected:
	void resizeEvent(QResizeEvent *event) override
	{
		QLabel::resizeEvent(event);
		updateElision();
	}

private:
	void updateElision()
	{
		const int margin = contentsMargins().left() + contentsMargins().right() + 4;
		setText(fontMetrics().elidedText(m_fullText, Qt::ElideMiddle,
			qMax(20, width() - margin)));
	}

	QString m_fullText;
};

/** Map an inclusive UTF-8 byte range from the engine onto UTF-16 offsets
    of the QString it was encoded from. */
void byteRangeToU16(const QString &line, int beginByte, int endByte,
	int *startU16, int *lengthU16)
{
	int bytePos = 0;
	int start = -1, end = -1;
	const int size = line.size();
	for (int i = 0; i < size;)
	{
		const QChar ch = line.at(i);
		int u16len = 1;
		char32_t cp = ch.unicode();
		if (ch.isHighSurrogate() && i + 1 < size && line.at(i + 1).isLowSurrogate())
		{
			cp = QChar::surrogateToUcs4(ch, line.at(i + 1));
			u16len = 2;
		}
		const int u8len = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
		if (start < 0 && beginByte < bytePos + u8len)
			start = i;
		if (endByte < bytePos + u8len)
		{
			end = i + u16len;
			break;
		}
		bytePos += u8len;
		i += u16len;
	}
	if (start < 0)
		start = size;
	if (end < 0)
		end = size;
	*startU16 = start;
	*lengthU16 = qMax(0, end - start);
}

QString encodingName(int unicoding, int codepage, bool bom)
{
	switch (unicoding)
	{
	case ucr::UCS2LE: return QStringLiteral("UTF-16LE");
	case ucr::UCS2BE: return QStringLiteral("UTF-16BE");
	case ucr::UTF8:
		return bom ? QStringLiteral("UTF-8 BOM") : QStringLiteral("UTF-8");
	default:
		break;
	}
	if (codepage == 65001)
		return QStringLiteral("UTF-8");
	if (codepage >= 1250 && codepage <= 1258)
		return QStringLiteral("Windows-%1").arg(codepage);
	if (codepage == 20127)
		return QStringLiteral("US-ASCII");
	return QStringLiteral("CP%1").arg(codepage);
}

QString eolName(const QString &eol)
{
	if (eol == QStringLiteral("\r\n"))
		return QStringLiteral("Windows");
	if (eol == QStringLiteral("\r"))
		return QStringLiteral("Mac");
	return QStringLiteral("Unix");
}

} // namespace

/**
 * What a line filter expression asks about the lines (CMergeDoc as an
 * ILineDataProvider): the view lines of every pane, ghost lines included,
 * with the flags upstream's text buffers carry, taken once when the
 * filter is about to look at them all.
 */
class FileCompareView::LineProvider : public ILineDataProvider
{
public:
	explicit LineProvider(const FileCompareView *view)
		: m_view(view)
	{
		const int sides = view->m_paneCount;
		for (int side = 0; side < sides; ++side)
		{
			const QTextDocument *doc = view->m_panes[side]->document();
			m_flags[side].assign(doc->blockCount(), 0);
			m_realIndex[side].assign(doc->blockCount(), 0);
			int line = 0;
			int real = 0;
			for (QTextBlock block = doc->begin(); block.isValid(); block = block.next(), ++line)
			{
				// (a ghost line counts as the real line that follows it,
				// as CDiffTextBuffer::ComputeRealLine has it)
				m_realIndex[side][line] = real;
				if (isGhostBlock(block))
					m_flags[side][line] |= LF_GHOST;
				else
					++real;
			}
			m_realCount[side] = real;
		}
		// the difference flags, as PrimeTextBuffers sets them
		for (const Block &block : view->m_blocks)
		{
			if (block.resolved)
				continue;
			for (int side = 0; side < sides; ++side)
			{
				unsigned threeWay = 0;
				if (block.op == OP_1STONLY)
					threeWay = LF_DIFF_1STONLY;
				else if (block.op == OP_2NDONLY)
					threeWay = LF_DIFF_2NDONLY;
				else if (block.op == OP_3RDONLY)
					threeWay = LF_DIFF_3RDONLY;
				for (int line = block.viewBegin; line <= block.viewEnd
					&& line < static_cast<int>(m_flags[side].size()); ++line)
				{
					unsigned &flags = m_flags[side][line];
					flags |= threeWay;
					if (block.trivial)
						flags |= LF_TRIVIAL;
					else if (!(flags & LF_GHOST))
						flags |= LF_DIFF;
				}
			}
		}
		for (int side = 0; side < sides; ++side)
			for (const int real : view->m_movedLines[side])
				if (real >= 0 && real < static_cast<int>(view->m_realToView[side].size()))
				{
					const int line = view->m_realToView[side][real];
					if (line >= 0 && line < static_cast<int>(m_flags[side].size()))
						m_flags[side][line] |= LF_MOVED;
				}
	}

	int GetLineCount() const override
	{
		return static_cast<int>(m_flags[0].size());
	}
	std::string GetLine(int pane, int lineIndex) const override
	{
		if (!valid(pane, lineIndex))
			return {};
		return m_view->m_panes[pane]->document()->findBlockByNumber(lineIndex)
			.text().toStdString();
	}
	// (no table editing in this view: a line is its one column)
	int GetColumnCount(int, int) const override { return 1; }
	std::string GetColumn(int pane, int lineIndex, int) const override
	{
		return GetLine(pane, lineIndex);
	}
	int GetRealLineNumber(int pane, int lineIndex) const override
	{
		return valid(pane, lineIndex) ? m_realIndex[pane][lineIndex] : 0;
	}
	unsigned GetLineFlags(int pane, int lineIndex) const override
	{
		return valid(pane, lineIndex) ? m_flags[pane][lineIndex] : 0;
	}
	unsigned GetLineEol(int pane, int lineIndex) const override
	{
		if (!valid(pane, lineIndex))
			return EOL_NONE;
		// the file's line ending, which every line of a pane shares here;
		// the last line has none when the file ends without one
		const Side &side = m_view->m_sides[pane];
		if (!side.hadFinalEol && m_realIndex[pane][lineIndex] == m_realCount[pane] - 1)
			return EOL_NONE;
		if (side.eol == QStringLiteral("\r\n"))
			return EOL_CRLF;
		return side.eol == QStringLiteral("\r") ? EOL_CR : EOL_LF;
	}

private:
	bool valid(int pane, int lineIndex) const
	{
		return pane >= 0 && pane < m_view->m_paneCount && lineIndex >= 0
			&& lineIndex < static_cast<int>(m_flags[pane].size());
	}

	const FileCompareView *m_view;
	std::vector<unsigned> m_flags[3];
	std::vector<int> m_realIndex[3];
	int m_realCount[3] = {};
};

FileCompareView::~FileCompareView() = default;

FileCompareView::FileCompareView(QWidget *parent)
	: QWidget(parent)
	, m_displayFilter(std::make_unique<LineFilterHelper>())
{
	auto *layout = new QVBoxLayout(this);
	m_layout = layout;
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto *toolbar = new QToolBar(this);
	toolbar->setIconSize(QSize(16, 16));
	toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	// keyboard shortcuts live on the main window's menu (they route to
	// the current tab); the toolbar only mentions them in tooltips
	auto addToolAction = [this, toolbar](lm::Icon icon, const QString &text,
		const QString &shortcutHint, auto slot) -> QAction * {
		QAction *action = toolbar->addAction(lm::icon(icon), text);
		action->setData(static_cast<int>(icon)); // re-rendered on theme change
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
	m_actCopyFromLeft = addToolAction(lm::Icon::CopyRight, tr("Copy to Right"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x92"), [this]() { copyToRight(); });
	m_actCopyFromRight = addToolAction(lm::Icon::CopyLeft, tr("Copy to Left"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x90"),
		[this]() { copyToLeft(); });
	addToolAction(lm::Icon::CopyAllRight, tr("Copy All to Right"),
		QString(), [this]() { copyAllToRight(); });
	addToolAction(lm::Icon::CopyAllLeft, tr("Copy All to Left"),
		QString(), [this]() { copyAllToLeft(); });
	toolbar->addSeparator();
	addToolAction(lm::Icon::Undo, tr("Undo"), QString::fromUtf8("\xE2\x8C\x98Z"),
		[this]() { undoActive(); });
	addToolAction(lm::Icon::Redo, tr("Redo"),
		QString::fromUtf8("\xE2\x87\xA7\xE2\x8C\x98Z"), [this]() { redoActive(); });
	toolbar->addSeparator();
	addToolAction(lm::Icon::Swap, tr("Swap Panes"), QString(),
		[this]() { swapSides(); });
	addToolAction(lm::Icon::Refresh, tr("Recompare"), QStringLiteral("F5"),
		[this]() { refreshByUser(); });
	m_actSave = addToolAction(lm::Icon::Save, tr("Save"),
		QString::fromUtf8("\xE2\x8C\x98S"),
		[this]() { QString error; saveModified(&error); });
	m_actSave->setEnabled(false);
	toolbar->addSeparator();
	m_actDiffPane = addToolAction(lm::Icon::DiffPane, tr("Diff Pane"),
		QString(), [this]() {});
	m_actDiffPane->setCheckable(true);
	m_actDiffPane->setChecked(
		QSettings().value(QStringLiteral("FileCompare/DiffPane"), true).toBool());
	connect(m_actDiffPane, &QAction::toggled, this, [this](bool on) {
		QSettings().setValue(QStringLiteral("FileCompare/DiffPane"), on);
		m_diffPaneWidget->setVisible(on);
		updateDiffPane();
	});
	addToolAction(lm::Icon::Find, tr("Find"), QString::fromUtf8("\xE2\x8C\x98""F"),
		[this]() { showFind(); });
	addToolAction(lm::Icon::Options, tr("Comparison Options"), QString(),
		[this]() { emit optionsRequested(); });
	layout->addWidget(toolbar);

	auto *panes = new QHBoxLayout;
	panes->setContentsMargins(0, 0, 0, 0);
	panes->setSpacing(1);

	m_locationPane = new LocationPane(this);
	panes->addWidget(m_locationPane);
	// upstream's location pane: the cursor to a line (GotoLocation), the
	// panes to a line (its press and drag), the wheel to the active pane,
	// and its menu
	connect(m_locationPane, &LocationPane::gotoRequested, this,
		[this](int line, int side, bool moveAnchor) {
			if (side < m_paneCount)
				gotoLine(viewLineOfShown(line), false, side, moveAnchor);
		});
	connect(m_locationPane, &LocationPane::centerRequested, this,
		&FileCompareView::centerShownLine);
	connect(m_locationPane, &LocationPane::wheelTurned, this, [this](QWheelEvent *event) {
		QWidget *target = m_panes[m_activePane]->viewport();
		const QPointF at(target->width() / 2.0, target->height() / 2.0);
		QWheelEvent forwarded(at, target->mapToGlobal(at), event->pixelDelta(),
			event->angleDelta(), event->buttons(), event->modifiers(), event->phase(),
			event->inverted());
		QApplication::sendEvent(target, &forwarded);
	});
	connect(m_locationPane, &LocationPane::contextMenuRequested, this,
		&FileCompareView::showLocationMenu);

	const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	for (int i = 0; i < 3; ++i)
	{
		auto *column = new QVBoxLayout;
		column->setContentsMargins(0, 0, 0, 0);
		column->setSpacing(0);

		auto *headerRow = new QWidget(this);
		headerRow->setAttribute(Qt::WA_StyledBackground, true);
		auto *headerLayout = new QHBoxLayout(headerRow);
		headerLayout->setContentsMargins(0, 0, 0, 0);
		headerLayout->setSpacing(0);
		auto *header = new ElidedLabel(headerRow);
		header->setContentsMargins(6, 2, 6, 2);
		m_headers[i] = header;
		headerLayout->addWidget(header, 1);
		// WinMerge's per-pane menu button
		auto *menuButton = new QToolButton(headerRow);
		menuButton->setText(QString::fromUtf8("\xE2\x89\xA1"));
		menuButton->setAutoRaise(true);
		menuButton->setFixedWidth(24);
		menuButton->setToolTip(tr("Pane options"));
		menuButton->setFocusPolicy(Qt::NoFocus);
		connect(menuButton, &QToolButton::clicked,
			this, [this, i]() { showHeaderMenu(i); });
		m_headerButtons[i] = menuButton;
		headerLayout->addWidget(menuButton);
		m_headerRows[i] = headerRow;
		column->addWidget(headerRow);

		m_panes[i] = new DiffTextEdit(this);
		m_panes[i]->setLineWrapMode(QPlainTextEdit::NoWrap);
		m_panes[i]->setFont(mono);
		column->addWidget(m_panes[i], 1);

		auto *statusRow = new QHBoxLayout;
		statusRow->setContentsMargins(0, 0, 0, 0);
		statusRow->setSpacing(0);
		m_posLabels[i] = new QLabel(this);
		m_posLabels[i]->setContentsMargins(6, 1, 6, 1);
		m_encLabels[i] = new QLabel(this);
		m_encLabels[i]->setContentsMargins(6, 1, 6, 1);
		statusRow->addWidget(m_posLabels[i], 1);
		statusRow->addWidget(m_encLabels[i]);
		column->addLayout(statusRow);

		panes->addLayout(column, 1);

		// a pane that scrolled, through its scroll bar or by itself; up
		// to Qt 6.5.0 (Debian 12's 6.4, in the AppImage) a QPlainTextEdit
		// sets its own scroll bar with the bar's signals blocked whenever
		// it scrolls by itself (centerCursor, the cursor keys, Find, a
		// click on the bar's track), and only updateRequest, with the
		// distance in dy, tells of those scrolls
		connect(m_panes[i]->verticalScrollBar(), &QScrollBar::valueChanged,
			this, [this, i]() { paneScrolled(i); });
		connect(m_panes[i], &QPlainTextEdit::updateRequest,
			this, [this, i](const QRect &, int dy) {
				if (dy != 0)
					paneScrolled(i);
			});
		connect(m_panes[i]->horizontalScrollBar(), &QScrollBar::valueChanged,
			this, [this, i](int value) { syncHScroll(i, value); });
		connect(m_panes[i], &QPlainTextEdit::cursorPositionChanged,
			this, [this, i]() { updatePaneStatus(i); });
		// double-clicking inside a difference selects it as current,
		// like WinMerge's OnLButtonDblClk
		m_panes[i]->setDoubleClickHook(
			[this](int viewLine) { selectDiffAtViewLine(viewLine); });
		m_panes[i]->setFileDropHook(
			[this, i](const QString &path) { changeSideFile(i, path); });
		m_panes[i]->setContextMenuHook(
			[this, i](QMenu *menu) { buildPaneMenu(i, menu); });
		m_panes[i]->setTabStopDistance(
			4 * QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')));
		// debug spy: LM_DEBUG_EDITS=1 prints every real document edit
		if (qEnvironmentVariableIsSet("LM_DEBUG_EDITS"))
			connect(m_panes[i]->document(), &QTextDocument::contentsChange,
				this, [this, i](int position, int removed, int added) {
					QTextCursor probe(m_panes[i]->document());
					probe.setPosition(qMax(0, position));
					probe.setPosition(qMin(position + added,
						m_panes[i]->document()->characterCount() - 1),
						QTextCursor::KeepAnchor);
					fprintf(stderr,
						"[edit] pane=%d pos=%d removed=%d added=%d text=%s\n",
						i, position, removed, added,
						qPrintable(probe.selectedText()));
				});
		// QTextDocument's own modified tracking ignores syntax-highlight
		// format changes, unlike contentsChange
		connect(m_panes[i]->document(), &QTextDocument::modificationChanged,
			this, [this, i](bool modified) {
				if (m_syncing)
					return;
				if (modified)
					m_diffStale = true;
				setSideModified(i, modified);
			});
		// undo is unified across the panes: remember which document each
		// edit landed on (and where), so Cmd+Z after a merge undoes the
		// merge even though the focus stayed on the other pane
		connect(m_panes[i]->document(), &QTextDocument::undoCommandAdded,
			this, [this, i]() {
				UndoRef ref;
				ref.side = i;
				ref.viewLine = m_panes[i]->textCursor().blockNumber();
				ref.alignment = m_recordingAlignment;
				m_undoOrder.append(ref);
				m_redoOrder.clear();
			});
	}
	// each pane's Find and Replace (upstream's views have their own)
	for (int i = 0; i < 3; ++i)
	{
		PaneSearch::Context context;
		context.eol = [this, i]() { return m_sides[i].eol; };
		context.editable = [this, i]() { return !m_readOnly[i]; };
		context.shownLine = [this](int viewLine) { return shownLineOfView(viewLine); };
		m_search[i] = new PaneSearch(m_panes[i], std::move(context), this);
	}

	// the marker's height is the lines that fit in the first pane
	connect(m_panes[0], &DiffTextEdit::resized,
		this, [this]() { updateLocationViewport(); });

	// WinMerge's diff pane: the current difference's content, one row per
	// file, in a resizable bottom panel
	auto *panesWidget = new QWidget(this);
	panesWidget->setLayout(panes);
	m_diffPaneWidget = new QWidget(this);
	auto *diffPaneLayout = new QVBoxLayout(m_diffPaneWidget);
	diffPaneLayout->setContentsMargins(0, 1, 0, 0);
	diffPaneLayout->setSpacing(1);
	QFont diffPaneFont = mono;
	diffPaneFont.setPointSizeF(qMax(9.0, mono.pointSizeF() - 1));
	for (int i = 0; i < 3; ++i)
	{
		auto *edit = new QPlainTextEdit(m_diffPaneWidget);
		edit->setReadOnly(true);
		edit->setLineWrapMode(QPlainTextEdit::NoWrap);
		edit->setFont(diffPaneFont);
		edit->setMinimumHeight(24);
		m_diffPaneEdits[i] = edit;
		diffPaneLayout->addWidget(edit);
	}
	auto *splitter = new QSplitter(Qt::Vertical, this);
	splitter->addWidget(panesWidget);
	splitter->addWidget(m_diffPaneWidget);
	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 0);
	splitter->setCollapsible(0, false);
	splitter->setSizes({ 560, 140 });
	m_diffPaneWidget->setVisible(m_actDiffPane->isChecked());
	layout->addWidget(splitter, 1);

	// highlight the header of the pane that owns the focus, like WinMerge
	connect(qApp, &QApplication::focusChanged, this,
		[this](QWidget *, QWidget *now) {
			for (int i = 0; i < m_paneCount; ++i)
				if (now == m_panes[i])
				{
					m_activePane = i;
					updateHeaderStyles();
				}
		});
	updateHeaderStyles();

	auto *captionAction = new QAction(tr("Edit Caption"), this);
	captionAction->setShortcut(QKeySequence(Qt::Key_F2));
	captionAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(captionAction);
	connect(captionAction, &QAction::triggered, this, [this]() {
		editCaption(m_activePane);
	});

	m_status = new QLabel(this);
	m_status->setContentsMargins(6, 3, 6, 3);
	layout->addWidget(m_status);

	applyTheme();
	connect(lm::Theme::instance(), &lm::Theme::changed,
		this, [this]() { applyTheme(); });

	const qreal savedSize = QSettings()
		.value(QStringLiteral("FileCompare/FontPointSize")).toReal();
	if (savedSize > 0)
		applyZoom(savedSize);
}

/** Set the comparison text size on the panes, gutters, tab stops and
    the diff pane (which stays one point smaller). */
void FileCompareView::applyZoom(qreal pointSize)
{
	pointSize = qBound<qreal>(6.0, pointSize, 36.0);
	for (int i = 0; i < 3; ++i)
	{
		QFont font = m_panes[i]->font();
		font.setPointSizeF(pointSize);
		m_panes[i]->setFont(font);
		m_panes[i]->setTabStopDistance(
			4 * QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')));
		font.setPointSizeF(qMax<qreal>(6.0, pointSize - 1));
		m_diffPaneEdits[i]->setFont(font);
	}
	// another text size, other lines in the view
	updateLocationViewport();
	QSettings().setValue(QStringLiteral("FileCompare/FontPointSize"), pointSize);
}

void FileCompareView::zoomIn()
{
	applyZoom(m_panes[0]->font().pointSizeF() + 1);
}

void FileCompareView::zoomOut()
{
	applyZoom(m_panes[0]->font().pointSizeF() - 1);
}

void FileCompareView::zoomReset()
{
	applyZoom(QFontDatabase::systemFont(QFontDatabase::FixedFont).pointSizeF());
}

bool FileCompareView::compare(const QStringList &paths, QString *error)
{
	if (paths.size() != 2 && paths.size() != 3)
	{
		if (error != nullptr)
			*error = tr("expected 2 or 3 files");
		return false;
	}
	m_paneCount = paths.size();
	m_locationPane->setPaneCount(m_paneCount);
	for (int i = 0; i < 3; ++i)
	{
		const bool visible = i < m_paneCount;
		m_headerRows[i]->setVisible(visible);
		m_panes[i]->setVisible(visible);
		m_posLabels[i]->setVisible(visible);
		m_encLabels[i]->setVisible(visible);
		m_diffPaneEdits[i]->setVisible(visible);
	}
	// in 3-way the copy commands are relative to the active pane
	// (WinMerge's MenuIDtoXY), so the captions stay "Copy to Right/Left"

	for (int i = 0; i < m_paneCount; ++i)
	{
		if (!loadSide(i, paths.at(i), error))
			return false;
		m_panes[i]->setReadOnly(m_readOnly[i]);
		m_highlighters[i] = std::make_unique<SyntaxHighlighter>(
			m_panes[i]->document(), paths.at(i));
	}
	if (!runDiff(error))
		return false;
	// like WinMerge, open with no difference selected
	m_current = -1;
	applyHighlights();
	updateStatus();
	updateHeaderStyles();
	// the initial ghost alignment is part of opening, not undoable
	resetUndoHistory();
	return true;
}

void FileCompareView::startBlank()
{
	m_paneCount = 2;
	m_locationPane->setPaneCount(m_paneCount);
	for (int i = 0; i < 3; ++i)
	{
		const bool visible = i < m_paneCount;
		m_headerRows[i]->setVisible(visible);
		m_panes[i]->setVisible(visible);
		m_posLabels[i]->setVisible(visible);
		m_encLabels[i]->setVisible(visible);
		m_diffPaneEdits[i]->setVisible(visible);
	}

	// like upstream's DoFileNew: empty untitled buffers, default encoding
	for (int side = 0; side < m_paneCount; ++side)
	{
		Side &s = m_sides[side];
		s = Side();
		s.caption = side == 0 ? tr("Untitled Left") : tr("Untitled Right");
		s.unicoding = ucr::UTF8;
		s.codepage = 65001;
		m_syncing = true;
		m_panes[side]->setPlainText(QString());
		m_panes[side]->document()->setModified(false);
		m_syncing = false;
		m_panes[side]->setReadOnly(false);
		m_highlighters[side].reset();
		updateHeader(side);
		m_encLabels[side]->setText(QStringLiteral("%1  %2")
			.arg(encodingName(s.unicoding, s.codepage, s.bom), eolName(s.eol)));
		updatePaneStatus(side);
	}

	QString error;
	runDiff(&error);
	m_current = -1;
	applyHighlights();
	updateStatus();
	updateHeaderStyles();
	resetUndoHistory();
}

QList<int> FileCompareView::diffLinesForTest() const
{
	QList<int> lines;
	for (const Block &block : m_blocks)
		if (!block.trivial && !block.resolved)
			for (int line = block.viewBegin; line <= block.viewEnd; ++line)
				lines.append(line);
	return lines;
}

QString FileCompareView::tabTitle() const
{
	QStringList names;
	for (int side = 0; side < m_paneCount; ++side)
	{
		const Side &s = m_sides[side];
		names.append(s.path.isEmpty() || s.described
			? (s.caption.isEmpty() ? tr("Untitled") : s.caption)
			: QFileInfo(s.path).fileName());
	}
	return names.join(QString::fromUtf8(" \xE2\x86\x94 "));
}

void FileCompareView::setSideDescription(int side, const QString &description)
{
	if (side < 0 || side >= m_paneCount)
		return;
	m_sides[side].described = true;
	setSideCaption(side, description);
}

/** Whether loadSide would take the file: it opens, and it is text. */
bool FileCompareView::canLoad(const QString &path, QString *error)
{
	// refuse binary files up front: silently comparing them as text ends
	// with a misleading "files are identical" (NUL bytes without a
	// UTF-16/UTF-32 BOM mean binary, the same heuristic diff uses)
	QFile probe(path);
	if (!probe.open(QIODevice::ReadOnly))
	{
		if (error != nullptr)
			*error = tr("cannot open %1").arg(path);
		return false;
	}
	const QByteArray head = probe.read(8192);
	const bool utf16or32Bom = head.startsWith("\xFF\xFE")
		|| head.startsWith("\xFE\xFF")
		|| head.startsWith(QByteArray("\x00\x00\xFE\xFF", 4));
	if (!utf16or32Bom && head.contains('\0'))
	{
		if (error != nullptr)
			*error = tr("%1 appears to be a binary file.\n"
				"LibreMerge compares text files; binary comparison "
				"is not supported yet.").arg(path);
		return false;
	}
	return true;
}

bool FileCompareView::loadSide(int side, const QString &path, QString *error)
{
	if (!canLoad(path, error))
		return false;

	// taken before the content: a change made while reading shows later
	const lm::FileStamp stamp = lm::fileStamp(path);
	UniMemFile file;
	if (!file.OpenReadOnly(path.toStdString()))
	{
		if (error != nullptr)
			*error = tr("cannot open %1").arg(path);
		return false;
	}
	file.ReadBom();

	Side &s = m_sides[side];
	s.path = path;
	s.stamp = stamp;
	s.unicoding = file.GetUnicoding();
	s.codepage = file.GetCodepage();
	s.bom = file.HasBom();

	QStringList lines;
	int crlf = 0, lf = 0, cr = 0;
	String line, eol;
	bool lossy = false;
	bool lastHadEol = true;
	while (file.ReadString(line, eol, &lossy))
	{
		lines.append(QString::fromUtf8(line.data(), static_cast<int>(line.size())));
		if (eol == "\r\n") ++crlf;
		else if (eol == "\n") ++lf;
		else if (eol == "\r") ++cr;
		lastHadEol = !eol.empty();
	}
	file.Close();

	s.hadFinalEol = lines.isEmpty() ? false : lastHadEol;
	if (crlf >= lf && crlf >= cr && crlf > 0)
		s.eol = QStringLiteral("\r\n");
	else if (cr > lf)
		s.eol = QStringLiteral("\r");
	else
		s.eol = QStringLiteral("\n");

	m_syncing = true;
	m_panes[side]->setPlainText(lines.join(QChar('\n')));
	m_panes[side]->document()->setModified(false);
	m_syncing = false;
	s.modified = false;

	updateHeader(side);
	m_encLabels[side]->setText(QStringLiteral("%1  %2")
		.arg(encodingName(s.unicoding, s.codepage, s.bom), eolName(s.eol)));
	updatePaneStatus(side);
	return true;
}

/** The side's real lines (skipping alignment ghosts); optionally also the
    per-view-line ghost flags. */
QStringList FileCompareView::collectRealLines(int side, QList<bool> *ghostFlags) const
{
	QStringList lines;
	for (QTextBlock block = m_panes[side]->document()->begin();
		block.isValid(); block = block.next())
	{
		const bool ghost = isGhostBlock(block);
		if (ghostFlags != nullptr)
			ghostFlags->append(ghost);
		if (!ghost)
			lines.append(block.text());
	}
	return lines;
}

bool FileCompareView::runDiff(QString *error)
{
	// Diff the real pane contents through temp files (the engine's diff
	// core operates on files, like upstream does for edited buffers).
	QTemporaryFile temp[3];
	PathContext paths;
	paths.SetSize(m_paneCount);
	for (int i = 0; i < m_paneCount; ++i)
	{
		m_realLines[i] = collectRealLines(i);
		if (!temp[i].open())
		{
			if (error != nullptr)
				*error = tr("cannot create temporary file");
			return false;
		}
		// every line goes with its line ending, the last one too, as
		// upstream's buffers save them: the engine takes a last line
		// without one for a line that differs from the same line with
		// others after it, and does not see an empty last line at all
		// (a pane with nothing in it stays an empty file)
		QByteArray bytes = m_realLines[i].join(QChar('\n')).toUtf8();
		if (!bytes.isEmpty())
			bytes.append('\n');
		temp[i].write(bytes);
		temp[i].flush();
		paths.SetPath(i, temp[i].fileName().toStdString(), false);
	}

	CDiffWrapper wrapper;
	DIFFOPTIONS options = lm::currentDiffOptions();
	DiffList diffList;
	wrapper.SetCreateDiffList(&diffList);
	wrapper.SetPaths(paths, false);
	wrapper.SetOptions(&options);

	// line filters: diffs whose lines all match an enabled expression
	// become trivial, like WinMerge's Tools > Filters
	if (auto filterList = lm::currentLineFilters())
		wrapper.SetFilterList(filterList);
	// substitution filters: a difference that is only a listed pair
	if (auto substitutions = lm::currentSubstitutionFilters())
		wrapper.SetSubstitutionList(substitutions);
	// "Ignore comment differences" needs the language to tell comments
	// from code: the first pane's, as WinMerge's Rescan takes it (the temp
	// files carry no extension)
	wrapper.SetFilterCommentsSourceDef(
		QFileInfo(m_sides[0].path).suffix().toLower().toStdString());

	COptionsMgr *mgr = GetOptionsMgr();
	const bool detectMoved = mgr != nullptr
		&& mgr->GetBool(OPT_CMP_MOVED_BLOCKS) && m_paneCount == 2;
	wrapper.SetDetectMovedBlocks(detectMoved);

	if (!wrapper.RunFileDiff())
	{
		if (error != nullptr)
			*error = tr("the diff engine failed");
		return false;
	}
	DIFFSTATUS status;
	wrapper.GetDiffStatus(&status);
	if (status.bBinaries)
	{
		// the text diff bailed out; an empty diff list here would show
		// the misleading "files are identical"
		if (error != nullptr)
			*error = tr("the files could not be compared as text");
		m_status->setText(tr("Binary content \xE2\x80\x94 comparison not supported"));
		return false;
	}

	// like upstream's FlagMovedLines: remember which real lines belong
	// to moved blocks, per side, and where the first file's went
	for (int side = 0; side < 3; ++side)
		m_movedLines[side].clear();
	m_movedRight.clear();
	if (detectMoved)
	{
		for (int side = 0; side < 2; ++side)
		{
			MovedLines *moved = wrapper.GetMovedLines(side);
			if (moved == nullptr)
				continue;
			const MovedLines::SIDE other = side == 0
				? MovedLines::SIDE::RIGHT : MovedLines::SIDE::LEFT;
			for (int line = 0; line < m_realLines[side].size(); ++line)
			{
				const int there = moved->LineInBlock(line, other);
				if (there == -1)
					continue;
				m_movedLines[side].insert(line);
				if (side == 0)
					m_movedRight.insert(line, there);
			}
		}
	}

	m_blocks.clear();
	m_diffCount = 0;
	for (int i = 0; i < diffList.GetSize(); ++i)
	{
		DIFFRANGE dr;
		diffList.GetDiff(i, dr);
		Block block{};
		for (int side = 0; side < m_paneCount; ++side)
		{
			block.begin[side] = dr.begin[side];
			block.end[side] = dr.end[side];
		}
		block.trivial = (dr.op == OP_TRIVIAL);
		block.op = dr.op;
		if (!block.trivial)
			++m_diffCount;
		m_blocks.push_back(block);
	}
	m_diffStale = false;
	rebuildAlignment();
	computeWordSpans();
	// upstream's Rescan hides the lines right after the buffers are primed
	hideLines();
	return true;
}

/** Pad every diff block with ghost lines so the panes stay aligned
    line-by-line, WinMerge style. Rebuilds the pane documents only when
    the alignment actually changed (a rebuild clears the undo history). */
void FileCompareView::rebuildAlignment()
{
	QStringList newLines[3];
	QList<bool> newFlags[3];
	int realPos[3] = {};
	int viewPos = 0;
	for (int side = 0; side < m_paneCount; ++side)
		m_realToView[side].clear();

	for (Block &block : m_blocks)
	{
		// identical region before this block: every side advances equally
		const int commonLen = qMax(0, block.begin[0] - realPos[0]);
		for (int side = 0; side < m_paneCount; ++side)
		{
			for (int k = 0; k < commonLen && realPos[side] < m_realLines[side].size(); ++k)
			{
				m_realToView[side].push_back(viewPos + k);
				newLines[side].append(m_realLines[side].at(realPos[side]++));
				newFlags[side].append(false);
			}
		}
		viewPos += commonLen;

		int maxLen = 0;
		for (int side = 0; side < m_paneCount; ++side)
			maxLen = qMax(maxLen, block.end[side] - block.begin[side] + 1);
		block.viewBegin = viewPos;
		block.viewEnd = viewPos + maxLen - 1;
		for (int side = 0; side < m_paneCount; ++side)
		{
			const int len = qMax(0, block.end[side] - block.begin[side] + 1);
			for (int k = 0; k < len; ++k)
			{
				m_realToView[side].push_back(viewPos + k);
				newLines[side].append(m_realLines[side].at(realPos[side]++));
				newFlags[side].append(false);
			}
			for (int k = len; k < maxLen; ++k)
			{
				newLines[side].append(QString());
				newFlags[side].append(true);
			}
		}
		viewPos += maxLen;
	}
	// identical tail
	int tailLen = 0;
	for (int side = 0; side < m_paneCount; ++side)
		tailLen = qMax(tailLen, static_cast<int>(m_realLines[side].size()) - realPos[side]);
	for (int side = 0; side < m_paneCount; ++side)
	{
		while (realPos[side] < m_realLines[side].size())
		{
			m_realToView[side].push_back(newLines[side].size());
			newLines[side].append(m_realLines[side].at(realPos[side]++));
			newFlags[side].append(false);
		}
		while (newLines[side].size() < viewPos + tailLen)
		{
			newLines[side].append(QString());
			newFlags[side].append(true);
		}
	}

	for (int side = 0; side < m_paneCount; ++side)
	{
		QTextDocument *doc = m_panes[side]->document();
		QList<bool> oldFlags;
		const QStringList oldLines = collectRealLines(side, &oldFlags);
		Q_UNUSED(oldLines);
		QStringList currentViewLines;
		QList<bool> rawFlags; // user-data presence, for the undo snapshot
		for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
		{
			currentViewLines.append(b.text());
			rawFlags.append(b.userData() != nullptr);
		}

		const bool changed = currentViewLines != newLines[side]
			|| oldFlags != newFlags[side];
		if (changed)
		{
			const bool wasModified = doc->isModified();
			const int vScroll = m_panes[side]->verticalScrollBar()->value();
			const int hScroll = m_panes[side]->horizontalScrollBar()->value();
			m_syncing = true;
			if (!applyAlignmentEdits(side, currentViewLines, oldFlags,
				rawFlags, newLines[side], newFlags[side]))
			{
				// the real lines diverged from the plan: rebuild the
				// document (this drops the pane's undo history)
				m_panes[side]->setPlainText(newLines[side].join(QChar('\n')));
				m_undoOrder.removeIf(
					[side](const UndoRef &r) { return r.side == side; });
				m_redoOrder.removeIf(
					[side](const UndoRef &r) { return r.side == side; });
			}
			int idx = 0;
			for (QTextBlock b = doc->begin(); b.isValid(); b = b.next(), ++idx)
				b.setUserData(idx < newFlags[side].size() && newFlags[side].at(idx)
					? new GhostBlockData : nullptr);
			doc->setModified(wasModified);
			m_panes[side]->verticalScrollBar()->setValue(vScroll);
			m_panes[side]->horizontalScrollBar()->setValue(hScroll);
			m_syncing = false;
		}

		// gutter numbering: real numbers, blanks on ghosts
		m_lineNumbers[side].clear();
		int realNo = 0;
		for (int v = 0; v < newFlags[side].size(); ++v)
			m_lineNumbers[side].append(newFlags[side].at(v) ? -1 : ++realNo);
		m_panes[side]->setLineNumbers(m_lineNumbers[side]);
		updatePaneStatus(side);
	}
}

/** Rebuild the real<->view maps of one side from the live document (used
    after in-place merges, when the alignment is known to be unchanged). */
void FileCompareView::refreshSideMaps(int side)
{
	m_realToView[side].clear();
	m_lineNumbers[side].clear();
	int view = 0, realNo = 0;
	for (QTextBlock block = m_panes[side]->document()->begin();
		block.isValid(); block = block.next(), ++view)
	{
		if (isGhostBlock(block))
		{
			m_lineNumbers[side].append(-1);
		}
		else
		{
			m_realToView[side].push_back(view);
			m_lineNumbers[side].append(++realNo);
		}
	}
	m_panes[side]->setLineNumbers(m_lineNumbers[side]);
	m_realLines[side] = collectRealLines(side);
}

/** Reshape one pane to a new ghost alignment with minimal cursor edits
    instead of rebuilding the document, so the undo history survives a
    recompare (the real lines are the same sequence by construction;
    only the ghost filler moves). The edits form one undoable command
    tagged as alignment: undo/redo replays it silently around the user's
    own edits, the QTextDocument equivalent of WinMerge's rescan, whose
    ghost operations bypass the undo buffer entirely. Returns false when
    the real lines diverge (the caller falls back to a full rebuild). */
bool FileCompareView::applyAlignmentEdits(int side,
	const QStringList &oldLines, const QList<bool> &oldGhosts,
	const QList<bool> &rawFlags, const QStringList &newLines,
	const QList<bool> &newGhosts)
{
	struct Op
	{
		int oldIndex; // first old view line of the run
		int count;
		bool insert;  // insert `count` ghosts before oldIndex, or
		              // delete the ghost run starting at oldIndex
	};
	std::vector<Op> ops;
	const int oldN = oldLines.size();
	const int newN = newLines.size();
	int i = 0, j = 0;
	while (i < oldN || j < newN)
	{
		const bool oldGhost = i < oldN && oldGhosts.at(i);
		const bool newGhost = j < newN && newGhosts.at(j);
		if (oldGhost && newGhost)
		{
			++i;
			++j;
		}
		else if (oldGhost)
		{
			if (!ops.empty() && !ops.back().insert
				&& ops.back().oldIndex + ops.back().count == i)
				++ops.back().count;
			else
				ops.push_back({ i, 1, false });
			++i;
		}
		else if (newGhost)
		{
			if (!ops.empty() && ops.back().insert
				&& ops.back().oldIndex == i)
				++ops.back().count;
			else
				ops.push_back({ i, 1, true });
			++j;
		}
		else if (i < oldN && j < newN && oldLines.at(i) == newLines.at(j))
		{
			++i;
			++j;
		}
		else
		{
			return false;
		}
	}
	if (ops.empty())
		return true;

	QTextDocument *doc = m_panes[side]->document();
	const int refsBefore = m_undoOrder.size();
	m_recordingAlignment = true;
	QTextCursor cursor(doc);
	cursor.beginEditBlock();
	// backward, so earlier view indices stay valid while editing
	for (auto it = ops.rbegin(); it != ops.rend(); ++it)
	{
		if (it->insert)
		{
			if (it->oldIndex >= doc->blockCount())
				cursor.movePosition(QTextCursor::End);
			else
				cursor.setPosition(
					doc->findBlockByNumber(it->oldIndex).position());
			cursor.insertText(QString(QChar('\n')).repeated(it->count));
		}
		else
		{
			const QTextBlock first = doc->findBlockByNumber(it->oldIndex);
			const QTextBlock last =
				doc->findBlockByNumber(it->oldIndex + it->count - 1);
			int from = first.position() - 1;
			int to = last.position() + last.length() - 1;
			if (from < 0)
			{
				// deleting from the top: take the separator after the
				// run instead of the one before it
				from = 0;
				to = qMin(to + 1, doc->characterCount() - 1);
			}
			cursor.setPosition(from);
			cursor.setPosition(to, QTextCursor::KeepAnchor);
			cursor.removeSelectedText();
		}
	}
	cursor.endEditBlock();
	m_recordingAlignment = false;
	if (m_undoOrder.size() > refsBefore)
		tagLastCommand(side, true, rawFlags, newGhosts);
	else
		// the edits merged into an existing command (defensive: Qt
		// does not merge across edit blocks today); keep that command
		// user-visible, it now carries the realignment too
		tagLastCommand(side, false, QList<bool>(), newGhosts);
	return true;
}

/** Reapply ghost markers to a whole pane. Undo/redo restores the text
    but not the block user data, so programmatic commands carry these
    snapshots. */
void FileCompareView::applyGhostFlags(int side, const QList<bool> &flags)
{
	int idx = 0;
	for (QTextBlock b = m_panes[side]->document()->begin(); b.isValid();
		b = b.next(), ++idx)
		b.setUserData(idx < flags.size() && flags.at(idx)
			? new GhostBlockData : nullptr);
}

/** Attach the flag snapshots (and the alignment marker) to the newest
    undo command recorded for a side. A joined edit block extends an
    existing command, so the entry may predate the current splice; only
    the first snapshot of the before-state is kept. */
void FileCompareView::tagLastCommand(int side, bool alignment,
	const QList<bool> &flagsBefore, const QList<bool> &flagsAfter)
{
	for (int k = m_undoOrder.size() - 1; k >= 0; --k)
	{
		if (m_undoOrder[k].side != side)
			continue;
		m_undoOrder[k].alignment = alignment;
		if (m_undoOrder[k].flagsBefore.isEmpty())
			m_undoOrder[k].flagsBefore = flagsBefore;
		m_undoOrder[k].flagsAfter = flagsAfter;
		return;
	}
}

/** Drop all undo state, documents and cross-pane order alike: the panes
    were rebuilt wholesale and nothing on the stacks matches them. */
void FileCompareView::resetUndoHistory()
{
	m_syncing = true;
	for (int side = 0; side < m_paneCount; ++side)
	{
		QTextDocument *doc = m_panes[side]->document();
		doc->clearUndoRedoStacks();
		// re-anchor the clean state: clearing rewinds the revision but
		// leaves the old clean marker behind, and the next edit-block
		// close (e.g. the highlighter's deferred pass) would otherwise
		// flag the pristine document as modified. setModified(false)
		// alone is a no-op on an already-clean document, hence the
		// round trip.
		doc->setModified(true);
		doc->setModified(false);
	}
	m_syncing = false;
	m_undoOrder.clear();
	m_redoOrder.clear();
}

/** Intra-line (word-level) diff spans, computed like upstream's
    CMergeDoc::GetWordDiffArrayInRange: the whole diff block is joined per
    side and diffed once, so the marks stay meaningful even when the sides
    have different line counts. */
void FileCompareView::computeWordSpans()
{
	m_wordSpans.clear();
	const DIFFOPTIONS options = lm::currentDiffOptions();

	for (size_t b = 0; b < m_blocks.size(); ++b)
	{
		const Block &block = m_blocks[b];
		if (block.trivial || block.resolved)
			continue;

		QList<QByteArray> lineBytes[3];
		String joined[3];
		std::vector<int> lineStart[3];
		bool tooBig = false;
		int nonEmptySides = 0;
		for (int side = 0; side < m_paneCount; ++side)
		{
			String &text = joined[side];
			for (int line = block.begin[side]; line <= block.end[side]; ++line)
			{
				if (line < 0 || line >= m_realLines[side].size())
					continue;
				lineStart[side].push_back(static_cast<int>(text.size()));
				const QByteArray utf8 = m_realLines[side].at(line).toUtf8();
				lineBytes[side].append(utf8);
				text.append(utf8.constData(), utf8.size());
				text += '\n';
			}
			if (!text.empty())
				++nonEmptySides;
			if (text.size() > kMaxWordDiffBlockBytes)
				tooBig = true;
		}
		if (tooBig || nonEmptySides < 2)
			continue;

		// the line ends inside a block compare as the options say
		// (GetWordDiffArrayInRange)
		const strdiff::EolCompareMode eolMode = options.bIgnoreLineBreaks
			? strdiff::EOL_AS_SPACE
			: options.bIgnoreEol ? strdiff::EOL_IGNORE : strdiff::EOL_STRICT;
		const std::vector<strdiff::wdiff> wdiffs = strdiff::ComputeWordDiffs(
			m_paneCount, joined,
			!options.bIgnoreCase, eolMode,
			options.nIgnoreWhitespace, options.bIgnoreNumbers,
			kBreakType, false /*byte_level*/);

		for (const strdiff::wdiff &wd : wdiffs)
		{
			int sidesWithText = 0;
			for (int side = 0; side < m_paneCount; ++side)
				if (wd.end[side] >= wd.begin[side])
					++sidesWithText;
			const bool oneSided = (sidesWithText == 1);

			for (int side = 0; side < m_paneCount; ++side)
			{
				if (wd.end[side] < wd.begin[side])
				{
					// nothing on this side: keep a zero-length span at
					// the insertion point, so a thin marker shows where
					// the other side's text would land (WinMerge draws
					// its zero-width word diffs the same way, issue #6)
					const int pos = wd.begin[side];
					for (int li = 0;
						li < static_cast<int>(lineStart[side].size()); ++li)
					{
						const int start = lineStart[side][li];
						const int len = lineBytes[side].at(li).size();
						if (pos < start || pos > start + len)
							continue;
						WordSpan span;
						span.side = side;
						span.line = block.begin[side] + li;
						span.blockIndex = static_cast<int>(b);
						span.oneSided = true;
						span.start = QString::fromUtf8(
							lineBytes[side].at(li).constData(),
							qBound(0, pos - start, len)).size();
						span.length = 0;
						m_wordSpans.push_back(span);
						break;
					}
					continue;
				}
				// split the byte range on the '\n' joins, one span per line
				for (int li = 0; li < static_cast<int>(lineStart[side].size()); ++li)
				{
					const int start = lineStart[side][li];
					const int len = lineBytes[side].at(li).size();
					const int b0 = qMax(wd.begin[side], start) - start;
					const int b1 = qMin(wd.end[side], start + len - 1) - start;
					if (b1 < b0 || b0 >= len)
						continue;
					WordSpan span;
					span.side = side;
					span.line = block.begin[side] + li;
					span.blockIndex = static_cast<int>(b);
					span.oneSided = oneSided;
					byteRangeToU16(m_realLines[side].at(span.line), b0, b1,
						&span.start, &span.length);
					if (span.length > 0)
						m_wordSpans.push_back(span);
				}
			}
		}
	}
}

void FileCompareView::applyHighlights()
{
	const lm::DiffColors &C = lm::diffColors();
	for (int side = 0; side < m_paneCount; ++side)
	{
		QList<QTextEdit::ExtraSelection> selections;
		QHash<int, QColor> gutterColors;
		QTextDocument *doc = m_panes[side]->document();

		auto addLineSelection = [&](int viewLine, const QColor &color) {
			const QTextBlock textBlock = doc->findBlockByNumber(viewLine);
			if (!textBlock.isValid())
				return;
			QTextEdit::ExtraSelection selection;
			selection.format.setBackground(color);
			selection.format.setProperty(QTextFormat::FullWidthSelection, true);
			selection.cursor = QTextCursor(textBlock);
			selections.append(selection);
			gutterColors.insert(viewLine, color);
		};

		for (size_t b = 0; b < m_blocks.size(); ++b)
		{
			const Block &block = m_blocks[b];
			const bool current = (static_cast<int>(b) == m_current);
			const int len = qMax(0, block.end[side] - block.begin[side] + 1);
			for (int v = block.viewBegin; v <= block.viewEnd; ++v)
			{
				const bool ghost = (v - block.viewBegin) >= len;
				QColor color;
				if (block.trivial)
					color = ghost ? C.trivialDeleted : C.trivial;
				else if (block.resolved)
				{
					if (!ghost)
						continue; // merged: real lines look like common text
					color = C.trivialDeleted;
				}
				else if (!ghost && m_movedLines[side].contains(
					block.begin[side] + (v - block.viewBegin)))
					color = current ? C.selMoved : C.moved;
				else if (current)
					color = ghost ? C.selDiffDeleted : C.selDiff;
				else
					color = ghost ? C.diffDeleted : C.diff;
				addLineSelection(v, color);
			}
		}

		// word-level spans on top of the line backgrounds; zero-length
		// spans become thin insertion markers, painted by the pane
		QList<DiffTextEdit::InsertionMarker> markers;
		for (const WordSpan &span : m_wordSpans)
		{
			if (span.side != side
				|| span.line >= static_cast<int>(m_realToView[side].size()))
				continue;
			const QTextBlock textBlock = doc->findBlockByNumber(
				m_realToView[side][span.line]);
			if (!textBlock.isValid())
				continue;
			const bool current = (span.blockIndex == m_current);
			if (span.length == 0)
			{
				DiffTextEdit::InsertionMarker marker;
				marker.viewLine = m_realToView[side][span.line];
				marker.column = span.start;
				marker.color = current ? C.selWordDiffDeleted
				                       : C.wordDiffDeleted;
				markers.append(marker);
				continue;
			}
			QTextEdit::ExtraSelection selection;
			selection.format.setBackground(current
				? (span.oneSided ? C.selWordDiffDeleted : C.selWordDiff)
				: (span.oneSided ? C.wordDiffDeleted : C.wordDiff));
			QTextCursor cursor(textBlock);
			cursor.setPosition(textBlock.position() + span.start);
			cursor.setPosition(textBlock.position() + span.start + span.length,
				QTextCursor::KeepAnchor);
			selection.cursor = cursor;
			selections.append(selection);
		}
		m_panes[side]->setInsertionMarkers(markers);
		m_panes[side]->setHighlightSelections(selections);
		m_panes[side]->setGutterLineColors(gutterColors);
	}

	// location pane: upstream's blocks (CLocationView::CalculateBlocks and
	// OnDraw). It draws the significant differences only, cut where the
	// lines of a file give way to filler, each part in the colors its first
	// line has on every file; with moved blocks on, a moved part of the
	// first file is joined to where it went on the second. Upstream leaves
	// the last line of a difference at the very end out; not here.
	std::vector<LocationPane::Band> bands;
	std::vector<LocationPane::Ribbon> ribbons;
	COptionsMgr *mgr = GetOptionsMgr();
	const bool showMoved = mgr != nullptr && mgr->GetBool(OPT_CMP_MOVED_BLOCKS)
		&& m_paneCount == 2 && !m_diffStale;
	const int viewLines = m_panes[0]->document()->blockCount();
	// the lines that show, when the display filter hides some
	const auto shownOf = [this](int viewLine) {
		if (m_shownBefore.empty())
			return viewLine;
		return m_shownBefore[qBound(0, viewLine, static_cast<int>(m_shownBefore.size()) - 1)];
	};
	for (size_t b = 0; b < m_blocks.size(); ++b)
	{
		const Block &block = m_blocks[b];
		if (block.trivial || block.viewEnd < block.viewBegin)
			continue;
		const bool current = (static_cast<int>(b) == m_current);
		const int rows = block.viewEnd - block.viewBegin + 1;
		int len[3] = {};
		int firstBlank = INT_MAX;
		int lastBlank = -1;
		for (int side = 0; side < m_paneCount; ++side)
		{
			len[side] = qMax(0, block.end[side] - block.begin[side] + 1);
			if (len[side] < rows)
			{
				firstBlank = qMin(firstBlank, block.viewBegin + len[side]);
				lastBlank = qMax(lastBlank, block.viewBegin + len[side]);
			}
		}
		std::vector<int> cuts{ block.viewBegin };
		for (const int cut : { firstBlank, lastBlank })
			if (cut > cuts.back() && cut <= block.viewEnd)
				cuts.push_back(cut);
		cuts.push_back(qMin(block.viewEnd + 1, viewLines));
		for (size_t c = 0; c + 1 < cuts.size(); ++c)
		{
			const int top = cuts[c];
			const int end = cuts[c + 1];
			if (end <= top)
				continue;
			for (int side = 0; side < m_paneCount; ++side)
			{
				const bool ghost = (top - block.viewBegin) >= len[side];
				QColor color;
				if (block.resolved)
				{
					if (!ghost)
						continue; // merged: real lines look like common text
					color = C.trivialDeleted;
				}
				else if (!ghost && m_movedLines[side].contains(
					block.begin[side] + (top - block.viewBegin)))
					color = current ? C.selMoved : C.moved;
				else if (current)
					color = ghost ? C.selDiffDeleted : C.selDiff;
				else
					color = ghost ? C.diffDeleted : C.diff;
				const int first = shownOf(top);
				const int last = shownOf(end) - 1;
				if (last >= first)
					bands.push_back(LocationPane::Band{ side, first, last, color });
			}
			// (upstream's RightLineInMovedBlock, asked of the part's
			// first line on the first file)
			if (!showMoved || block.resolved || (top - block.viewBegin) >= len[0])
				continue;
			const auto there = m_movedRight.constFind(block.begin[0] + (top - block.viewBegin));
			if (there == m_movedRight.cend() || there.value() < 0
				|| there.value() >= static_cast<int>(m_realToView[1].size()))
				continue;
			const int otherView = m_realToView[1][there.value()];
			const int lines = shownOf(end) - shownOf(top);
			if (lines <= 0)
				continue;
			bool onCurrent = current;
			if (m_current >= 0 && m_current < static_cast<int>(m_blocks.size()))
				onCurrent = onCurrent || (m_blocks[m_current].viewBegin <= otherView
					&& otherView <= m_blocks[m_current].viewEnd);
			LocationPane::Ribbon ribbon{ 0, shownOf(top), shownOf(otherView), lines, onCurrent };
			// a part that goes on from the last one makes one ribbon with it
			if (!ribbons.empty())
			{
				LocationPane::Ribbon &previous = ribbons.back();
				if (previous.firstLine + previous.lines == ribbon.firstLine
					&& previous.otherFirstLine + previous.lines == ribbon.otherFirstLine)
				{
					previous.lines += ribbon.lines;
					previous.current = previous.current || ribbon.current;
					continue;
				}
			}
			ribbons.push_back(ribbon);
		}
	}
	const int totalLines = m_shownBefore.empty() ? viewLines
		: m_shownBefore[static_cast<int>(m_shownBefore.size()) - 1];
	m_locationPane->setBands(std::move(bands), qMax(1, totalLines));
	m_locationPane->setRibbons(std::move(ribbons), C.moved, C.selMoved);
	updateLocationViewport();

	updateDiffPane();
}

void FileCompareView::updateLocationViewport()
{
	int first = m_panes[0]->firstVisibleLine();
	if (!m_shownBefore.empty())
		first = m_shownBefore[qBound(0, first, static_cast<int>(m_shownBefore.size()) - 1)];
	m_locationPane->setViewport(first, m_panes[0]->visibleLineCount());
}

/** Fill the bottom diff pane with the current difference's content, one
    read-only row per file, using the selected-difference colors. */
void FileCompareView::updateDiffPane()
{
	if (!m_actDiffPane->isChecked())
		return;
	const lm::DiffColors &C = lm::diffColors();
	const bool valid = m_current >= 0
		&& m_current < static_cast<int>(m_blocks.size())
		&& !m_blocks[m_current].trivial && !m_blocks[m_current].resolved;
	for (int side = 0; side < m_paneCount; ++side)
	{
		QPlainTextEdit *edit = m_diffPaneEdits[side];
		if (!valid)
		{
			edit->setPlainText(QString());
			edit->setExtraSelections({});
			continue;
		}
		const Block &block = m_blocks[m_current];
		QStringList lines;
		constexpr int kMaxDiffPaneLines = 500;
		for (int line = block.begin[side];
			line <= block.end[side] && line < m_realLines[side].size(); ++line)
		{
			if (lines.size() >= kMaxDiffPaneLines)
			{
				lines.append(tr("\xE2\x80\xA6 (%1 more lines)")
					.arg(block.end[side] - line + 1));
				break;
			}
			lines.append(m_realLines[side].at(qMax(0, line)));
		}
		edit->setPlainText(lines.join(QChar('\n')));

		QList<QTextEdit::ExtraSelection> selections;
		QTextDocument *doc = edit->document();
		for (int i = 0; i < qMax(1, static_cast<int>(lines.size())); ++i)
		{
			const QTextBlock textBlock = doc->findBlockByNumber(i);
			if (!textBlock.isValid())
				continue;
			QTextEdit::ExtraSelection selection;
			selection.format.setBackground(C.selDiff);
			selection.format.setProperty(QTextFormat::FullWidthSelection, true);
			selection.cursor = QTextCursor(textBlock);
			selections.append(selection);
		}
		for (const WordSpan &span : m_wordSpans)
		{
			if (span.side != side || span.blockIndex != m_current)
				continue;
			const QTextBlock textBlock = doc->findBlockByNumber(
				span.line - block.begin[side]);
			if (!textBlock.isValid())
				continue;
			QTextEdit::ExtraSelection selection;
			selection.format.setBackground(
				span.oneSided ? C.selWordDiffDeleted : C.selWordDiff);
			QTextCursor cursor(textBlock);
			cursor.setPosition(textBlock.position() + span.start);
			cursor.setPosition(textBlock.position() + span.start + span.length,
				QTextCursor::KeepAnchor);
			selection.cursor = cursor;
			selections.append(selection);
		}
		edit->setExtraSelections(selections);
	}
}

void FileCompareView::updateStatus()
{
	QString text;
	if (m_diffStale)
		text = tr("Edited \xE2\x80\x94 press F5 to recompare");
	else if (m_diffCount == 0)
		text = tr("Files are identical");
	else
	{
		int index = 0;
		if (m_current >= 0)
		{
			for (int b = 0; b <= m_current && b < static_cast<int>(m_blocks.size()); ++b)
				if (!m_blocks[b].trivial && !m_blocks[b].resolved)
					++index;
		}
		text = index > 0
			? tr("Difference %1 of %2").arg(index).arg(m_diffCount)
			: tr("%n difference(s) found", nullptr, m_diffCount);
	}
	bool modified = false;
	for (int side = 0; side < m_paneCount; ++side)
		modified = modified || m_sides[side].modified;
	if (modified)
		text += tr("  \xE2\x80\xA2 unsaved changes");
	m_status->setText(text);
}

void FileCompareView::updatePaneStatus(int side)
{
	const QTextCursor cursor = m_panes[side]->textCursor();
	const int viewLine = cursor.blockNumber();
	int realLine = viewLine + 1;
	if (!m_lineNumbers[side].isEmpty() && viewLine < m_lineNumbers[side].size())
	{
		realLine = m_lineNumbers[side].at(viewLine);
		for (int v = viewLine; realLine < 0 && v >= 0; --v)
			realLine = m_lineNumbers[side].at(v);
		if (realLine < 0)
			realLine = 1;
	}
	const int col = cursor.positionInBlock() + 1;
	// like WinMerge, the maximum column is the line length + 1
	const int maxCol = qMax(1, cursor.block().length());
	m_posLabels[side]->setText(tr("Ln: %1  Col: %2/%3  Ch: %2/%3")
		.arg(realLine).arg(col).arg(maxCol));
}

void FileCompareView::setSideCaption(int side, const QString &caption)
{
	if (side < 0 || side >= m_paneCount)
		return;
	m_sides[side].caption = caption;
	updateHeader(side);
}

void FileCompareView::updateHeader(int side)
{
	const Side &s = m_sides[side];
	const QString display = s.caption.isEmpty() ? s.path : s.caption;
	static_cast<ElidedLabel *>(m_headers[side])->setFullText(
		(s.modified ? QStringLiteral("* ") : QString()) + display);
}

/** Apply the light (WinMerge classic) or dark palette to every themed
    part of the view: editor panes, gutters, diff pane, location pane,
    status strips, headers, syntax colors and diff highlights. */
void FileCompareView::applyTheme()
{
	const bool dark = lm::Theme::instance()->dark();
	QPalette pal;
	if (dark)
	{
		pal.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
		pal.setColor(QPalette::Text, QColor(0xd4, 0xd4, 0xd4));
		pal.setColor(QPalette::Window, QColor(0x2a, 0x2a, 0x2a));
		pal.setColor(QPalette::PlaceholderText, QColor(0x70, 0x70, 0x70));
		pal.setColor(QPalette::Highlight, QColor(0x26, 0x4f, 0x78));
		pal.setColor(QPalette::HighlightedText, QColor(0xe6, 0xe6, 0xe6));
	}
	else
	{
		pal.setColor(QPalette::Base, Qt::white);
		pal.setColor(QPalette::Text, Qt::black);
		pal.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
		pal.setColor(QPalette::PlaceholderText, QColor(0x88, 0x88, 0x88));
		pal.setColor(QPalette::Highlight, QColor(0xb5, 0xd5, 0xff));
		pal.setColor(QPalette::HighlightedText, Qt::black);
	}
	const QString statusStyle = dark
		? QStringLiteral("QLabel { background: #2c2c2c; color: #b8b8b8; }")
		: QStringLiteral("QLabel { background: #ececec; color: #303030; }");
	for (int i = 0; i < 3; ++i)
	{
		m_panes[i]->setPalette(pal);
		m_diffPaneEdits[i]->setPalette(pal);
		m_posLabels[i]->setStyleSheet(statusStyle);
		m_encLabels[i]->setStyleSheet(statusStyle);
		// syntax palettes are theme-specific: rebuild and rehighlight
		if (m_highlighters[i] != nullptr)
			m_highlighters[i] = std::make_unique<SyntaxHighlighter>(
				m_panes[i]->document(), m_sides[i].path);
	}
	QPalette locPal = m_locationPane->palette();
	locPal.setColor(QPalette::Base,
		dark ? QColor(0x24, 0x24, 0x24) : QColor(Qt::white));
	locPal.setColor(QPalette::Mid,
		dark ? QColor(0x50, 0x50, 0x50) : QColor(0xc0, 0xc0, 0xc0));
	m_locationPane->setPalette(locPal);
	m_status->setStyleSheet(statusStyle);
	lm::applyToolbarTheme(this);
	updateHeaderStyles();
	applyHighlights();
}

void FileCompareView::updateHeaderStyles()
{
	const int active = m_activePane;
	const bool dark = lm::Theme::instance()->dark();
	const QString activeStyle = dark
		? QStringLiteral("QWidget { background: #2d4a6b; border: 1px solid #46689a; }"
			" QLabel { background: transparent; border: none; color: #e8e8e8; font-weight: 600; }"
			" QToolButton { background: transparent; border: none; color: #e8e8e8; }")
		: QStringLiteral("QWidget { background: #d6e4f5; border: 1px solid #a0b0c8; }"
			" QLabel { background: transparent; border: none; color: #101010; font-weight: 600; }"
			" QToolButton { background: transparent; border: none; color: #101010; }");
	const QString inactiveStyle = dark
		? QStringLiteral("QWidget { background: #303030; border: 1px solid #454545; }"
			" QLabel { background: transparent; border: none; color: #b0b0b0; }"
			" QToolButton { background: transparent; border: none; color: #b0b0b0; }")
		: QStringLiteral("QWidget { background: #ececec; border: 1px solid #c0c0c0; }"
			" QLabel { background: transparent; border: none; color: #404040; }"
			" QToolButton { background: transparent; border: none; color: #404040; }");
	for (int i = 0; i < 3; ++i)
		m_headerRows[i]->setStyleSheet(i == active ? activeStyle : inactiveStyle);
}

int FileCompareView::firstVisibleViewLine() const
{
	return m_panes[0]->firstVisibleLine();
}

int FileCompareView::cursorViewLineForTest(int side) const
{
	return m_panes[side]->textCursor().blockNumber();
}

void FileCompareView::setCursorViewLineForTest(int side, int viewLine)
{
	m_activePane = side;
	m_panes[side]->setTextCursor(QTextCursor(
		m_panes[side]->document()->findBlockByNumber(viewLine)));
}

void FileCompareView::selectAllAndCopyForTest(int side)
{
	m_panes[side]->selectAll();
	m_panes[side]->copy();
}

void FileCompareView::typeAtForTest(int side, int viewLine, const QString &text)
{
	const QTextBlock block =
		m_panes[side]->document()->findBlockByNumber(viewLine);
	if (!block.isValid())
		return;
	QTextCursor cursor(block);
	cursor.insertText(text);
}

QStringList FileCompareView::paths() const
{
	QStringList result;
	for (int side = 0; side < m_paneCount; ++side)
		result.append(m_sides[side].path);
	return result;
}

void FileCompareView::showHeaderMenu(int side)
{
	const QString path = m_sides[side].path;
	QMenu menu(this);
	QAction *copyPath = menu.addAction(tr("Copy Full Path"), this, [path]() {
		QApplication::clipboard()->setText(path);
	});
	copyPath->setEnabled(!path.isEmpty());
	QAction *copyName = menu.addAction(tr("Copy Filename"), this, [path]() {
		QApplication::clipboard()->setText(QFileInfo(path).fileName());
	});
	copyName->setEnabled(!path.isEmpty());
	menu.addSeparator();
	QAction *caption = menu.addAction(tr("Edit Caption\xE2\x80\xA6"), this,
		[this, side]() { editCaption(side); });
	caption->setShortcut(QKeySequence(Qt::Key_F2));
	menu.addSeparator();
	menu.addAction(tr("Open File\xE2\x80\xA6"), this, [this, side, path]() {
		const QString chosen = QFileDialog::getOpenFileName(this,
			tr("Open File"), QFileInfo(path).absolutePath());
		if (!chosen.isEmpty())
			changeSideFile(side, chosen);
	});
#ifdef Q_OS_MACOS
	QAction *reveal = menu.addAction(tr("Reveal in Finder"), this, [path]() {
		QProcess::startDetached(QStringLiteral("/usr/bin/open"),
			{ QStringLiteral("-R"), path });
	});
#else
	QAction *reveal = menu.addAction(tr("Show in File Manager"), this, [path]() {
		QDesktopServices::openUrl(
			QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
	});
#endif
	reveal->setEnabled(!path.isEmpty());
	QMenu *recent = menu.addMenu(tr("Recent Files"));
	const QStringList history =
		QSettings().value(QStringLiteral("NewComparison/History")).toStringList();
	for (const QString &entry : history)
	{
		if (entry == path || !QFileInfo(entry).isFile())
			continue;
		recent->addAction(
			menu.fontMetrics().elidedText(entry, Qt::ElideMiddle, 420),
			this, [this, side, entry]() { changeSideFile(side, entry); });
	}
	recent->setEnabled(!recent->isEmpty());

	menu.exec(m_headerButtons[side]->mapToGlobal(
		QPoint(0, m_headerButtons[side]->height())));
}

void FileCompareView::editCaption(int side)
{
	bool ok = false;
	const QString text = QInputDialog::getText(this, tr("Edit Caption"),
		tr("Caption for this pane (leave empty to show the file path):"),
		QLineEdit::Normal, m_sides[side].caption, &ok);
	if (!ok)
		return;
	m_sides[side].caption = text.trimmed();
	updateHeader(side);
}

/** Load a different file into one pane and recompare, like WinMerge's
    per-pane Open. */
void FileCompareView::changeSideFile(int side, const QString &path)
{
	if (path.isEmpty() || path == m_sides[side].path)
		return;
	if (m_sides[side].modified
		&& lm::question(this, tr("LibreMerge"),
			tr("Discard unsaved changes in this pane?")) != QMessageBox::Yes)
		return;

	QString error;
	if (!loadSide(side, path, &error))
	{
		lm::warning(this, tr("LibreMerge"), error);
		return;
	}
	m_sides[side].caption.clear();
	m_panes[side]->setReadOnly(m_readOnly[side]);
	m_highlighters[side] = std::make_unique<SyntaxHighlighter>(
		m_panes[side]->document(), path);
	setSideModified(side, false);
	// the reload dropped this pane's undo stack (the other panes keep
	// theirs; the recompare below realigns them undoably)
	m_undoOrder.removeIf([side](const UndoRef &r) { return r.side == side; });
	m_redoOrder.removeIf([side](const UndoRef &r) { return r.side == side; });

	QSettings settings;
	const QString historyKey = QStringLiteral("NewComparison/History");
	QStringList history = settings.value(historyKey).toStringList();
	history.removeAll(path);
	history.prepend(path);
	while (history.size() > 12)
		history.removeLast();
	settings.setValue(historyKey, history);

	recompare();

	// changing a pane's file re-applies "scroll to first difference",
	// like WinMerge (CMergeDoc::ChangeFile ends in MoveOnLoad) - this
	// covers dropping files onto the panes of a File > New comparison.
	// Deferred one cycle so the reloaded documents are laid out.
	if (OptionsDialog::scrollToFirstDiff())
	{
		QTimer::singleShot(0, this, [this]() {
			gotoFirstDiff();
			if (OptionsDialog::scrollToFirstInlineDiff())
				scrollToFirstInlineDiff();
		});
	}

	emit pathsChanged();
}

/** The next difference to stop at: not an ignored one, not one merged
    since the comparison, and not one the display filter hides whole
    (upstream's Find...NonFilteredDiff). */
int FileCompareView::nextActive(int from, int direction) const
{
	for (int b = from + direction; b >= 0 && b < static_cast<int>(m_blocks.size()); b += direction)
		if (!m_blocks[b].trivial && !m_blocks[b].resolved && !blockFiltered(b))
			return b;
	return -1;
}

void FileCompareView::gotoDiff(int blockIndex, bool center)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(m_blocks.size()))
		return;
	m_current = blockIndex;
	for (int side = 0; side < m_paneCount; ++side)
	{
		const QTextBlock cursorBlock = m_panes[side]->document()
			->findBlockByNumber(qMax(0, m_blocks[blockIndex].viewBegin));
		QTextCursor cursor(cursorBlock);
		m_syncing = true;
		m_panes[side]->setTextCursor(cursor);
		// centerCursor dereferences the block's QTextLine; a freshly
		// replaced document may not be laid out yet (crashed on macOS)
		if (center && cursorBlock.layout() != nullptr
			&& cursorBlock.layout()->lineCount() > 0)
			m_panes[side]->centerCursor();
		m_syncing = false;
	}
	applyHighlights();
	updateStatus();
}

void FileCompareView::gotoNextDiff()
{
	if (m_diffStale)
		recompare();
	int next = -1;
	if (m_current >= 0)
		next = nextActive(m_current, +1);
	else
	{
		// no selected diff: search from the cursor, like upstream's
		// DiffList::NextSignificantDiffFromLine
		const int line = m_panes[m_activePane]->textCursor().blockNumber();
		for (int b = 0; b < static_cast<int>(m_blocks.size()); ++b)
		{
			if (!m_blocks[b].trivial && !m_blocks[b].resolved
				&& m_blocks[b].viewBegin >= line && !blockFiltered(b))
			{
				next = b;
				break;
			}
		}
	}
	if (next >= 0)
		gotoDiff(next);
}

void FileCompareView::gotoPrevDiff()
{
	if (m_diffStale)
		recompare();
	int prev = -1;
	if (m_current >= 0)
		prev = nextActive(m_current, -1);
	else
	{
		const int line = m_panes[m_activePane]->textCursor().blockNumber();
		for (int b = static_cast<int>(m_blocks.size()) - 1; b >= 0; --b)
		{
			if (!m_blocks[b].trivial && !m_blocks[b].resolved
				&& m_blocks[b].viewEnd <= line && !blockFiltered(b))
			{
				prev = b;
				break;
			}
		}
	}
	if (prev >= 0)
		gotoDiff(prev);
}

void FileCompareView::refreshByUser()
{
	// WinMerge's Rescan starts with CheckFileChanged; a Yes there has
	// reloaded the files by the time the comparison runs
	emit aboutToRescan();
	recompare();
	emit rescanned();
}

bool FileCompareView::encodingsDiffer() const
{
	for (int side = 1; side < m_paneCount; ++side)
		if (m_sides[side].unicoding != m_sides[0].unicoding
			|| m_sides[side].codepage != m_sides[0].codepage
			|| m_sides[side].bom != m_sides[0].bom)
			return true;
	return false;
}

QString FileCompareView::changedPathOnDisk() const
{
	for (int side = 0; side < m_paneCount; ++side)
		if (lm::fileChangedOnDisk(m_sides[side].path, m_sides[side].stamp)
			== lm::FileChange::Changed)
			return m_sides[side].path;
	return QString();
}

bool FileCompareView::reload(QString *error)
{
	// refuse before touching anything: a pane already replaced could not
	// be put back (WinMerge closes the comparison at this point instead)
	for (int side = 0; side < m_paneCount; ++side)
		if (!m_sides[side].path.isEmpty() && !canLoad(m_sides[side].path, error))
			return false;

	const bool wasModified = isModified();
	// MoveOnLoad(nActivePane, pt.y): back to the cursor's line afterwards
	const int cursorLine = m_panes[m_activePane]->textCursor().blockNumber();
	bool loaded = true;
	for (int side = 0; side < m_paneCount && loaded; ++side)
	{
		Side &s = m_sides[side];
		if (!s.path.isEmpty())
		{
			loaded = loadSide(side, s.path, error);
			continue;
		}
		// an untitled pane starts over empty, like upstream's InitNew
		m_syncing = true;
		m_panes[side]->setPlainText(QString());
		m_panes[side]->document()->setModified(false);
		m_syncing = false;
		s.modified = false;
		updateHeader(side);
		updatePaneStatus(side);
	}

	// a file that went away between the check and the load leaves the
	// panes loaded so far: compare what is there either way
	QString diffError;
	const bool compared = runDiff(&diffError);
	m_current = -1;
	applyHighlights();
	updateStatus();
	updateHeaderStyles();
	// like opening, nothing of the reload is undoable
	resetUndoHistory();
	if (m_actSave != nullptr)
		m_actSave->setEnabled(isModified());
	if (wasModified != isModified())
		emit modifiedChanged(isModified());

	// deferred one cycle so the reloaded documents are laid out
	QTimer::singleShot(0, this, [this, cursorLine]() {
		for (int side = 0; side < m_paneCount; ++side)
		{
			QTextDocument *doc = m_panes[side]->document();
			const QTextBlock block = doc->findBlockByNumber(
				qBound(0, cursorLine, doc->blockCount() - 1));
			m_syncing = true;
			m_panes[side]->setTextCursor(QTextCursor(block));
			if (block.layout() != nullptr && block.layout()->lineCount() > 0)
				m_panes[side]->centerCursor();
			m_syncing = false;
		}
	});

	if (loaded && !compared && error != nullptr)
		*error = diffError;
	return loaded && compared;
}

void FileCompareView::recompare()
{
	QString error;
	if (!runDiff(&error))
		return;
	m_current = -1;
	applyHighlights();
	updateStatus();
}

/** Merge one block from sourceSide into the target pane, updating the
    model in place (a full recompare would rebuild the documents and
    clear the undo history). The caller refreshes maps/highlights. */
void FileCompareView::applyBlockCopy(int blockIndex, int sourceSide,
	int targetSide, bool joinUndo)
{
	Block &block = m_blocks[blockIndex];
	const int target = targetSide;

	// the panes are ghost-aligned: the block occupies the same view range
	// everywhere, so the merge is an equal-length line replacement
	QTextDocument *srcDoc = m_panes[sourceSide]->document();
	QTextDocument *tgtDoc = m_panes[target]->document();
	QStringList newLines;
	QList<bool> ghost;
	for (int v = block.viewBegin; v <= block.viewEnd; ++v)
	{
		const QTextBlock b = srcDoc->findBlockByNumber(v);
		newLines.append(b.text());
		ghost.append(isGhostBlock(b));
	}

	const QTextBlock firstBlock = tgtDoc->findBlockByNumber(block.viewBegin);
	const QTextBlock lastBlock = tgtDoc->findBlockByNumber(block.viewEnd);
	if (!firstBlock.isValid() || !lastBlock.isValid())
		return;
	// snapshot of the target's ghost markers for undo/redo: Qt restores
	// the text but not the block user data. A joined step already has a
	// before-state on its command; reuse its after-state as the base.
	QList<bool> flagsBase;
	if (joinUndo)
	{
		for (int k = m_undoOrder.size() - 1; k >= 0; --k)
			if (m_undoOrder[k].side == target)
			{
				flagsBase = m_undoOrder[k].flagsAfter;
				break;
			}
	}
	if (flagsBase.isEmpty())
		for (QTextBlock b = tgtDoc->begin(); b.isValid(); b = b.next())
			flagsBase.append(b.userData() != nullptr);
	m_syncing = true;
	QTextCursor cursor(tgtDoc);
	// position before opening the edit block: undo restores the cursor
	// to the macro's starting position, which must be the merge spot,
	// not the top of the document
	cursor.setPosition(firstBlock.position());
	if (joinUndo)
		cursor.joinPreviousEditBlock();
	else
		cursor.beginEditBlock();
	cursor.setPosition(lastBlock.position() + lastBlock.length() - 1,
		QTextCursor::KeepAnchor);
	cursor.insertText(newLines.join(QChar('\n')));
	cursor.endEditBlock();
	for (int v = block.viewBegin; v <= block.viewEnd; ++v)
		tgtDoc->findBlockByNumber(v).setUserData(
			ghost.at(v - block.viewBegin) ? new GhostBlockData : nullptr);
	m_syncing = false;
	QList<bool> flagsAfter = flagsBase;
	for (int v = block.viewBegin; v <= block.viewEnd && v < flagsAfter.size(); ++v)
		flagsAfter[v] = ghost.at(v - block.viewBegin);
	tagLastCommand(target, false,
		joinUndo ? QList<bool>() : flagsBase, flagsAfter);
	for (int k = m_undoOrder.size() - 1; k >= 0; --k)
		if (m_undoOrder[k].side == target)
		{
			m_undoOrder[k].merge = true;
			break;
		}

	const int srcLen = qMax(0, block.end[sourceSide] - block.begin[sourceSide] + 1);
	const int tgtLen = qMax(0, block.end[target] - block.begin[target] + 1);
	const int delta = srcLen - tgtLen;
	block.end[target] = block.begin[target] + srcLen - 1;
	for (size_t b2 = blockIndex + 1; b2 < m_blocks.size(); ++b2)
	{
		m_blocks[b2].begin[target] += delta;
		m_blocks[b2].end[target] += delta;
	}
	block.resolved = true;
	--m_diffCount;
	m_wordSpans.erase(std::remove_if(m_wordSpans.begin(), m_wordSpans.end(),
		[blockIndex](const WordSpan &s) { return s.blockIndex == blockIndex; }),
		m_wordSpans.end());
	// spans of later blocks reference real line numbers, which shifted
	for (WordSpan &s : m_wordSpans)
		if (s.blockIndex > blockIndex && s.side == target)
			s.line += delta;
}

void FileCompareView::copyToRight(bool advance)
{
	// WinMerge's ID_L2R: relative to the active pane, so the middle pane
	// of a 3-way pushes into the right pane
	const int target = qMin(m_activePane + 1, m_paneCount - 1);
	copyCurrentDiff(target - 1, target, advance);
}

void FileCompareView::copyToLeft(bool advance)
{
	const int target = qMax(m_activePane - 1, 0);
	copyCurrentDiff(target + 1, target, advance);
}

void FileCompareView::copyAllToRight()
{
	const int target = qMin(m_activePane + 1, m_paneCount - 1);
	copyAllFrom(target - 1, target);
}

void FileCompareView::copyAllToLeft()
{
	const int target = qMax(m_activePane - 1, 0);
	copyAllFrom(target + 1, target);
}

void FileCompareView::copyCurrentDiff(int sourceSide, int targetSide, bool advance)
{
	if (m_diffStale)
		recompare();
	if (sourceSide >= m_paneCount || targetSide >= m_paneCount
		|| sourceSide == targetSide)
		return;
	if (m_current < 0)
		m_current = nextActive(-1, +1);
	if (m_current < 0 || m_current >= static_cast<int>(m_blocks.size()))
		return;
	if (m_blocks[m_current].trivial || m_blocks[m_current].resolved)
		return;
	const int target = targetSide;
	if (m_readOnly[target])
	{
		m_status->setText(tr("The merge target is read-only."));
		return;
	}

	const int mergedViewBegin = m_blocks[m_current].viewBegin;
	applyBlockCopy(m_current, sourceSide, target, false);
	refreshSideMaps(target);
	setSideModified(target, true);
	// the lines the copy wrote anew are hidden where they were
	applyHiddenLines();

	if (advance)
	{
		// "copy and advance": land on the next remaining difference
		int next = nextActive(m_current, +1);
		if (next < 0)
			next = nextActive(m_current, -1);
		if (next >= 0)
		{
			gotoDiff(next);
			return;
		}
	}
	// like WinMerge's plain copy: stay on the merged spot
	m_current = -1;
	for (int side = 0; side < m_paneCount; ++side)
	{
		QTextCursor cursor(m_panes[side]->document()->findBlockByNumber(
			qMax(0, mergedViewBegin)));
		m_syncing = true;
		m_panes[side]->setTextCursor(cursor);
		m_syncing = false;
	}
	applyHighlights();
	updateStatus();
}

void FileCompareView::copyAllFrom(int sourceSide, int targetSide)
{
	if (m_diffStale)
		recompare();
	if (sourceSide >= m_paneCount || targetSide >= m_paneCount
		|| sourceSide == targetSide)
		return;
	const int target = targetSide;
	if (m_readOnly[target])
	{
		m_status->setText(tr("The merge target is read-only."));
		return;
	}
	// CMergeDoc::CopyMultipleList: not with hidden lines among the
	// differences to copy
	if (hasInvisibleLines())
	{
		lm::showError(this, tr("Merging/copying differences that contain hidden lines is "
			"not currently supported.\n\nPlease clear the display filter or adjust the "
			"filter settings to show all lines before merging."));
		return;
	}

	bool first = true;
	for (int b = 0; b < static_cast<int>(m_blocks.size()); ++b)
	{
		if (m_blocks[b].trivial || m_blocks[b].resolved)
			continue;
		// one undoable step for the whole merge
		applyBlockCopy(b, sourceSide, target, !first);
		first = false;
	}
	if (first)
		return; // nothing to copy
	refreshSideMaps(target);
	setSideModified(target, true);
	applyHiddenLines();
	m_current = -1;
	applyHighlights();
	updateStatus();
}

void FileCompareView::gotoFirstDiff()
{
	if (m_diffStale)
		recompare();
	const int firstBlock = nextActive(-1, +1);
	if (firstBlock >= 0)
		gotoDiff(firstBlock);
}

void FileCompareView::scrollToFirstInlineDiff()
{
	// WinMerge's "scroll to first inline difference": place the cursor on
	// the first word-level difference of the current block, so long lines
	// scroll horizontally to where the change actually is. Like upstream
	// (worddiffs[0].begin[m_nThisPane]), the active pane's own span is
	// preferred, so the pane the user is looking at lands exactly on it.
	if (m_current < 0)
		return;
	const WordSpan *chosen = nullptr;
	for (const WordSpan &span : m_wordSpans)
	{
		if (span.blockIndex != m_current)
			continue;
		if (chosen == nullptr)
			chosen = &span;
		if (span.side == m_activePane)
		{
			chosen = &span;
			break;
		}
	}
	if (chosen == nullptr)
		return;
	if (qEnvironmentVariableIsSet("LM_DEBUG_EDITS"))
	{
		for (const WordSpan &s : m_wordSpans)
			if (s.blockIndex == m_current)
				fprintf(stderr, "[span] side=%d line=%d start=%d len=%d one=%d%s\n",
					s.side, s.line, s.start, s.length, s.oneSided,
					&s == chosen ? "  <-- escolhido" : "");
	}
	const WordSpan &span = *chosen;
	if (span.line < 0
		|| span.line >= static_cast<int>(m_realToView[span.side].size()))
		return;
	const int viewLine = m_realToView[span.side][span.line];
	DiffTextEdit *paneEdit = m_panes[span.side];
	const QTextBlock block =
		paneEdit->document()->findBlockByNumber(viewLine);
	if (!block.isValid())
		return;
	const int position = block.position()
		+ qMin(span.start, static_cast<int>(block.length()) - 1);
	QTextCursor cursor(block);
	cursor.setPosition(position);
	paneEdit->setTextCursor(cursor);

	// explicit horizontal scroll: WinMerge's EnsureVisible leaves the
	// inline difference ~5 characters from the left edge. The target x
	// comes from the block's real layout (cursorToX handles tab stops
	// and formats; raw font metrics overshot on lines with tabs, hiding
	// the change off-screen to the left), forcing the lazy layout first.
	// QPlainTextEdit's horizontal range only exists after the first
	// paint - when it is not there yet, the value is applied on the
	// scrollbar's own rangeChanged. No m_syncing guard: the sibling
	// panes follow, like WinMerge's UpdateSiblingScrollPos.
	paneEdit->document()->documentLayout()->blockBoundingRect(block);
	const QFontMetricsF metrics(paneEdit->font());
	int x = -1;
	if (block.layout() != nullptr)
	{
		const QTextLine line =
			block.layout()->lineForTextPosition(position - block.position());
		if (line.isValid())
			x = qRound(line.cursorToX(position - block.position()));
	}
	if (x < 0) // layout unavailable: metrics approximation
		x = qRound(metrics.horizontalAdvance(block.text().left(span.start)));
	const int margin = qRound(5 * metrics.horizontalAdvance(QLatin1Char(' ')));
	QScrollBar *hbar = paneEdit->horizontalScrollBar();
	auto apply = [paneEdit, hbar, x, margin]() {
		if (x > paneEdit->viewport()->width() - margin)
			hbar->setValue(qMax(0, x - margin));
	};
	if (hbar->maximum() > 0)
		apply();
	else
	{
		// the horizontal range appears after the first paint
		auto conn = std::make_shared<QMetaObject::Connection>();
		*conn = connect(hbar, &QAbstractSlider::rangeChanged,
			paneEdit, [apply, conn](int, int max) {
				if (max <= 0)
					return;
				QObject::disconnect(*conn);
				apply();
			});
	}
	// starting the app straight into a comparison relayouts the panes
	// when the window first shows, resetting the scroll: re-assert once
	// after the startup settles (idempotent)
	QTimer::singleShot(300, paneEdit, apply);
}

void FileCompareView::gotoLastDiff()
{
	if (m_diffStale)
		recompare();
	const int lastBlock = nextActive(static_cast<int>(m_blocks.size()), -1);
	if (lastBlock >= 0)
		gotoDiff(lastBlock);
}

void FileCompareView::swapSides()
{
	if (m_paneCount < 2)
		return;
	if (isModified() && lm::question(this, tr("LibreMerge"),
		tr("Swapping reloads both files. Discard unsaved changes?"))
			!= QMessageBox::Yes)
		return;

	QStringList newPaths = paths();
	const int last = newPaths.size() - 1;
	newPaths.swapItemsAt(0, last);
	std::swap(m_readOnly[0], m_readOnly[last]);
	QString captions[3];
	for (int i = 0; i < m_paneCount; ++i)
		captions[i] = m_sides[i].caption;
	std::swap(captions[0], captions[last]);

	QString error;
	if (!compare(newPaths, &error))
	{
		lm::warning(this, tr("LibreMerge"), error);
		return;
	}
	for (int i = 0; i < m_paneCount; ++i)
	{
		m_sides[i].caption = captions[i];
		updateHeader(i);
	}
	setSideModified(0, false);
	emit pathsChanged();
}

/** After an undo/redo that kept every pane's line count (merge splices
    always do), the alignment still holds: recompare in place to refresh
    the highlights without touching the undo stacks. Line-count changes
    fall back to the stale-marker flow (F5), which may rebuild. */
void FileCompareView::refreshAfterUndoRedo(const int countsBefore[3])
{
	for (int side = 0; side < m_paneCount; ++side)
		if (m_panes[side]->document()->blockCount() != countsBefore[side])
			return; // stale marker already set by the modified listener
	recompare();
}

void FileCompareView::undoActive()
{
	int counts[3] = {};
	for (int side = 0; side < m_paneCount; ++side)
		counts[side] = m_panes[side]->document()->blockCount();
	const int topLine = m_panes[0]->verticalScrollBar()->value();
	bool undidReal = false;
	bool undidAny = false;
	UndoRef realRef;
	while (!m_undoOrder.isEmpty() && !undidReal)
	{
		const UndoRef ref = m_undoOrder.takeLast();
		if (ref.side >= m_paneCount
			|| !m_panes[ref.side]->document()->isUndoAvailable())
			continue; // stale entry (e.g. a rebuild cleared that stack)
		if (ref.alignment)
		{
			// a recompare's ghost edits: replay them silently and keep
			// going, the user asked to undo their own edit (WinMerge's
			// rescan ghosts live outside the undo history)
			m_syncing = true;
			m_panes[ref.side]->undo();
			if (!ref.flagsBefore.isEmpty())
				applyGhostFlags(ref.side, ref.flagsBefore);
			if (!m_sides[ref.side].modified)
				m_panes[ref.side]->document()->setModified(false);
			m_syncing = false;
		}
		else
		{
			m_panes[ref.side]->undo();
			if (!ref.flagsBefore.isEmpty())
				applyGhostFlags(ref.side, ref.flagsBefore);
			undidReal = true;
			realRef = ref;
		}
		refreshSideMaps(ref.side);
		m_redoOrder.append(ref);
		undidAny = true;
	}
	if (undidAny)
	{
		refreshAfterUndoRedo(counts);
		// like WinMerge's OnEditUndo: the viewport stays where it was,
		// and the difference at the undone spot is selected again so it
		// can simply be merged once more
		m_syncing = true;
		for (int i = 0; i < m_paneCount; ++i)
			m_panes[i]->verticalScrollBar()->setValue(topLine);
		m_syncing = false;
		if (undidReal && realRef.merge)
			selectDiffAtViewLine(realRef.viewLine);
		else if (!undidReal)
		{
			m_diffStale = true;
			updateStatus();
		}
		return;
	}
	m_panes[m_activePane]->undo();
}

void FileCompareView::redoActive()
{
	int counts[3] = {};
	for (int side = 0; side < m_paneCount; ++side)
		counts[side] = m_panes[side]->document()->blockCount();
	const int topLine = m_panes[0]->verticalScrollBar()->value();
	bool redidReal = false;
	bool redidAny = false;
	while (!m_redoOrder.isEmpty())
	{
		const UndoRef ref = m_redoOrder.last();
		if (ref.side >= m_paneCount
			|| !m_panes[ref.side]->document()->isRedoAvailable())
		{
			m_redoOrder.removeLast();
			continue;
		}
		// one user edit per redo; the alignment edits that followed it
		// (a recompare between the edit and now) replay silently after
		if (redidReal && !ref.alignment)
			break;
		m_redoOrder.removeLast();
		if (ref.alignment)
		{
			m_syncing = true;
			m_panes[ref.side]->redo();
			if (!ref.flagsAfter.isEmpty())
				applyGhostFlags(ref.side, ref.flagsAfter);
			if (!m_sides[ref.side].modified)
				m_panes[ref.side]->document()->setModified(false);
			m_syncing = false;
		}
		else
		{
			m_panes[ref.side]->redo();
			if (!ref.flagsAfter.isEmpty())
				applyGhostFlags(ref.side, ref.flagsAfter);
			redidReal = true;
		}
		refreshSideMaps(ref.side);
		m_undoOrder.append(ref);
		redidAny = true;
	}
	if (redidAny)
	{
		refreshAfterUndoRedo(counts);
		m_syncing = true;
		for (int i = 0; i < m_paneCount; ++i)
			m_panes[i]->verticalScrollBar()->setValue(topLine);
		m_syncing = false;
		return;
	}
	m_panes[m_activePane]->redo();
}

void FileCompareView::selectDiffAtCursor()
{
	if (m_diffStale)
		recompare();
	selectDiffAtViewLine(m_panes[m_activePane]->textCursor().blockNumber());
}

void FileCompareView::focusNextPane()
{
	m_activePane = (m_activePane + 1) % m_paneCount;
	m_panes[m_activePane]->setFocus();
	updateHeaderStyles();
}

/** Select (without scrolling) the difference covering the given view
    line, mirroring upstream's OnCurdiff after a merge undo. */
void FileCompareView::selectDiffAtViewLine(int viewLine)
{
	for (int b = 0; b < static_cast<int>(m_blocks.size()); ++b)
	{
		const Block &block = m_blocks[b];
		if (block.trivial || block.resolved)
			continue;
		if (viewLine >= block.viewBegin && viewLine <= block.viewEnd)
		{
			gotoDiff(b, false);
			return;
		}
	}
}

void FileCompareView::showFind()
{
	m_search[m_activePane]->editFind();
}

void FileCompareView::showReplace()
{
	m_search[m_activePane]->editReplace();
}

void FileCompareView::findRepeat(bool control, bool shift)
{
	m_search[m_activePane]->editRepeat(control, shift);
}

bool FileCompareView::activePaneEditable() const
{
	return !m_readOnly[m_activePane];
}

void FileCompareView::setReadOnlySides(const QList<bool> &readOnly)
{
	for (int i = 0; i < 3 && i < readOnly.size(); ++i)
		m_readOnly[i] = readOnly.at(i);
}

bool FileCompareView::isModified() const
{
	for (int side = 0; side < m_paneCount; ++side)
		if (m_sides[side].modified)
			return true;
	return false;
}

void FileCompareView::setSideModified(int side, bool modified)
{
	const bool was = isModified();
	m_sides[side].modified = modified;
	if (m_actSave != nullptr)
		m_actSave->setEnabled(isModified());
	updateHeader(side);
	updateStatus();
	if (was != isModified())
		emit modifiedChanged(isModified());
}

bool FileCompareView::saveModified(QString *error)
{
	bool savedAny = false;
	for (int side = 0; side < m_paneCount; ++side)
	{
		if (!m_sides[side].modified)
			continue;
		const SaveResult result = saveSide(side, error);
		if (result == SaveResult::Failed)
			return false;
		savedAny = savedAny || result == SaveResult::Saved;
	}
	if (savedAny)
	{
		// a folder comparison that opened this tab updates its row from
		// this, like WinMerge's UpdateChangedItem; refresh the count
		// first so it reflects what was written
		if (m_diffStale)
			recompare();
		emit fileSaved(paths(), m_diffCount);
	}
	return true;
}

QList<int> FileCompareView::modifiedSideIndexes() const
{
	QList<int> sides;
	for (int side = 0; side < m_paneCount; ++side)
		if (m_sides[side].modified)
			sides.append(side);
	return sides;
}

QString FileCompareView::sideLabel(int side) const
{
	const Side &s = m_sides[side];
	if (!s.path.isEmpty())
		return s.path;
	return s.caption.isEmpty() ? tr("Untitled") : s.caption;
}

bool FileCompareView::saveSideAt(int side, QString *error)
{
	if (side < 0 || side >= m_paneCount)
		return false;
	const SaveResult result = saveSide(side, error);
	if (result == SaveResult::Failed)
		return false;
	// declining to overwrite is no failure: the caller goes on, as after
	// WinMerge's DoSave
	if (result == SaveResult::Declined)
		return true;
	if (m_diffStale)
		recompare();
	emit fileSaved(paths(), m_diffCount);
	return true;
}

FileCompareView::SaveResult FileCompareView::saveSide(int side, QString *error)
{
	Side &s = m_sides[side];

	// WinMerge's DoSave: writing over a file another application changed
	// since it was loaded here asks first; No leaves the file and the
	// pane's unsaved changes as they are
	if (lm::fileChangedOnDisk(s.path, s.stamp) == lm::FileChange::Changed
		&& !lm::askOverwriteChangedFile(this, s.path))
		return SaveResult::Declined;

	// untitled panes (File > New) ask for a name on first save
	if (s.path.isEmpty())
	{
		const QString chosen = QFileDialog::getSaveFileName(this,
			tr("Save As"));
		if (chosen.isEmpty())
		{
			if (error != nullptr)
				*error = tr("save canceled");
			return SaveResult::Failed;
		}
		s.path = chosen;
		s.caption.clear();
		m_highlighters[side] = std::make_unique<SyntaxHighlighter>(
			m_panes[side]->document(), chosen);
		updateHeader(side);
		emit pathsChanged();
	}

	const QDateTime originalTime = lm::timeToPreserve(s.path);

	// like WinMerge (OPT_BACKUP_FILECMP, on by default): keep the
	// original as <name>.bak next to it before overwriting
	if (QSettings().value(QStringLiteral("Backup/FileCompare"), true).toBool()
		&& QFile::exists(s.path))
	{
		const QString backupPath = s.path + QStringLiteral(".bak");
		QFile::remove(backupPath);
		if (!QFile::copy(s.path, backupPath))
		{
			if (error != nullptr)
				*error = tr("could not create the backup file %1")
					.arg(backupPath);
			return SaveResult::Failed;
		}
	}

	UniStdioFile file;
	if (!file.OpenCreate(s.path.toStdString()))
	{
		if (error != nullptr)
			*error = tr("cannot write %1").arg(s.path);
		return SaveResult::Failed;
	}
	file.SetUnicoding(static_cast<ucr::UNICODESET>(s.unicoding));
	file.SetCodepage(s.codepage);
	file.SetBom(s.bom);
	file.WriteBom();

	// ghost alignment lines are visual only: never write them
	const QStringList lines = collectRealLines(side);
	const std::string eol = s.eol.toStdString();
	for (int i = 0; i < lines.size(); ++i)
	{
		file.WriteString(lines.at(i).toStdString());
		if (i + 1 < lines.size() || s.hadFinalEol)
			file.WriteString(eol);
	}
	file.Close();
	lm::restoreFileTime(s.path, originalTime);
	// what is on disk now is this pane's content: no change to report
	s.stamp = lm::fileStamp(s.path);
	m_syncing = true;
	m_panes[side]->document()->setModified(false);
	m_syncing = false;
	setSideModified(side, false);
	return SaveResult::Saved;
}

// --- WinMerge's display filter of the file window ---

/** CMergeDoc::HideLines, its display filter part (the diff context, the
    other thing upstream hides lines by, is not here): a line the filter's
    expression does not hold for is hidden in every pane. Nothing is
    hidden without a filter, or by one that does not parse. */
void FileCompareView::hideLines()
{
	QList<bool> hidden;
	if (lm::lineFilterHides(m_displayFilter.get()))
	{
		QList<lm::ComparedFile> files;
		for (int side = 0; side < m_paneCount; ++side)
			files.append({ m_sides[side].path, m_sides[side].unicoding, m_sides[side].codepage,
				m_sides[side].bom });
		hidden = lm::linesHiddenByFilter(m_displayFilter.get(), LineProvider(this), files,
			m_diffCount, ignoredDiffCount());
	}
	m_hiddenLines = hidden;
	applyHiddenLines();
}

/** Hand the hidden lines to the panes, again after something wrote lines
    anew (a merge): a new line shows until told otherwise. */
void FileCompareView::applyHiddenLines()
{
	const bool any = m_hiddenLines.contains(true);
	m_shownBefore.clear();
	if (any)
	{
		m_shownBefore.resize(m_hiddenLines.size() + 1);
		int shown = 0;
		for (int line = 0; line < m_hiddenLines.size(); ++line)
		{
			m_shownBefore[line] = shown;
			shown += m_hiddenLines.at(line) ? 0 : 1;
		}
		m_shownBefore[m_hiddenLines.size()] = shown;
	}
	m_syncing = true;
	for (int side = 0; side < m_paneCount; ++side)
	{
		// only while the pane has the lines the filter looked at
		if (any && m_panes[side]->document()->blockCount() != m_hiddenLines.size())
			continue;
		m_panes[side]->setHiddenLines(any ? m_hiddenLines : QList<bool>());
	}
	// the panes scroll by the lines that show: level them again
	const int active = qBound(0, m_activePane, m_paneCount - 1);
	const int value = m_panes[active]->verticalScrollBar()->value();
	for (int side = 0; side < m_paneCount; ++side)
		if (side != active)
			m_panes[side]->verticalScrollBar()->setValue(value);
	m_syncing = false;
}

/** CMergeEditView::IsDiffFiltered: every line of the difference is
    hidden. */
bool FileCompareView::blockFiltered(int blockIndex) const
{
	if (m_shownBefore.empty())
		return false;
	const Block &block = m_blocks[blockIndex];
	const int lines = static_cast<int>(m_shownBefore.size()) - 1;
	const int first = qBound(0, block.viewBegin, lines);
	const int last = qBound(0, block.viewEnd + 1, lines);
	return m_shownBefore[last] == m_shownBefore[first];
}

/** CMergeDoc::HasInvisibleLines over every difference there is to copy:
    one of them has a hidden line. */
bool FileCompareView::hasInvisibleLines() const
{
	if (m_shownBefore.empty())
		return false;
	const int lines = static_cast<int>(m_shownBefore.size()) - 1;
	for (const Block &block : m_blocks)
	{
		if (block.trivial || block.resolved)
			continue;
		const int first = qBound(0, block.viewBegin, lines);
		const int last = qBound(0, block.viewEnd + 1, lines);
		if (m_shownBefore[last] - m_shownBefore[first] < last - first)
			return true;
	}
	return false;
}

QString FileCompareView::displayFilter() const
{
	return QString::fromStdString(m_displayFilter->GetStringOrExpression());
}

void FileCompareView::ensureFilterBar()
{
	if (m_filterBar != nullptr)
		return;
	m_filterBar = new DisplayFilterBar(DisplayFilterBar::Lines, this);
	// under the toolbar, above the panes
	m_layout->insertWidget(1, m_filterBar);
	connect(m_filterBar, &DisplayFilterBar::applyRequested, this,
		&FileCompareView::applyDisplayFilter);
	connect(m_filterBar, &DisplayFilterBar::closeRequested, this,
		&FileCompareView::closeDisplayFilterBar);
}

/** CMergeEditFrame::HideFilterBar: the bar goes away; the filter it
    applied stays in use. */
void FileCompareView::hideFilterBar()
{
	if (m_filterBar == nullptr)
		return;
	m_filterBar->hide();
	m_filterBar->deleteLater(); // it may be the one asking
	m_filterBar = nullptr;
}

/** CMergeEditFrame::OnDisplayFilterBarClose. */
void FileCompareView::closeDisplayFilterBar()
{
	hideFilterBar();
	m_panes[qBound(0, m_activePane, m_paneCount - 1)]->setFocus();
}

void FileCompareView::showDisplayFilterBar()
{
	ensureFilterBar();
	if (!displayFilter().isEmpty())
		m_filterBar->setFilterText(displayFilter());
	m_filterBar->focusField();
}

void FileCompareView::toggleDisplayFilterBar()
{
	if (m_filterBar == nullptr)
		ensureFilterBar();
	else
		hideFilterBar();
}

void FileCompareView::applyDisplayFilter()
{
	if (m_filterBar == nullptr)
		return;
	QGuiApplication::setOverrideCursor(Qt::WaitCursor);
	// the text as it is typed: saving it reads the list again, which puts
	// its latest entry in the field
	const QString text = m_filterBar->filterText();
	m_filterBar->saveFilterText();
	m_displayFilter->SetStringOrExpression(text.toStdString());
	if (!text.isEmpty()
		&& m_displayFilter->GetFilterExpression().errorCode == FILTER_ERROR_NO_ERROR)
		m_filterBar->setFilterApplied(true);
	// FlushAndRescan(true): the comparison runs again, and hides the lines
	recompare();
	m_panes[qBound(0, m_activePane, m_paneCount - 1)]->setFocus();
	QGuiApplication::restoreOverrideCursor();
}

void FileCompareView::addToDisplayFilter(const QString &text)
{
	m_displayFilter->AddToExpression(
		_T("Line contains ") + LineFilterHelper::Quote(text.toStdString()), _T("AND"));
	ensureFilterBar();
	m_filterBar->setFilterText(displayFilter());
	applyDisplayFilter();
}

void FileCompareView::addSelectionToDisplayFilter(int side)
{
	if (side < 0 || side >= m_paneCount)
		return;
	const QTextCursor cursor = m_panes[side]->textCursor();
	const QTextDocument *doc = m_panes[side]->document();
	QString text;
	if (cursor.hasSelection()
		&& doc->findBlock(cursor.selectionStart()) == doc->findBlock(cursor.selectionEnd()))
		text = cursor.selectedText();
	if (text.isEmpty())
	{
		QTextCursor word = cursor;
		word.setPosition(cursor.position());
		word.select(QTextCursor::WordUnderCursor);
		text = word.selectedText();
	}
	if (!text.isEmpty())
		addToDisplayFilter(text);
}

/** WinMerge's pane menu has "Add to Filters" above Undo, with the line
    and the substitution filters in it too; those two are not here. */
void FileCompareView::buildPaneMenu(int side, QMenu *menu)
{
	QAction *first = menu->actions().value(0);
	auto *filters = new QMenu(tr("Add to &Filters"), menu);
	QAction *display = filters->addAction(tr("Add to &Display Filter"));
	display->setObjectName(QStringLiteral("addToDisplayFilter"));
	connect(display, &QAction::triggered, this,
		[this, side]() { addSelectionToDisplayFilter(side); });
	menu->insertMenu(first, filters);
	menu->insertSeparator(first);
}

bool FileCompareView::lineHiddenForTest(int viewLine) const
{
	return m_panes[0]->isLineHidden(viewLine);
}

QStringList FileCompareView::shownLinesForTest(int side) const
{
	QStringList lines;
	for (QTextBlock block = m_panes[side]->document()->begin(); block.isValid();
		block = block.next())
		if (block.isVisible() && !isGhostBlock(block))
			lines.append(block.text());
	return lines;
}

/** The lines of the location pane are the lines that show: a view line
    from one, and one from a view line. */
int FileCompareView::viewLineOfShown(int shownLine) const
{
	if (m_shownBefore.empty())
		return shownLine;
	const auto it = std::upper_bound(m_shownBefore.begin(), m_shownBefore.end(), shownLine);
	return qMax(0, static_cast<int>(it - m_shownBefore.begin()) - 1);
}

int FileCompareView::shownLineOfView(int viewLine) const
{
	if (m_shownBefore.empty())
		return viewLine;
	return m_shownBefore[qBound(0, viewLine, static_cast<int>(m_shownBefore.size()) - 1)];
}

/** The file's line at a view line (ComputeRealLine): a filler line stands
    for the file's next line. Upstream gives the filler after a file's
    end the line past it, a line the file does not have ("Go to Line 101"
    over a file of 100); here it is the file's last line. */
int FileCompareView::realLineOfView(int side, int viewLine) const
{
	const QList<int> &numbers = m_lineNumbers[side];
	for (int v = qMax(0, viewLine); v < numbers.size(); ++v)
		if (numbers[v] > 0)
			return numbers[v] - 1;
	return qMax(0, static_cast<int>(m_realToView[side].size()) - 1);
}

/** WinMerge's GotoLine: a line of a file, a real one or a line of the
    views, in the middle of the panes, and the cursor on it in every pane,
    with no selection but in the file's own pane when its anchor is to
    stay (Shift held), which becomes the active pane. */
void FileCompareView::gotoLine(int line, bool realLine, int pane, bool moveAnchor)
{
	if (pane < 0 || pane >= m_paneCount)
		return;
	int viewLine = line;
	if (realLine)
	{
		const int count = static_cast<int>(m_realToView[pane].size());
		viewLine = count > 0 ? m_realToView[pane][qBound(0, line, count - 1)] : 0;
	}
	const int top = qMax(0, shownLineOfView(viewLine) - m_panes[pane]->visibleLineCount() / 2);
	m_syncing = true;
	for (int p = 0; p < m_paneCount; ++p)
	{
		DiffTextEdit *edit = m_panes[p];
		edit->verticalScrollBar()->setValue(top);
		const QTextBlock block = edit->document()->findBlockByNumber(
			qBound(0, viewLine, edit->document()->blockCount() - 1));
		QTextCursor cursor = edit->textCursor();
		cursor.setPosition(block.position(), moveAnchor || p != pane
			? QTextCursor::MoveAnchor : QTextCursor::KeepAnchor);
		edit->setTextCursor(cursor);
	}
	m_syncing = false;
	m_activePane = pane;
	m_panes[pane]->setFocus();
	updateHeaderStyles();
}

/** The location pane's press and drag (CLocationView::OnMouseMove): a line
    of it in the middle of the panes, their scroll range keeping it in. */
void FileCompareView::centerShownLine(int shownLine)
{
	m_panes[0]->verticalScrollBar()->setValue(
		qMax(0, shownLine - m_panes[0]->visibleLineCount() / 2));
}

QKeySequence FileCompareView::goToShortcut()
{
#ifdef Q_OS_MACOS
	return QKeySequence(Qt::META | Qt::Key_G);
#else
	return QKeySequence(Qt::CTRL | Qt::Key_G);
#endif
}

/** Upstream's location pane menu (CLocationView::OnContextMenu over its
    IDR_POPUP_LOCATIONBAR), and what its items do. */
void FileCompareView::showLocationMenu(const QPoint &globalPos, int side, int line)
{
	QMenu menu(this);
	// "Go to Diff" becomes "Go to Line" and the line under the pointer, of
	// the file whose bar it is over (the first one between the bars); off
	// the bars' height it has no number and is grey
	const int realLine = side >= 0 && side < m_paneCount
		? realLineOfView(side, viewLineOfShown(line)) : -1;
	QAction *gotoLineAction = menu.addAction(tr("G&o to Line %1")
		.arg(realLine >= 0 ? QString::number(realLine + 1) : QString()));
	gotoLineAction->setObjectName(QStringLiteral("locationGotoLine"));
	gotoLineAction->setEnabled(realLine >= 0);
	QAction *gotoAction = menu.addAction(tr("&Go to..."));
	gotoAction->setObjectName(QStringLiteral("locationGoto"));
	gotoAction->setShortcut(goToShortcut());
	// upstream finds a definition with its Tree-sitter parsers, which are
	// not here (and its own item in this menu has no handler)
	QAction *definitionAction = menu.addAction(tr("Go to &Definition"));
	definitionAction->setObjectName(QStringLiteral("locationGotoDefinition"));
	definitionAction->setShortcut(QKeySequence(Qt::Key_F12));
	definitionAction->setEnabled(false);
	menu.addSeparator();
	QAction *moveCursorAction = menu.addAction(tr("Move Cursor on &Click"));
	moveCursorAction->setObjectName(QStringLiteral("locationMoveCursor"));
	moveCursorAction->setCheckable(true);
	moveCursorAction->setChecked(LocationPane::moveCursorOnClick());
	menu.addSeparator();
	COptionsMgr *mgr = GetOptionsMgr();
	const bool moved = mgr != nullptr && mgr->GetBool(OPT_CMP_MOVED_BLOCKS);
	auto *movedGroup = new QActionGroup(&menu);
	QAction *noMovedAction = menu.addAction(tr("&No Moved Blocks"));
	noMovedAction->setObjectName(QStringLiteral("locationNoMovedBlocks"));
	QAction *allMovedAction = menu.addAction(tr("&All Moved Blocks"));
	allMovedAction->setObjectName(QStringLiteral("locationAllMovedBlocks"));
	for (QAction *action : { noMovedAction, allMovedAction })
	{
		action->setCheckable(true);
		movedGroup->addAction(action);
	}
	noMovedAction->setChecked(!moved);
	allMovedAction->setChecked(moved);

	QAction *chosen = LocationPane::execMenu(&menu, globalPos);
	if (chosen == nullptr)
		return;
	if (chosen == gotoLineAction)
		gotoLine(realLine, true, side, true);
	else if (chosen == gotoAction)
		showGoTo(0); // upstream opens it from the first file's view
	else if (chosen == moveCursorAction)
		LocationPane::setMoveCursorOnClick(!LocationPane::moveCursorOnClick());
	else if (chosen == noMovedAction)
		setMovedBlocks(false);
	else if (chosen == allMovedAction)
		setMovedBlocks(true);
}

/** "No Moved Blocks" and "All Moved Blocks" (CMergeDoc::SetDetectMovedBlocks):
    moved blocks are looked for, or not, from now on, and this comparison
    is made again. */
void FileCompareView::setMovedBlocks(bool detect)
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr || mgr->GetBool(OPT_CMP_MOVED_BLOCKS) == detect)
		return;
	mgr->SaveOption(OPT_CMP_MOVED_BLOCKS, detect);
	recompare();
}

/** WinMerge's Go To (CMergeEditView::OnWMGoto): the dialog opens on the
    pane's line and file, and goes to a line of the file chosen or to a
    difference by its place in the list, every difference counted. */
void FileCompareView::showGoTo(int fromPane)
{
	if (fromPane < 0 || fromPane >= m_paneCount)
		fromPane = m_activePane;
	int lastLine[3] = { -1, -1, -1 }; // the last real line of each file
	for (int p = 0; p < m_paneCount; ++p)
		lastLine[p] = static_cast<int>(m_realToView[p].size()) - 1;
	const bool twoWay = m_paneCount < 3;
	GoToDialog dialog(this);
	dialog.init(QString::number(realLineOfView(fromPane,
			m_panes[fromPane]->textCursor().blockNumber()) + 1),
		twoWay ? (fromPane == 1 ? 2 : 0) : fromPane, m_paneCount,
		{ lastLine[0] + 1, twoWay ? -1 : lastLine[1] + 1,
			twoWay ? lastLine[1] + 1 : lastLine[2] + 1 },
		static_cast<int>(m_blocks.size()));
	if (!GoToDialog::run(&dialog))
		return;
	const int number = dialog.number() - 1;
	if (dialog.goesToLine())
	{
		const int pane = twoWay ? (dialog.file() == 2 ? 1 : 0) : dialog.file();
		const bool shift = QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier;
		gotoLine(qBound(0, number, qMax(0, lastLine[pane])), true, pane, !shift);
	}
	else
	{
		const int diff = qBound(0, number, static_cast<int>(m_blocks.size()) - 1);
		if (diff >= 0)
			gotoDiff(diff, true);
	}
}

/** A pane scrolled vertically, whatever moved it: the other panes follow
    it, as WinMerge's UpdateSiblingScrollPos has them, and the location
    pane's marker shows where the first pane now is, as the view itself
    tells WinMerge's location pane (UpdateLocationViewPosition). */
void FileCompareView::paneScrolled(int pane)
{
	syncScroll(pane, m_panes[pane]->verticalScrollBar()->value());
	if (pane == 0)
		updateLocationViewport();
}

void FileCompareView::syncScroll(int pane, int value)
{
	if (m_syncing)
		return;
	m_syncing = true;
	for (int i = 0; i < m_paneCount; ++i)
	{
		if (i != pane)
			m_panes[i]->verticalScrollBar()->setValue(value);
	}
	m_syncing = false;
}

void FileCompareView::syncHScroll(int pane, int value)
{
	if (m_syncing)
		return;
	m_syncing = true;
	for (int i = 0; i < m_paneCount; ++i)
	{
		if (i != pane)
			m_panes[i]->horizontalScrollBar()->setValue(value);
	}
	m_syncing = false;
}
