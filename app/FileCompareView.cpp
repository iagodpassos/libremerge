// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "FileCompareView.h"

#include <QAction>
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
#include "EngineOptions.h"
#include "Icons.h"
#include "LocationPane.h"
#include "SyntaxHighlighter.h"
#include "OptionsDialog.h"
#include "Theme.h"

// engine
#include "DiffWrapper.h"
#include "DiffList.h"
#include "FilterList.h"
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

FileCompareView::~FileCompareView() = default;

FileCompareView::FileCompareView(QWidget *parent)
	: QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
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
		[this]() { recompare(); });
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
		[this]() { showFindBar(); });
	addToolAction(lm::Icon::Options, tr("Comparison Options"), QString(),
		[this]() { emit optionsRequested(); });
	layout->addWidget(toolbar);

	// find/replace bar, hidden until requested
	m_findBar = new QWidget(this);
	auto *findLayout = new QHBoxLayout(m_findBar);
	findLayout->setContentsMargins(6, 3, 6, 3);
	findLayout->setSpacing(6);
	m_findEdit = new QLineEdit(m_findBar);
	m_findEdit->setPlaceholderText(tr("Find"));
	m_findEdit->setMaximumWidth(260);
	connect(m_findEdit, &QLineEdit::returnPressed,
		this, [this]() { findNext(false); });
	findLayout->addWidget(m_findEdit);
	auto *prevButton = new QPushButton(tr("Previous"), m_findBar);
	connect(prevButton, &QPushButton::clicked, this, [this]() { findNext(true); });
	findLayout->addWidget(prevButton);
	auto *nextButton = new QPushButton(tr("Next"), m_findBar);
	nextButton->setDefault(false);
	connect(nextButton, &QPushButton::clicked, this, [this]() { findNext(false); });
	findLayout->addWidget(nextButton);
	m_findCase = new QCheckBox(tr("Match case"), m_findBar);
	findLayout->addWidget(m_findCase);
	findLayout->addSpacing(12);
	m_replaceEdit = new QLineEdit(m_findBar);
	m_replaceEdit->setPlaceholderText(tr("Replace with"));
	m_replaceEdit->setMaximumWidth(220);
	findLayout->addWidget(m_replaceEdit);
	auto *replaceButton = new QPushButton(tr("Replace"), m_findBar);
	connect(replaceButton, &QPushButton::clicked, this, [this]() { replaceOne(); });
	findLayout->addWidget(replaceButton);
	auto *replaceAllButton = new QPushButton(tr("Replace All"), m_findBar);
	connect(replaceAllButton, &QPushButton::clicked, this, [this]() { replaceAll(); });
	findLayout->addWidget(replaceAllButton);
	m_findStatus = new QLabel(m_findBar);
	findLayout->addWidget(m_findStatus, 1);
	auto *closeButton = new QToolButton(m_findBar);
	closeButton->setText(QString::fromUtf8("\xE2\x9C\x95"));
	closeButton->setAutoRaise(true);
	connect(closeButton, &QToolButton::clicked, this, [this]() {
		m_findBar->hide();
		m_panes[m_activePane]->setFocus();
	});
	findLayout->addWidget(closeButton);
	auto *escapeAction = new QAction(m_findBar);
	escapeAction->setShortcut(QKeySequence(Qt::Key_Escape));
	escapeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	m_findBar->addAction(escapeAction);
	connect(escapeAction, &QAction::triggered, this, [this]() {
		m_findBar->hide();
		m_panes[m_activePane]->setFocus();
	});
	m_findBar->hide();
	layout->addWidget(m_findBar);

	auto *panes = new QHBoxLayout;
	panes->setContentsMargins(0, 0, 0, 0);
	panes->setSpacing(1);

	m_locationPane = new LocationPane(this);
	panes->addWidget(m_locationPane);
	connect(m_locationPane, &LocationPane::jumpRequested, this, [this](int line) {
		for (int side = 0; side < m_paneCount; ++side)
		{
			QTextCursor cursor(m_panes[side]->document()->findBlockByNumber(
				qMin(line, m_panes[side]->document()->blockCount() - 1)));
			m_syncing = true;
			m_panes[side]->setTextCursor(cursor);
			m_panes[side]->centerCursor();
			m_syncing = false;
		}
	});

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

		connect(m_panes[i]->verticalScrollBar(), &QScrollBar::valueChanged,
			this, [this, i](int value) { syncScroll(i, value); });
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
				m_undoOrder.append(
					{ i, m_panes[i]->textCursor().blockNumber() });
				m_redoOrder.clear();
			});
	}
	connect(m_panes[0]->verticalScrollBar(), &QScrollBar::valueChanged,
		this, [this]() {
			m_locationPane->setViewport(m_panes[0]->firstVisibleLine(),
				m_panes[0]->visibleLineCount());
		});

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
}

QString FileCompareView::tabTitle() const
{
	QStringList names;
	for (int side = 0; side < m_paneCount; ++side)
	{
		const Side &s = m_sides[side];
		names.append(s.path.isEmpty()
			? (s.caption.isEmpty() ? tr("Untitled") : s.caption)
			: QFileInfo(s.path).fileName());
	}
	return names.join(QString::fromUtf8(" \xE2\x86\x94 "));
}

bool FileCompareView::loadSide(int side, const QString &path, QString *error)
{
	// refuse binary files up front: silently comparing them as text ends
	// with a misleading "files are identical" (NUL bytes without a
	// UTF-16/UTF-32 BOM mean binary, the same heuristic diff uses)
	{
		QFile probe(path);
		if (probe.open(QIODevice::ReadOnly))
		{
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
		}
	}

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
		const QByteArray bytes = m_realLines[i].join(QChar('\n')).toUtf8();
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
	auto filterList = std::make_shared<FilterList>();
	const QStringList filterEntries = QSettings()
		.value(QStringLiteral("LineFilters/List")).toStringList();
	for (const QString &entry : filterEntries)
	{
		if (entry.startsWith(QStringLiteral("1\t")))
		{
			try
			{
				filterList->AddRegExp(entry.mid(2).toStdString());
			}
			catch (...)
			{
				// invalid expression: skip it
			}
		}
	}
	if (filterList->HasRegExps())
		wrapper.SetFilterList(filterList);

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
	// to moved blocks, per side
	for (int side = 0; side < 3; ++side)
		m_movedLines[side].clear();
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
				if (moved->LineInBlock(line, other) != -1)
					m_movedLines[side].insert(line);
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
		if (!block.trivial)
			++m_diffCount;
		m_blocks.push_back(block);
	}
	m_diffStale = false;
	rebuildAlignment();
	computeWordSpans();
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
		for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
			currentViewLines.append(b.text());

		const bool changed = currentViewLines != newLines[side]
			|| oldFlags != newFlags[side];
		if (changed)
		{
			const bool wasModified = doc->isModified();
			const int vScroll = m_panes[side]->verticalScrollBar()->value();
			const int hScroll = m_panes[side]->horizontalScrollBar()->value();
			m_syncing = true;
			m_panes[side]->setPlainText(newLines[side].join(QChar('\n')));
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

		const std::vector<strdiff::wdiff> wdiffs = strdiff::ComputeWordDiffs(
			m_paneCount, joined,
			!options.bIgnoreCase, strdiff::EOL_STRICT,
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
					continue;
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

		// word-level spans on top of the line backgrounds
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
		m_panes[side]->setExtraSelections(selections);
		m_panes[side]->setGutterLineColors(gutterColors);
	}

	// location pane: real content and ghost filler get separate bands
	std::vector<LocationPane::Band> bands;
	for (size_t b = 0; b < m_blocks.size(); ++b)
	{
		const Block &block = m_blocks[b];
		const bool current = (static_cast<int>(b) == m_current);
		for (int side = 0; side < m_paneCount; ++side)
		{
			const int len = qMax(0, block.end[side] - block.begin[side] + 1);
			if (len > 0 && !block.resolved)
			{
				LocationPane::Band band;
				band.side = side;
				band.firstLine = block.viewBegin;
				band.lastLine = block.viewBegin + len - 1;
				band.color = block.trivial ? C.trivial
					: (current ? C.selDiff : C.diff);
				bands.push_back(band);
			}
			if (len <= block.viewEnd - block.viewBegin)
			{
				LocationPane::Band band;
				band.side = side;
				band.firstLine = block.viewBegin + len;
				band.lastLine = block.viewEnd;
				band.color = block.trivial || block.resolved
					? C.trivialDeleted
					: (current ? C.selDiffDeleted : C.diffDeleted);
				bands.push_back(band);
			}
		}
	}
	m_locationPane->setBands(std::move(bands),
		qMax(1, m_panes[0]->document()->blockCount()));
	m_locationPane->setViewport(m_panes[0]->firstVisibleLine(),
		m_panes[0]->visibleLineCount());

	updateDiffPane();
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
	m_posLabels[side]->setText(tr("Lin: %1  Col: %2/%3  Car: %2/%3")
		.arg(realLine).arg(col).arg(maxCol));
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

void FileCompareView::selectAllAndCopyForTest(int side)
{
	m_panes[side]->selectAll();
	m_panes[side]->copy();
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

int FileCompareView::nextActive(int from, int direction) const
{
	for (int b = from + direction; b >= 0 && b < static_cast<int>(m_blocks.size()); b += direction)
		if (!m_blocks[b].trivial && !m_blocks[b].resolved)
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
				&& m_blocks[b].viewBegin >= line)
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
				&& m_blocks[b].viewEnd <= line)
			{
				prev = b;
				break;
			}
		}
	}
	if (prev >= 0)
		gotoDiff(prev);
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
	// scroll horizontally to where the change actually is
	if (m_current < 0)
		return;
	for (const WordSpan &span : m_wordSpans)
	{
		if (span.blockIndex != m_current)
			continue;
		if (span.line < 0
			|| span.line >= static_cast<int>(m_realToView[span.side].size()))
			continue;
		const int viewLine = m_realToView[span.side][span.line];
		const QTextBlock block =
			m_panes[span.side]->document()->findBlockByNumber(viewLine);
		if (!block.isValid())
			continue;
		QTextCursor cursor(block);
		cursor.setPosition(block.position()
			+ qMin(span.start, static_cast<int>(block.length()) - 1));
		m_panes[span.side]->setTextCursor(cursor);
		// explicit horizontal scroll: WinMerge's EnsureVisible leaves the
		// inline difference ~5 characters from the left edge. The target
		// x comes from font metrics (needs no layout), but QPlainTextEdit
		// lays out lazily and its horizontal range only exists after the
		// first paint - so when the range is not there yet, the value is
		// applied on the scrollbar's own rangeChanged. No m_syncing
		// guard: the sibling panes follow, like WinMerge's
		// UpdateSiblingScrollPos on the horizontal axis.
		{
			const QFontMetricsF metrics(m_panes[span.side]->font());
			const int x = qRound(metrics.horizontalAdvance(
				block.text().left(span.start)));
			const int margin =
				qRound(5 * metrics.horizontalAdvance(QLatin1Char(' ')));
			DiffTextEdit *paneEdit = m_panes[span.side];
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
			// starting the app straight into a comparison relayouts the
			// panes when the window first shows, resetting the scroll:
			// re-assert once after the startup settles (idempotent)
			QTimer::singleShot(300, paneEdit, apply);
		}
		break;
	}
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
	while (!m_undoOrder.isEmpty())
	{
		const UndoRef ref = m_undoOrder.takeLast();
		if (ref.side >= m_paneCount
			|| !m_panes[ref.side]->document()->isUndoAvailable())
			continue; // stale entry (e.g. a rebuild cleared that stack)
		m_panes[ref.side]->undo();
		m_redoOrder.append(ref);
		refreshAfterUndoRedo(counts);
		// like WinMerge's OnEditUndo: the viewport stays where it was,
		// and the difference at the undone spot is selected again so it
		// can simply be merged once more
		m_syncing = true;
		for (int i = 0; i < m_paneCount; ++i)
			m_panes[i]->verticalScrollBar()->setValue(topLine);
		m_syncing = false;
		selectDiffAtViewLine(ref.viewLine);
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
	while (!m_redoOrder.isEmpty())
	{
		const UndoRef ref = m_redoOrder.takeLast();
		if (ref.side >= m_paneCount
			|| !m_panes[ref.side]->document()->isRedoAvailable())
			continue;
		m_panes[ref.side]->redo();
		m_undoOrder.append(ref);
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

void FileCompareView::showFindBar()
{
	m_findBar->show();
	const QString selected = m_panes[m_activePane]->textCursor().selectedText();
	if (!selected.isEmpty() && !selected.contains(QChar(0x2029)))
		m_findEdit->setText(selected);
	m_findStatus->clear();
	m_findEdit->selectAll();
	m_findEdit->setFocus();
}

void FileCompareView::findNext(bool backward)
{
	if (m_findBar->isHidden())
	{
		showFindBar();
		return;
	}
	const QString needle = m_findEdit->text();
	if (needle.isEmpty())
		return;
	QTextDocument::FindFlags flags;
	if (backward)
		flags |= QTextDocument::FindBackward;
	if (m_findCase->isChecked())
		flags |= QTextDocument::FindCaseSensitively;

	DiffTextEdit *pane = m_panes[m_activePane];
	if (pane->find(needle, flags))
	{
		m_findStatus->clear();
		return;
	}
	// wrap around
	QTextCursor cursor = pane->textCursor();
	cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
	pane->setTextCursor(cursor);
	m_findStatus->setText(pane->find(needle, flags)
		? tr("Search wrapped") : tr("Not found"));
}

void FileCompareView::replaceOne()
{
	if (m_readOnly[m_activePane])
	{
		m_findStatus->setText(tr("This pane is read-only."));
		return;
	}
	DiffTextEdit *pane = m_panes[m_activePane];
	const QString needle = m_findEdit->text();
	if (needle.isEmpty())
		return;
	QTextCursor cursor = pane->textCursor();
	const Qt::CaseSensitivity cs = m_findCase->isChecked()
		? Qt::CaseSensitive : Qt::CaseInsensitive;
	if (cursor.hasSelection()
		&& QString::compare(cursor.selectedText(), needle, cs) == 0)
		cursor.insertText(m_replaceEdit->text());
	findNext(false);
}

void FileCompareView::replaceAll()
{
	if (m_readOnly[m_activePane])
	{
		m_findStatus->setText(tr("This pane is read-only."));
		return;
	}
	DiffTextEdit *pane = m_panes[m_activePane];
	const QString needle = m_findEdit->text();
	if (needle.isEmpty())
		return;
	QTextDocument::FindFlags flags;
	if (m_findCase->isChecked())
		flags |= QTextDocument::FindCaseSensitively;

	QTextCursor cursor(pane->document());
	cursor.beginEditBlock();
	int count = 0;
	QTextCursor match = pane->document()->find(needle, 0, flags);
	while (!match.isNull())
	{
		match.insertText(m_replaceEdit->text());
		++count;
		// continue after the replacement (safe even when it contains
		// the search text)
		match = pane->document()->find(needle, match.position(), flags);
	}
	cursor.endEditBlock();
	m_findStatus->setText(tr("%n replacement(s)", nullptr, count));
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
	for (int side = 0; side < m_paneCount; ++side)
	{
		if (m_sides[side].modified && !saveSide(side, error))
			return false;
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
	return saveSide(side, error);
}

bool FileCompareView::saveSide(int side, QString *error)
{
	Side &s = m_sides[side];

	// untitled panes (File > New) ask for a name on first save
	if (s.path.isEmpty())
	{
		const QString chosen = QFileDialog::getSaveFileName(this,
			tr("Save As"));
		if (chosen.isEmpty())
		{
			if (error != nullptr)
				*error = tr("save canceled");
			return false;
		}
		s.path = chosen;
		s.caption.clear();
		m_highlighters[side] = std::make_unique<SyntaxHighlighter>(
			m_panes[side]->document(), chosen);
		updateHeader(side);
		emit pathsChanged();
	}

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
			return false;
		}
	}

	UniStdioFile file;
	if (!file.OpenCreate(s.path.toStdString()))
	{
		if (error != nullptr)
			*error = tr("cannot write %1").arg(s.path);
		return false;
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
	m_syncing = true;
	m_panes[side]->document()->setModified(false);
	m_syncing = false;
	setSideModified(side, false);
	return true;
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
