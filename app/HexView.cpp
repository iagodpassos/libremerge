// SPDX-License-Identifier: GPL-3.0-or-later
#include "HexView.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSettings>
#include <QStyleHints>
#include <QTimer>

#include "HexDialogs.h"
#include "MessageBoxes.h"
#include "Theme.h"

namespace
{

// Frhed's layout, in characters: two between the offset and the bytes,
// one between the bytes and the characters; a line one pixel taller above
// and below the text
constexpr int kByteSpace = 2;
constexpr int kCharSpace = 1;
constexpr int kLineSpaceAbove = 1;
constexpr int kLineAdditionalSpace = 2;

/** The hex digits the largest offset takes, one at least. */
int hexDigits(qsizetype value)
{
	int digits = 1;
	while ((static_cast<quint64>(value) >> (4 * digits)) != 0 && digits < 16)
		++digits;
	return digits;
}

QChar hexDigit(int value)
{
	return QLatin1Char("0123456789abcdef"[value & 0xf]);
}

QString fontKey()
{
	return QStringLiteral("FileCompare/FontPointSize");
}

qreal standardPointSize()
{
	return QFontDatabase::systemFont(QFontDatabase::FixedFont).pointSizeF();
}

/** WinMerge's zoom, which its text and binary comparisons share: here the
    text comparison's size over the standard one. */
qreal zoomFactor()
{
	return QSettings().value(fontKey(), standardPointSize()).toReal() / standardPointSize();
}

const QString kSettings = QStringLiteral("BinaryCompare/");

std::function<void(QMenu *)> &contextMenuPresenter()
{
	static std::function<void(QMenu *)> presenter;
	return presenter;
}

} // namespace

// --- HexSettings -------------------------------------------------------------

HexSettings HexSettings::load()
{
	const QSettings store;
	HexSettings settings;
	settings.automaticBytesPerLine = store.value(kSettings + QStringLiteral("AutomaticBytesPerLine"),
		settings.automaticBytesPerLine).toBool();
	settings.bytesPerLine = qMax(1, store.value(kSettings + QStringLiteral("BytesPerLine"),
		settings.bytesPerLine).toInt());
	settings.automaticOffsetLength = store.value(kSettings + QStringLiteral("AutomaticOffsetLength"),
		settings.automaticOffsetLength).toBool();
	settings.offsetLength = qBound(1, store.value(kSettings + QStringLiteral("OffsetLength"),
		settings.offsetLength).toInt(), 16);
	settings.unsignedValues = store.value(kSettings + QStringLiteral("UnsignedValues"),
		settings.unsignedValues).toBool();
	settings.bigEndian = store.value(kSettings + QStringLiteral("BigEndian"),
		settings.bigEndian).toBool();
	settings.oemCharacterSet = store.value(kSettings + QStringLiteral("OemCharacterSet"),
		settings.oemCharacterSet).toBool();
	settings.fontSize = store.value(kSettings + QStringLiteral("FontSize"), settings.fontSize).toReal();
	return settings;
}

void HexSettings::save() const
{
	QSettings store;
	store.setValue(kSettings + QStringLiteral("AutomaticBytesPerLine"), automaticBytesPerLine);
	store.setValue(kSettings + QStringLiteral("BytesPerLine"), bytesPerLine);
	store.setValue(kSettings + QStringLiteral("AutomaticOffsetLength"), automaticOffsetLength);
	store.setValue(kSettings + QStringLiteral("OffsetLength"), offsetLength);
	store.setValue(kSettings + QStringLiteral("UnsignedValues"), unsignedValues);
	store.setValue(kSettings + QStringLiteral("BigEndian"), bigEndian);
	store.setValue(kSettings + QStringLiteral("OemCharacterSet"), oemCharacterSet);
	store.setValue(kSettings + QStringLiteral("FontSize"), fontSize);
	emit HexSettingsWatch::instance()->changed();
}

qreal HexSettings::basePointSize() const
{
	return fontSize > 0 ? fontSize : standardPointSize();
}

HexSettingsWatch *HexSettingsWatch::instance()
{
	static HexSettingsWatch watch;
	return &watch;
}

// --- HexUndoHistory ---------------------------------------------------------

void HexUndoHistory::push(HexView *view, qsizetype offset, const QByteArray &before,
	const QByteArray &after)
{
	m_records.resize(m_next);
	Record record;
	record.view = view;
	record.offset = offset;
	record.before = before;
	record.after = after;
	record.id = ++m_lastId;
	record.previousState = view->m_state;
	view->m_state = record.id;
	m_records.append(record);
	m_next = m_records.size();
	emit changed();
}

void HexUndoHistory::undo()
{
	if (!canUndo())
		return;
	const Record &record = m_records.at(--m_next);
	record.view->replaceBytes(record.offset, record.after.size(), record.before, false);
	record.view->m_state = record.previousState;
	record.view->showUndone(record.offset);
	emit changed();
}

void HexUndoHistory::redo()
{
	if (!canRedo())
		return;
	const Record &record = m_records.at(m_next++);
	record.view->replaceBytes(record.offset, record.before.size(), record.after, false);
	record.view->m_state = record.id;
	record.view->showUndone(record.offset);
	emit changed();
}

void HexUndoHistory::clear(HexView *view)
{
	if (view == nullptr)
	{
		m_records.clear();
		m_next = 0;
	}
	else
	{
		for (qsizetype i = m_records.size() - 1; i >= 0; --i)
		{
			if (m_records.at(i).view != view)
				continue;
			m_records.removeAt(i);
			if (i < m_next)
				--m_next;
		}
	}
	emit changed();
}

// --- HexView ----------------------------------------------------------------

HexView::HexView(QWidget *parent)
	: QAbstractScrollArea(parent)
	, m_dragDelay(new QTimer(this))
	, m_scrollTimer(new QTimer(this))
	, m_blink(new QTimer(this))
{
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_InputMethodEnabled, false);
	viewport()->setCursor(Qt::IBeamCursor);
	viewport()->setMouseTracking(true);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

	m_settings = HexSettings::load();
	QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	font.setPointSizeF(m_settings.basePointSize() * zoomFactor());
	setFont(font);
	connect(HexSettingsWatch::instance(), &HexSettingsWatch::changed, this,
		[this]() { applySettings(HexSettings::load()); });

	// a press held or dragged becomes a selection (Frhed's mouse operation
	// delay: the system's drag delay and distance)
	m_dragDelay->setSingleShot(true);
	m_dragDelay->setInterval(QApplication::startDragTime());
	connect(m_dragDelay, &QTimer::timeout, this, &HexView::beginDragSelection);
	m_scrollTimer->setInterval(50);
	connect(m_scrollTimer, &QTimer::timeout, this, &HexView::autoScroll);
	const int flash = QApplication::cursorFlashTime();
	m_blink->setInterval(flash > 0 ? flash / 2 : 500);
	connect(m_blink, &QTimer::timeout, this, [this]() {
		m_caretShown = !m_caretShown;
		viewport()->update();
	});
	connect(lm::Theme::instance(), &lm::Theme::changed, viewport(),
		qOverload<>(&QWidget::update));
	relayout();
}

HexView::~HexView() = default;

void HexView::setData(const QByteArray &data)
{
	m_data = data;
	m_curByte = 0;
	m_curNibble = 0;
	m_selected = false;
	m_selStart = m_selEnd = 0;
	m_state = m_savedState = 0;
	verticalScrollBar()->setValue(0);
	horizontalScrollBar()->setValue(0);
	relayout();
	changed();
}

void HexView::setSiblings(HexView *first, HexView *second)
{
	m_siblings[0] = first != nullptr ? first : this;
	m_siblings[1] = second != nullptr ? second : this;
	relayout();
}

void HexView::setReadOnly(bool readOnly)
{
	m_readOnly = readOnly;
	emit statusChanged();
}

void HexView::setSavePoint()
{
	m_savedState = m_state;
	emit statusChanged();
}

void HexView::toggleInsertMode()
{
	m_insertMode = !m_insertMode;
	emit statusChanged();
}

qsizetype HexView::siblingLength(int i) const
{
	return m_siblings[i]->m_data.size();
}

bool HexView::isDiff(qsizetype offset) const
{
	if (offset < 0 || offset >= m_data.size())
		return false;
	const char byte = m_data.at(offset);
	for (const HexView *sibling : m_siblings)
	{
		if (offset >= sibling->m_data.size() || sibling->m_data.at(offset) != byte)
			return true;
	}
	return false;
}

// --- layout -----------------------------------------------------------------

int HexView::charsPerLine() const
{
	return m_offsetLength + kByteSpace + m_bytesPerLine * 3 + kCharSpace + m_bytesPerLine;
}

int HexView::lineCount() const
{
	return static_cast<int>((m_data.size() + m_bytesPerLine) / m_bytesPerLine);
}

int HexView::screenLines() const
{
	return qMax(1, viewport()->height() / m_cy);
}

int HexView::screenColumns() const
{
	return qMax(1, viewport()->width() / m_cx);
}

int HexView::topLine() const
{
	return verticalScrollBar()->value();
}

/** Upstream's resize_window: the character cell, the offset's digits and
    the bytes that fit in a line, keeping the first byte shown on top. */
void HexView::relayout()
{
	const QFontMetrics metrics(font());
	m_cx = qMax(1, metrics.horizontalAdvance(QLatin1Char('0')));
	m_cy = metrics.height() + metrics.leading() + kLineAdditionalSpace;
	m_ascent = metrics.ascent();

	const qsizetype firstByte = static_cast<qsizetype>(topLine()) * m_bytesPerLine;
	// the offset's digits: the size's, or at least the settings' when
	// they do not follow it
	m_offsetLength = hexDigits(m_data.size());
	if (!m_settings.automaticOffsetLength)
		m_offsetLength = qMax(m_offsetLength, m_settings.offsetLength);
	if (m_settings.automaticBytesPerLine)
	{
		const int room = screenColumns() - m_offsetLength - kByteSpace - kCharSpace;
		m_bytesPerLine = qMax(1, room / 4);
	}
	else
	{
		m_bytesPerLine = qMax(1, m_settings.bytesPerLine);
	}
	updateScrollBars();
	verticalScrollBar()->setValue(static_cast<int>(firstByte / m_bytesPerLine));
	viewport()->update();
}

void HexView::updateScrollBars()
{
	qsizetype longest = m_data.size();
	for (int i = 0; i < 2; ++i)
		longest = qMax(longest, siblingLength(i));
	// as upstream's, the range runs one line past the last
	const int lines = static_cast<int>((longest + m_bytesPerLine) / m_bytesPerLine);
	verticalScrollBar()->setRange(0, qMax(0, lines - screenLines() + 1));
	verticalScrollBar()->setPageStep(screenLines());
	verticalScrollBar()->setSingleStep(1);
	horizontalScrollBar()->setRange(0, qMax(0, charsPerLine() - screenColumns()));
	horizontalScrollBar()->setPageStep(screenColumns());
	horizontalScrollBar()->setSingleStep(1);
}

void HexView::setFontPointSize(qreal pointSize)
{
	QFont font = this->font();
	font.setPointSizeF(qMax<qreal>(2.0, pointSize));
	setFont(font);
	relayout();
}

/** Upstream's ZoomText: a point more or less (two at least), or the
    settings' size; the zoom is kept as the text comparison's, which it
    shares. */
void HexView::zoom(int amount)
{
	const qreal base = m_settings.basePointSize();
	const qreal size = amount == 0 ? base : qMax<qreal>(2.0, font().pointSizeF() + amount);
	setFontPointSize(size);
	QSettings().setValue(fontKey(), standardPointSize() * size / base);
}

void HexView::applySettings(const HexSettings &settings)
{
	m_settings = settings;
	QFont font = this->font();
	font.setPointSizeF(m_settings.basePointSize() * zoomFactor());
	setFont(font);
	relayout();
	changed();
}

void HexView::resizeEvent(QResizeEvent *event)
{
	QAbstractScrollArea::resizeEvent(event);
	relayout();
}

void HexView::scrollContentsBy(int, int)
{
	viewport()->update();
	emit scrolled();
}

bool HexView::event(QEvent *event)
{
	if (event->type() == QEvent::FontChange)
		relayout();
	return QAbstractScrollArea::event(event);
}

// --- painting ---------------------------------------------------------------

QColor HexView::separatorColor() const
{
	// Frhed's light gray, a dimmer one on a dark background
	return lm::Theme::instance()->dark() ? QColor(0x5a, 0x5a, 0x5a) : QColor(192, 192, 192);
}

QString HexView::lineTextForTest(int line) const
{
	QString text = QString::number(static_cast<qulonglong>(line) * m_bytesPerLine, 16)
		.rightJustified(m_offsetLength, QLatin1Char('0'));
	text += QString(kByteSpace, QLatin1Char(' '));
	QString chars;
	for (int k = 0; k < m_bytesPerLine; ++k)
	{
		const qsizetype i = static_cast<qsizetype>(line) * m_bytesPerLine + k;
		if (i < m_data.size())
		{
			const auto byte = static_cast<unsigned char>(m_data.at(i));
			text += hexDigit(byte >> 4);
			text += hexDigit(byte);
			chars += lm::hex::displayChar(byte, m_settings.oemCharacterSet);
		}
		else
		{
			text += i == m_data.size() ? QStringLiteral("__") : QStringLiteral("  ");
			chars += QLatin1Char(' ');
		}
		if (k < m_bytesPerLine - 1)
			text += QLatin1Char(' ');
	}
	return text + QString(kCharSpace + 1, QLatin1Char(' ')) + chars;
}

QPoint HexView::pointForTest(qsizetype byte, int nibble, bool chars) const
{
	const int bytesColumn = m_offsetLength + kByteSpace;
	const int column = chars
		? bytesColumn + m_bytesPerLine * 3 + kCharSpace + static_cast<int>(byte % m_bytesPerLine)
		: bytesColumn + static_cast<int>(byte % m_bytesPerLine) * 3 + nibble;
	const int line = static_cast<int>(byte / m_bytesPerLine) - topLine();
	return QPoint((column - horizontalScrollBar()->value()) * m_cx + m_cx / 2,
		line * m_cy + m_cy / 2);
}

void HexView::paintEvent(QPaintEvent *)
{
	QPainter painter(viewport());
	painter.setFont(font());
	const QPalette &pal = palette();
	const lm::DiffColors &colors = lm::diffColors();
	const QColor text = pal.color(QPalette::Text);
	const QColor background = pal.color(QPalette::Base);
	const QColor selText = pal.color(QPalette::HighlightedText);
	const QColor selBackground = pal.color(QPalette::Highlight);
	painter.fillRect(viewport()->rect(), background);

	const int top = topLine();
	const int left = horizontalScrollBar()->value();
	const int lines = lineCount();
	const int last = qMin(lines - 1, top + screenLines());
	const qsizetype lower = selectionLower();
	const qsizetype upper = selectionUpper();
	const int bytesColumn = m_offsetLength + kByteSpace;
	const int charsColumn = bytesColumn + m_bytesPerLine * 3 + kCharSpace;

	// one run of cells in the same colors: x and width in characters
	const auto cells = [&](int line, int column, const QString &string, const QColor &fg,
		const QColor &bg) {
		const QRect rect((column - left) * m_cx, (line - top) * m_cy,
			string.size() * m_cx, m_cy);
		if (rect.right() < 0 || rect.left() > viewport()->width())
			return;
		painter.fillRect(rect, bg);
		painter.setPen(fg);
		for (int k = 0; k < string.size(); ++k)
			painter.drawText(rect.left() + k * m_cx, rect.top() + kLineSpaceAbove + m_ascent,
				QString(string.at(k)));
	};

	for (int line = top; line <= last; ++line)
	{
		const qsizetype start = static_cast<qsizetype>(line) * m_bytesPerLine;
		cells(line, 0, QString::number(static_cast<qulonglong>(start), 16)
			.rightJustified(m_offsetLength, QLatin1Char('0'))
			+ QString(kByteSpace, QLatin1Char(' ')), text, background);
		cells(line, charsColumn - kCharSpace - 1, QString(kCharSpace + 1, QLatin1Char(' ')),
			text, background);
		for (int k = 0; k < m_bytesPerLine; ++k)
		{
			const qsizetype i = start + k;
			QString hex;
			QString character(QLatin1Char(' '));
			if (i < m_data.size())
			{
				const auto byte = static_cast<unsigned char>(m_data.at(i));
				hex = QString(hexDigit(byte >> 4)) + hexDigit(byte);
				character = QString(lm::hex::displayChar(byte, m_settings.oemCharacterSet));
			}
			else
			{
				hex = i == m_data.size() ? QStringLiteral("__") : QStringLiteral("  ");
			}
			if (k < m_bytesPerLine - 1)
				hex += QLatin1Char(' ');
			const bool diff = isDiff(i);
			const bool selected = m_selected && i >= lower && i <= upper;
			const QColor fg = selected ? selText : text;
			const QColor bg = selected ? (diff ? colors.selDiff : selBackground)
				: (diff ? colors.diff : background);
			cells(line, bytesColumn + k * 3, hex, fg, bg);
			cells(line, charsColumn + k, character, fg, bg);
		}

		// separators: one every four bytes, two before the characters
		painter.setPen(separatorColor());
		const int y0 = (line - top) * m_cy;
		const int y1 = y0 + m_cy - 1;
		for (int k = 0; k <= m_bytesPerLine; k += 4)
		{
			const int x = (bytesColumn - left + k * 3) * m_cx - m_cx / 2;
			painter.drawLine(x, y0, x, y1);
		}
		const int x = (bytesColumn - left + m_bytesPerLine * 3 + kCharSpace) * m_cx - m_cx / 2 - 1;
		painter.drawLine(x, y0, x, y1);
		painter.drawLine(x + 2, y0, x + 2, y1);
	}

	if (!hasFocus())
		return;
	painter.setCompositionMode(QPainter::CompositionMode_Difference);
	// the caret's byte in the other column, inverted (upstream's mark_char)
	if (!m_selected && m_curByte / m_bytesPerLine >= top
		&& m_curByte / m_bytesPerLine <= top + screenLines())
	{
		const int column = m_area == Area::Chars
			? bytesColumn + static_cast<int>(m_curByte % m_bytesPerLine) * 3
			: charsColumn + static_cast<int>(m_curByte % m_bytesPerLine);
		const int width = m_area == Area::Chars ? 2 : 1;
		const int y = static_cast<int>(m_curByte / m_bytesPerLine - top) * m_cy;
		painter.fillRect(QRect((column - left) * m_cx, y + kLineSpaceAbove, width * m_cx,
			m_cy - kLineAdditionalSpace), Qt::white);
	}
	// the caret: a blinking block on the nibble or the character
	// (upstream's set_caret_pos: at the end of a selection)
	if (m_caretShown && !m_selecting)
	{
		const qsizetype byte = m_selected ? m_selEnd : m_curByte;
		const qsizetype line = byte / m_bytesPerLine;
		if (line >= top && line < top + screenLines())
		{
			const int column = m_area == Area::Chars
				? charsColumn + static_cast<int>(byte % m_bytesPerLine)
				: bytesColumn + static_cast<int>(byte % m_bytesPerLine) * 3 + m_curNibble;
			painter.fillRect(QRect((column - left) * m_cx,
				static_cast<int>(line - top) * m_cy + kLineSpaceAbove, m_cx,
				m_cy - kLineAdditionalSpace), Qt::white);
		}
	}
}

// --- caret and selection ----------------------------------------------------

void HexView::changed()
{
	m_caretShown = true;
	if (hasFocus())
		m_blink->start();
	viewport()->update();
	emit statusChanged();
}

/** Upstream's snap_caret: scroll just enough to show the caret. */
void HexView::snapCaret()
{
	int column = m_offsetLength + kByteSpace;
	if (m_area == Area::Bytes)
		column += static_cast<int>(m_curByte % m_bytesPerLine) * 3 + m_curNibble;
	else
		column += m_bytesPerLine * 3 + kCharSpace + static_cast<int>(m_curByte % m_bytesPerLine);
	QScrollBar *h = horizontalScrollBar();
	if (column >= h->value() + screenColumns())
		h->setValue(column - (screenColumns() - 1));
	else if (column < h->value())
		h->setValue(column);

	const qsizetype row = m_curByte / m_bytesPerLine;
	QScrollBar *v = verticalScrollBar();
	if (row < v->value())
		v->setValue(static_cast<int>(row));
	else if (row >= v->value() + screenLines())
		v->setValue(static_cast<int>(row - (screenLines() - 1)));
}

/** Upstream's adjust_view_for_selection: the selection in the middle of
    the view, its start when it is taller than the view. */
void HexView::showSelection()
{
	if (!m_selected)
		return;
	const qsizetype lower = selectionLower();
	const qsizetype upper = selectionUpper();
	const qsizetype firstLine = lower / m_bytesPerLine;
	const qsizetype lastLine = upper / m_bytesPerLine;
	const int bytesColumn = m_offsetLength + kByteSpace;
	const int charsColumn = bytesColumn + m_bytesPerLine * 3 + kCharSpace;
	int startColumn, endColumn, width;
	if (m_area == Area::Bytes)
	{
		startColumn = bytesColumn + static_cast<int>(lower % m_bytesPerLine) * 3;
		endColumn = bytesColumn + static_cast<int>(upper % m_bytesPerLine) * 3 + 2;
		width = m_bytesPerLine * 3;
	}
	else
	{
		startColumn = charsColumn + static_cast<int>(lower % m_bytesPerLine);
		endColumn = charsColumn + static_cast<int>(upper % m_bytesPerLine);
		width = m_bytesPerLine;
	}
	const qsizetype lines = lastLine - firstLine + 1;
	const int columns = firstLine == lastLine ? endColumn - startColumn + 1 : width;
	const int screen = screenLines();
	const int screenWidth = screenColumns();

	QScrollBar *v = verticalScrollBar();
	qsizetype topLine;
	if (lines > screen)
		topLine = v->value() <= (firstLine + lastLine - screen + 1) / 2 ? firstLine
			: lastLine - screen + 1;
	else
		topLine = firstLine - (screen - (lastLine - firstLine)) / 2;

	int minColumn = qMin(startColumn, endColumn);
	int maxColumn = qMax(startColumn, endColumn);
	QScrollBar *h = horizontalScrollBar();
	int leftColumn;
	if (columns > screenWidth && maxColumn - minColumn + 1 > screenWidth)
	{
		leftColumn = qAbs(h->value() - minColumn) < qAbs(h->value() + screenWidth - 1 - maxColumn)
			? minColumn - 1 : maxColumn - screenWidth + 1;
	}
	else
	{
		if (maxColumn - minColumn + 1 < screenWidth && lines > 1)
		{
			minColumn = m_area == Area::Bytes ? bytesColumn : charsColumn;
			maxColumn = m_area == Area::Bytes ? bytesColumn + (m_bytesPerLine - 1) * 3 + 2
				: charsColumn + m_bytesPerLine;
		}
		leftColumn = minColumn - (screenWidth - (maxColumn - minColumn)) / 2;
	}
	h->setValue(qMax(0, leftColumn));
	v->setValue(static_cast<int>(qMax<qsizetype>(0, topLine)));
}

void HexView::setSelection(qsizetype start, qsizetype end)
{
	if (m_data.isEmpty())
		return;
	m_selStart = qBound<qsizetype>(0, start, m_data.size() - 1);
	m_selEnd = qBound<qsizetype>(0, end, m_data.size() - 1);
	m_curByte = qMin(m_selStart, m_selEnd);
	m_selected = true;
	showSelection();
	changed();
}

void HexView::selectAll()
{
	if (m_data.isEmpty())
		return;
	m_selected = true;
	m_selStart = 0;
	m_selEnd = m_data.size() - 1;
	showSelection();
	changed();
}

/** Upstream's synch_sibling: the siblings take the caret, the entering
    mode and the scroll position, and the selection when asked, cut to
    their own length. */
void HexView::syncSiblings(bool selection)
{
	for (HexView *sibling : m_siblings)
	{
		if (sibling == this)
			continue;
		const qsizetype length = sibling->m_data.size();
		sibling->m_area = m_area;
		sibling->m_curByte = qMin(m_curByte, length);
		sibling->m_curNibble = m_curNibble;
		if (selection)
		{
			sibling->m_selected = m_selected && (m_selStart < length || m_selEnd < length);
			sibling->m_selStart = qMin(m_selStart, length - 1);
			sibling->m_selEnd = qMin(m_selEnd, length - 1);
		}
		sibling->verticalScrollBar()->setValue(verticalScrollBar()->value());
		sibling->changed();
	}
}

bool HexView::selectNextDiff(bool fromStart)
{
	qsizetype common = m_data.size();
	for (int i = 0; i < 2; ++i)
		common = qMin(common, siblingLength(i));
	// from past the selection; without one, from the caret's own byte
	// (upstream's iGetEndOfSelection(1))
	qsizetype i = fromStart ? 0 : (m_selected ? selectionUpper() + 1 : m_curByte);
	while (i < common && !isDiff(i))
		++i;
	qsizetype j = i;
	while (j < common && isDiff(j))
		++j;
	if (i == j)
		return false;
	m_curByte = i;
	m_selStart = i;
	m_selEnd = j - 1;
	m_selected = true;
	showSelection();
	changed();
	syncSiblings(true);
	return true;
}

bool HexView::selectPrevDiff(bool fromEnd)
{
	qsizetype i = fromEnd ? m_data.size() : (m_selected ? selectionLower() : m_curByte);
	for (int k = 0; k < 2; ++k)
		i = qMin(i, siblingLength(k));
	do
		--i;
	while (i >= 0 && !isDiff(i));
	qsizetype j = i;
	while (j >= 0 && isDiff(j))
		--j;
	if (i == j)
		return false;
	m_selStart = m_curByte = j + 1;
	m_selEnd = i;
	m_selected = true;
	showSelection();
	changed();
	syncSiblings(true);
	return true;
}

// --- editing ----------------------------------------------------------------

void HexView::replaceBytes(qsizetype offset, qsizetype removed, const QByteArray &inserted,
	bool record)
{
	offset = qBound<qsizetype>(0, offset, m_data.size());
	removed = qBound<qsizetype>(0, removed, m_data.size() - offset);
	const QByteArray before = m_data.mid(offset, removed);
	m_data.replace(offset, removed, inserted);
	if (record && m_history != nullptr)
		m_history->push(this, offset, before, inserted);
	relayout();
	for (HexView *sibling : m_siblings)
		if (sibling != this)
			sibling->relayout();
	changed();
	emit dataEdited();
}

void HexView::showUndone(qsizetype offset)
{
	setFocus();
	m_curByte = qMin(offset, m_data.size());
	m_curNibble = 0;
	m_selected = false;
	snapCaret();
	changed();
}

void HexView::copySelectionFrom(const HexView *source)
{
	if (!source->m_selected || m_readOnly)
		return;
	const qsizetype i = source->selectionLower();
	const qsizetype j = source->selectionUpper();
	if (i > m_data.size())
		return;
	// the bytes at i..j here, the data growing to hold them
	const qsizetype replaced = qMin(j + 1, m_data.size()) - i;
	replaceBytes(i, replaced, source->m_data.mid(i, j - i + 1));
}

void HexView::copyAllFrom(const HexView *source)
{
	if (source->m_data.isEmpty() || m_readOnly)
		return;
	replaceBytes(0, qMin(source->m_data.size(), m_data.size()), source->m_data);
}

/** Upstream's character(): a hex digit sets the caret's nibble, a
    character its byte; over a selection the first byte takes it and the
    rest go; in insert mode (or at the end) a byte comes in first. */
void HexView::typeCharacter(const QString &text)
{
	if (m_selecting || text.isEmpty())
		return;
	if (m_readOnly)
		return;
	const QChar ch = text.at(0);
	int digit = -1;
	unsigned char byte = 0;
	if (m_area == Area::Bytes)
	{
		const char c = static_cast<char>(ch.toLower().toLatin1());
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'a' && c <= 'f')
			digit = c - 'a' + 10;
		else
			return;
	}
	else
	{
		// the character's byte in the character set (ANSI or OEM)
		const QByteArray bytes = lm::hex::charsetBytes(QString(ch), m_settings.oemCharacterSet);
		byte = static_cast<unsigned char>(bytes.at(0));
	}

	qsizetype at = m_curByte;
	QByteArray before;
	unsigned char value = 0;
	if (m_selected)
	{
		// the selection goes, its first byte cleared for the entry
		at = selectionLower();
		before = m_data.mid(at, selectionUpper() - at + 1);
		m_selected = false;
	}
	else if ((m_insertMode && m_curNibble == 0) || at == m_data.size())
	{
		m_curNibble = 0;
	}
	else
	{
		before = m_data.mid(at, 1);
		value = static_cast<unsigned char>(before.at(0));
	}
	m_curByte = at;
	if (m_area == Area::Bytes)
	{
		if (m_curNibble == 0)
		{
			value = static_cast<unsigned char>((value & 0x0f) | (digit << 4));
			m_curNibble = 1;
		}
		else
		{
			value = static_cast<unsigned char>((value & 0xf0) | digit);
			m_curNibble = 0;
			++m_curByte;
		}
	}
	else
	{
		value = byte;
		++m_curByte;
	}
	replaceBytes(at, before.size(), QByteArray(1, static_cast<char>(value)));
	snapCaret();
	changed();
}

/** Upstream's CMD_on_backspace: in insert mode the byte before the caret
    goes, otherwise the caret only steps back. */
void HexView::backspace()
{
	if (m_curByte <= 0)
		return;
	if (m_insertMode && !m_readOnly)
	{
		--m_curByte;
		replaceBytes(m_curByte, 1, QByteArray());
	}
	else
	{
		--m_curByte;
		snapCaret();
		changed();
	}
}

void HexView::selectFound(qsizetype start, qsizetype length)
{
	if (length <= 0)
		return;
	m_curByte = start;
	m_selStart = start;
	m_selEnd = start + length - 1;
	m_selected = true;
	showSelection();
	changed();
	syncSiblings(false);
}

void HexView::placeCaret(qsizetype offset)
{
	m_selected = false;
	m_curByte = qBound<qsizetype>(0, offset, m_data.size());
	m_curNibble = 0;
	snapCaret();
	changed();
}

/** Upstream's queryCommandEnabled: Copy over a selection or a byte; Cut,
    Delete and Paste besides not in a read-only pane. */
bool HexView::canCopy() const
{
	return m_selected || m_curByte < m_data.size();
}

bool HexView::canEdit() const
{
	return !m_readOnly;
}

QString HexView::selectionAsSpecialSyntax() const
{
	const qsizetype lower = m_selected ? selectionLower() : m_curByte;
	const qsizetype upper = m_selected ? selectionUpper() : m_curByte;
	return lm::hex::bytesToSpecialSyntax(m_data.mid(lower, upper - lower + 1));
}

/** Copy, Cut and Delete: Frhed's dialog of a start offset and an end or a
    count, from the selection (the caret's byte without one). */
void HexView::editRange(int mode)
{
	if (!canCopy() || (mode != HexRangeDialog::Copy && !canEdit()))
		return;
	const qsizetype lower = m_selected ? selectionLower() : m_curByte;
	const qsizetype upper = m_selected ? selectionUpper() : m_curByte;
	HexRangeDialog dialog(static_cast<HexRangeDialog::Mode>(mode), lower, upper,
		m_data.size(), this);
	if (!HexRangeDialog::run(&dialog))
		return;
	const qsizetype offset = dialog.offset();
	const qsizetype count = dialog.count();
	if (mode != HexRangeDialog::Delete)
	{
		// the clipboard takes the bytes in the special syntax (CF_TEXT)
		QGuiApplication::clipboard()->setText(
			lm::hex::bytesToSpecialSyntax(m_data.mid(offset, count)));
	}
	if (mode == HexRangeDialog::Copy)
		return;
	m_selected = false;
	replaceBytes(offset, count, QByteArray());
	m_curByte = qMax<qsizetype>(0, qMin(offset, m_data.size() - 1));
	m_curNibble = 0;
	snapCaret();
	changed();
}

void HexView::editCopy()
{
	editRange(HexRangeDialog::Copy);
}

void HexView::editCut()
{
	editRange(HexRangeDialog::Cut);
}

void HexView::editDelete()
{
	editRange(HexRangeDialog::Delete);
}

/** Frhed's "Paste with dialogue": over a selection or in insert mode the
    bytes come in (the selection going), otherwise they overwrite, as many
    times as asked, the bytes to skip between them left as they are. */
void HexView::editPaste()
{
	if (!canEdit())
		return;
	HexPasteDialog dialog(m_selected, m_insertMode, m_settings.bigEndian,
		m_settings.oemCharacterSet, this);
	if (!HexPasteDialog::run(&dialog))
		return;
	const QByteArray pattern = dialog.bytes();
	const qsizetype times = dialog.times();
	const qsizetype skip = dialog.skip();
	const qsizetype span = (times - 1) * (pattern.size() + skip) + pattern.size();
	if (m_selected || dialog.insert())
	{
		qsizetype at = m_curByte;
		qsizetype removed = 0;
		if (m_selected)
		{
			at = selectionLower();
			removed = selectionUpper() - at + 1;
			m_selected = false;
		}
		// the bytes after the paste point, between the copies
		const QByteArray rest = m_data.mid(at + removed, (times - 1) * skip);
		QByteArray region;
		for (qsizetype k = 0; k < times; ++k)
		{
			region += pattern;
			if (k < times - 1)
				region += rest.mid(k * skip, skip);
		}
		m_curByte = at;
		replaceBytes(at, removed + rest.size(), region);
	}
	else
	{
		if (m_data.size() - m_curByte < (skip + pattern.size()) * times)
		{
			lm::showError(this, HexPasteDialog::tr("Not enough space for overwriting."));
			return;
		}
		QByteArray region = m_data.mid(m_curByte, span);
		for (qsizetype k = 0; k < times; ++k)
			region.replace(k * (skip + pattern.size()), pattern.size(), pattern);
		replaceBytes(m_curByte, span, region);
	}
	snapCaret();
	changed();
}

/** Upstream's search from the caret: down from the byte after it, up
    through the bytes before it and the pattern's length less one, so that
    a match the caret stands in counts. */
void HexView::findFromCaret(const QByteArray &pattern, int direction, bool showDialogMessages)
{
	qsizetype found = -1;
	if (direction > 0)
	{
		const qsizetype from = m_curByte + 1;
		if (from <= m_data.size())
		{
			const qsizetype at = lm::hex::findBytes(m_data, from, m_data.size() - from, pattern,
				1, m_findMatchCase);
			if (at >= 0)
				found = from + at;
		}
	}
	else
	{
		const qsizetype end = qMin(m_curByte + pattern.size() - 1, m_data.size());
		found = lm::hex::findBytes(m_data, 0, end, pattern, -1, m_findMatchCase);
	}
	if (found >= 0)
	{
		m_curByte = found;
		m_selected = true;
		m_selStart = found;
		m_selEnd = found + pattern.size() - 1;
		showSelection();
		changed();
	}
	else if (showDialogMessages)
	{
		lm::showWarning(this, HexFindDialog::tr("Could not find data."));
	}
	else
	{
		lm::showInformation(this, tr("Could not find any more occurrences."));
	}
}

/** Frhed's Find dialog: the selection (when there is one) is what to
    find, else the last text looked for. */
void HexView::showFind()
{
	if (m_data.isEmpty())
		return;
	if (m_selected)
		m_findText = selectionAsSpecialSyntax();
	HexFindDialog dialog(m_findText, m_findDirection, m_findMatchCase, m_findUnicode, this);
	if (!HexFindDialog::run(&dialog))
		return;
	const QString text = dialog.text();
	m_findText = text;
	if (text.isEmpty())
	{
		lm::showError(this, HexFindDialog::tr("Findstring is zero bytes long."));
		return;
	}
	m_findMatchCase = dialog.matchCase();
	m_findUnicode = dialog.unicode();
	m_findDirection = dialog.direction();
	QByteArray pattern;
	if (m_findUnicode)
	{
		// the text as UTF-16, little-endian
		for (const QChar ch : text)
		{
			pattern += static_cast<char>(ch.unicode() & 0xff);
			pattern += static_cast<char>(ch.unicode() >> 8);
		}
	}
	else
	{
		pattern = lm::hex::specialSyntaxToBytes(text, m_settings.bigEndian,
			m_settings.oemCharacterSet);
	}
	if (pattern.isEmpty())
	{
		lm::showError(this, HexFindDialog::tr("Findstring is zero bytes long."));
		return;
	}
	findFromCaret(pattern, m_findDirection, true);
}

/** Upstream's CMD_findnext and CMD_findprev (F3, Shift+F3): the selection
    is what to find when there is one; without a text, the dialog. */
void HexView::findNext()
{
	if (m_data.isEmpty())
		return;
	if (m_selected)
		m_findText = selectionAsSpecialSyntax();
	if (m_findText.isEmpty())
	{
		lm::showError(this, tr("String to find not specified."));
		return;
	}
	const QByteArray pattern = lm::hex::specialSyntaxToBytes(m_findText, m_settings.bigEndian, m_settings.oemCharacterSet);
	if (pattern.isEmpty())
	{
		m_findDirection = 1;
		showFind();
		return;
	}
	findFromCaret(pattern, 1, false);
}

void HexView::findPrevious()
{
	if (m_data.isEmpty())
		return;
	if (m_selected)
		m_findText = selectionAsSpecialSyntax();
	if (m_findText.isEmpty())
	{
		lm::showError(this, tr("String to find not specified."));
		return;
	}
	const QByteArray pattern = lm::hex::specialSyntaxToBytes(m_findText, m_settings.bigEndian, m_settings.oemCharacterSet);
	if (pattern.isEmpty())
	{
		m_findDirection = -1;
		showFind();
		return;
	}
	findFromCaret(pattern, -1, false);
}

void HexView::showReplace()
{
	if (m_data.isEmpty() || !canEdit())
		return;
	HexReplaceDialog dialog(this, this);
	HexReplaceDialog::run(&dialog);
}

/** Frhed's "Go to Offset": the caret goes, the selection stays as it is. */
void HexView::showGoTo()
{
	if (m_data.isEmpty())
		return;
	HexGoToDialog dialog(m_curByte, m_data.size(), this);
	if (!HexGoToDialog::run(&dialog))
		return;
	m_curByte = dialog.offset();
	snapCaret();
	changed();
}

/** Frhed's context menu (IDR_CONTEXTMENU): the clipboard commands, then
    the finding ones, each enabled as its command is. */
void HexView::contextMenuEvent(QContextMenuEvent *event)
{
	// over the offsets a right click lengthens them instead, when their
	// length does not follow the size
	if (event->reason() == QContextMenuEvent::Mouse
		&& hitTest(event->pos()).where == Hit::Offsets)
	{
		if (!m_settings.automaticOffsetLength)
		{
			HexSettings settings = m_settings;
			settings.offsetLength = qMin(16, settings.offsetLength + 1);
			settings.save();
		}
		return;
	}
	QMenu menu(this);
	const auto add = [&menu](const QString &text, const QKeySequence &keys, bool enabled,
		auto slot) {
		QAction *action = menu.addAction(text, slot);
		action->setShortcut(keys);
		action->setShortcutVisibleInContextMenu(true);
		action->setEnabled(enabled);
	};
	const bool haveData = !m_data.isEmpty();
	add(tr("Cu&t..."), QKeySequence::Cut, canCopy() && canEdit(), [this]() { editCut(); });
	add(tr("&Copy..."), QKeySequence::Copy, canCopy(), [this]() { editCopy(); });
	add(tr("&Paste..."), QKeySequence::Paste,
		canEdit() && !QGuiApplication::clipboard()->text().isEmpty(), [this]() { editPaste(); });
	add(tr("&Delete"), QKeySequence(Qt::Key_Delete), canCopy() && canEdit(),
		[this]() { editDelete(); });
	menu.addSeparator();
	add(tr("&Find..."), QKeySequence::Find, haveData, [this]() { showFind(); });
#ifdef Q_OS_MACOS
	const QKeySequence replaceKeys(Qt::CTRL | Qt::ALT | Qt::Key_F);
	const QKeySequence goToKeys(Qt::META | Qt::Key_G);
#else
	const QKeySequence replaceKeys(Qt::CTRL | Qt::Key_H);
	const QKeySequence goToKeys(Qt::CTRL | Qt::Key_G);
#endif
	add(tr("Replace..."), replaceKeys, haveData && canEdit(), [this]() { showReplace(); });
	const bool canRepeat = haveData && (!m_findText.isEmpty() || m_selected);
	add(tr("Find next"), QKeySequence(Qt::Key_F3), canRepeat, [this]() { findNext(); });
	add(tr("Find previous"), QKeySequence(Qt::SHIFT | Qt::Key_F3), canRepeat,
		[this]() { findPrevious(); });
	add(tr("&Go To..."), goToKeys, haveData, [this]() { showGoTo(); });
	if (contextMenuPresenter())
		contextMenuPresenter()(&menu);
	else
		menu.exec(event->globalPos());
}

void HexView::setContextMenuPresenterForTest(std::function<void(QMenu *)> presenter)
{
	contextMenuPresenter() = std::move(presenter);
}

// --- keyboard ---------------------------------------------------------------

bool HexView::focusNextPrevChild(bool)
{
	// Tab switches between the hex and the text column
	return false;
}

void HexView::keyPressEvent(QKeyEvent *event)
{
	const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
	const bool shift = (mods & Qt::ShiftModifier) != 0;
	const bool ctrl = (mods & Qt::ControlModifier) != 0;
	const int key = event->key();

	// Frhed's accelerators: Ctrl+arrows and Ctrl+Page Up/Down scroll the
	// view and leave the caret
	if (ctrl && !shift)
	{
		QScrollBar *v = verticalScrollBar();
		QScrollBar *h = horizontalScrollBar();
		switch (key)
		{
		case Qt::Key_Up: v->setValue(v->value() - 1); return;
		case Qt::Key_Down: v->setValue(v->value() + 1); return;
		case Qt::Key_Left: h->setValue(h->value() - 1); return;
		case Qt::Key_Right: h->setValue(h->value() + 1); return;
		case Qt::Key_PageUp: v->setValue(v->value() - screenLines()); return;
		case Qt::Key_PageDown: v->setValue(v->value() + screenLines()); return;
		default: break;
		}
	}
	// the main window's Edit commands and Frhed's own accelerators, which
	// open the editor's dialogs (Del included)
	if (event->matches(QKeySequence::Copy))
	{
		editCopy();
		return;
	}
	if (event->matches(QKeySequence::Cut))
	{
		editCut();
		return;
	}
	if (event->matches(QKeySequence::Paste))
	{
		editPaste();
		return;
	}
	if (event->matches(QKeySequence::SelectAll))
	{
		selectAll();
		return;
	}
	if (key == Qt::Key_Delete && mods == Qt::NoModifier)
	{
		editDelete();
		return;
	}
	switch (key)
	{
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down:
	case Qt::Key_PageUp:
	case Qt::Key_PageDown:
	case Qt::Key_Home:
	case Qt::Key_End:
	case Qt::Key_Escape:
		moveCaret(key, shift, ctrl);
		return;
	case Qt::Key_Tab:
	case Qt::Key_Backtab:
		if (ctrl)
			break;
		m_area = m_area == Area::Bytes ? Area::Chars : Area::Bytes;
		if (m_selected)
			showSelection();
		else
			snapCaret();
		changed();
		return;
	case Qt::Key_Insert:
		if (mods == Qt::NoModifier)
		{
			toggleInsertMode();
			return;
		}
		break;
	case Qt::Key_Backspace:
		if (mods == Qt::NoModifier)
		{
			backspace();
			return;
		}
		break;
	default:
		break;
	}
	const QString text = event->text();
	if (!text.isEmpty() && !ctrl && (mods & Qt::MetaModifier) == 0 && text.at(0).isPrint())
	{
		typeCharacter(text);
		return;
	}
	QAbstractScrollArea::keyPressEvent(event);
}

/** Upstream's keydown: the arrows step a nibble in the hex column (a byte
    in the text column), a line up or down, a screen with Page Up and
    Down, to the line's ends with Home and End (the data's with Ctrl);
    with Shift they move the end of the selection; without it a selection
    goes, the caret first moving to the selection's side the key points
    to; Escape leaves the caret at the selection's start. */
void HexView::moveCaret(int key, bool shift, bool ctrl)
{
	qsizetype *target = nullptr;
	qsizetype original = 0;
	const bool wasSelected = m_selected;
	if (shift)
	{
		target = &m_selEnd;
		if (!m_selected)
		{
			m_selStart = m_selEnd = original = m_curByte;
			m_selected = true;
		}
		else
		{
			original = m_selEnd;
		}
	}
	if (!shift || ctrl || key == Qt::Key_Escape)
	{
		original = m_curByte;
		if (m_selected)
		{
			const qsizetype lower = selectionLower();
			const qsizetype upper = selectionUpper();
			switch (key)
			{
			case Qt::Key_Escape:
			case Qt::Key_Left:
			case Qt::Key_Up:
			case Qt::Key_PageUp:
			case Qt::Key_Home:
				m_curByte = lower;
				original = upper;
				m_curNibble = 0;
				break;
			default:
				m_curByte = upper;
				original = lower;
				m_curNibble = 1;
				break;
			}
		}
		else
		{
			m_selStart = m_curByte;
		}
		if (ctrl && shift)
		{
			target = &m_selEnd;
			original = m_selEnd;
			m_selected = true;
		}
		else
		{
			m_selected = false;
			target = &m_curByte;
		}
	}

	const int oldNibble = m_curNibble;
	const qsizetype last = m_selected ? m_data.size() - 1 : m_data.size();
	qsizetype step = 0;
	switch (key)
	{
	case Qt::Key_Left:
		if (m_area == Area::Chars || m_selected)
			step = 1;
		else
		{
			m_curNibble = m_curNibble == 0 ? 1 : 0;
			step = m_curNibble;
		}
		break;
	case Qt::Key_Right:
		step = m_area == Area::Chars || m_selected ? 1 : m_curNibble;
		if (m_area == Area::Bytes)
			m_curNibble = m_curNibble == 0 ? 1 : 0;
		break;
	case Qt::Key_Up:
	case Qt::Key_Down:
		step = m_bytesPerLine;
		break;
	case Qt::Key_PageUp:
	case Qt::Key_PageDown:
		step = static_cast<qsizetype>(screenLines()) * m_bytesPerLine;
		break;
	default:
		break;
	}
	switch (key)
	{
	case Qt::Key_Left:
	case Qt::Key_Up:
	case Qt::Key_PageUp:
		*target = *target >= step ? *target - step : 0;
		break;
	case Qt::Key_Right:
	case Qt::Key_Down:
	case Qt::Key_PageDown:
		*target += step;
		break;
	case Qt::Key_Home:
		*target = ctrl ? 0 : *target / m_bytesPerLine * m_bytesPerLine;
		m_curNibble = 0;
		break;
	case Qt::Key_End:
		*target = ctrl ? last : (*target / m_bytesPerLine + 1) * m_bytesPerLine - 1;
		m_curNibble = 1;
		break;
	default:
		break;
	}
	if (m_selected)
	{
		if (last < 0)
		{
			m_selected = false;
			m_curNibble = 0;
			m_curByte = 0;
		}
		else
		{
			if (m_selStart > last)
			{
				m_selStart = last;
				m_curNibble = 1;
			}
			if (m_selEnd > last)
			{
				m_selEnd = last;
				m_curNibble = 1;
			}
		}
	}
	else if (m_curByte > last)
	{
		m_curByte = last;
		m_curNibble = 1;
	}
	if (original != *target || oldNibble != m_curNibble || wasSelected != m_selected)
	{
		if (m_selected)
		{
			m_curByte = *target;
			m_curNibble = 0;
		}
		snapCaret();
		changed();
	}
}

// --- mouse ------------------------------------------------------------------

/** Upstream's get_pos: what is under a point of the view. */
HexView::Hit HexView::hitTest(const QPoint &point) const
{
	Hit hit;
	const int column = point.x() / m_cx + horizontalScrollBar()->value();
	const qsizetype line = point.y() / m_cy + topLine();
	const int startOfBytes = m_offsetLength + kByteSpace;
	const int startOfChars = charsPerLine() - m_bytesPerLine;
	qsizetype byte = 0;
	if (column >= 0 && column < startOfBytes)
	{
		hit.where = Hit::Offsets;
		hit.pos = line * m_bytesPerLine;
	}
	else if (column >= startOfBytes && column < startOfChars - kCharSpace - 1)
	{
		const int relative = column - startOfBytes;
		hit.where = Hit::Bytes;
		hit.nibble = relative % 3;
		byte = relative / 3;
		if (hit.nibble == 2)
		{
			// the space after a byte: the nearer of the two
			if (point.x() % m_cx > m_cx / 2)
			{
				++byte;
				hit.nibble = 0;
			}
			else
			{
				hit.nibble = 1;
			}
		}
		hit.pos = line * m_bytesPerLine + byte;
	}
	else if (column >= startOfChars - kCharSpace - 1 && column < startOfChars)
	{
		const int x = point.x() + horizontalScrollBar()->value() * m_cx;
		if (x <= m_cx * (startOfChars - 1) - m_cx * kCharSpace / 2)
		{
			hit.where = Hit::Bytes;
			hit.nibble = 1;
			byte = m_bytesPerLine - 1;
		}
		else
		{
			hit.where = Hit::Chars;
			byte = 0;
		}
		hit.pos = line * m_bytesPerLine + byte;
	}
	else if (column >= startOfChars && column < charsPerLine())
	{
		hit.where = Hit::Chars;
		hit.pos = line * m_bytesPerLine + (column - startOfChars);
	}
	else
	{
		hit.where = Hit::After;
		hit.pos = (line + 1) * m_bytesPerLine - 1;
	}
	if (hit.pos > m_data.size())
	{
		hit.nibble = 1;
		hit.pos = m_data.size();
	}
	if (hit.pos < 0)
		hit.pos = 0;
	return hit;
}

void HexView::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
	{
		QAbstractScrollArea::mousePressEvent(event);
		return;
	}
	setFocus(Qt::MouseFocusReason);
	Hit hit = hitTest(event->position().toPoint());
	// upstream's: a click on the offsets shortens them, when their length
	// does not follow the size
	if (hit.where == Hit::Offsets && !m_settings.automaticOffsetLength
		&& m_settings.offsetLength > 1)
	{
		HexSettings settings = m_settings;
		--settings.offsetLength;
		settings.save();
		return;
	}
	m_pressed = true;
	m_pressPoint = m_mousePoint = event->position().toPoint();
	m_pressPos = m_lastPos = hit.pos;
	if (event->modifiers() & Qt::ShiftModifier)
	{
		// extend the selection to the byte clicked
		if (hit.nibble != 2 && !m_data.isEmpty())
		{
			if (hit.pos == m_data.size())
				--hit.pos;
			m_selecting = true;
			if (m_selEnd != hit.pos || !m_selected)
			{
				if (!m_selected)
				{
					m_selected = true;
					m_selStart = m_curByte;
				}
				m_selEnd = hit.pos;
				changed();
			}
		}
		return;
	}
	m_dragDelay->start();
}

void HexView::beginDragSelection()
{
	m_dragDelay->stop();
	if (!m_pressed || m_data.isEmpty())
		return;
	const qsizetype size = m_data.size();
	m_selStart = qBound<qsizetype>(0, m_pressPos, size - 1);
	m_selEnd = qBound<qsizetype>(0, m_lastPos, size - 1);
	m_selected = m_selecting = true;
	m_scrollTimer->start();
	changed();
}

void HexView::mouseMoveEvent(QMouseEvent *event)
{
	m_mousePoint = event->position().toPoint();
	if (!m_pressed)
	{
		QAbstractScrollArea::mouseMoveEvent(event);
		return;
	}
	const Hit hit = hitTest(m_mousePoint);
	if (m_selecting)
	{
		qsizetype pos = hit.pos;
		if (pos >= m_data.size())
			pos = m_data.size() - 1;
		if (m_selEnd != pos)
		{
			m_selected = true;
			m_selEnd = pos;
			changed();
		}
	}
	else if (m_dragDelay->isActive()
		&& (m_mousePoint - m_pressPoint).manhattanLength() > QApplication::startDragDistance())
	{
		m_lastPos = hit.pos;
		beginDragSelection();
	}
	m_lastPos = hit.pos;
}

/** Upstream's lbuttonup: a click puts the caret on the nibble or the
    character clicked (a selection goes); a drag ends selecting, in the
    column it ended in. */
void HexView::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || !m_pressed)
	{
		QAbstractScrollArea::mouseReleaseEvent(event);
		return;
	}
	m_pressed = false;
	m_dragDelay->stop();
	m_scrollTimer->stop();
	const Hit hit = hitTest(event->position().toPoint());
	if (m_selecting)
	{
		m_curNibble = 0;
		m_area = hit.where == Hit::Bytes || hit.where == Hit::Offsets ? Area::Bytes : Area::Chars;
	}
	else
	{
		if (hit.where == Hit::Bytes || hit.where == Hit::Offsets)
		{
			if (hit.nibble == 2)
			{
				m_selecting = false;
				return;
			}
			m_curNibble = hit.nibble;
			m_area = Area::Bytes;
		}
		else
		{
			m_area = Area::Chars;
		}
		m_selected = false;
		m_curByte = hit.pos;
	}
	m_selecting = false;
	changed();
}

/** Selecting past the view's edge scrolls it (upstream's scroll timer). */
void HexView::autoScroll()
{
	if (!m_selecting)
	{
		m_scrollTimer->stop();
		return;
	}
	QScrollBar *v = verticalScrollBar();
	QScrollBar *h = horizontalScrollBar();
	if (m_mousePoint.y() >= screenLines() * m_cy)
		v->setValue(v->value() + 1);
	else if (m_mousePoint.y() < m_cy)
		v->setValue(v->value() - 1);
	if (m_mousePoint.x() >= screenColumns() * m_cx)
		h->setValue(h->value() + 1);
	else if (m_mousePoint.x() < m_cx)
		h->setValue(h->value() - 1);
}

void HexView::wheelEvent(QWheelEvent *event)
{
	// Ctrl with the wheel zooms, as in upstream's view
	if (event->modifiers() & Qt::ControlModifier)
	{
		const int delta = event->angleDelta().y();
		if (delta != 0)
			zoom(delta > 0 ? 1 : -1);
		event->accept();
		return;
	}
	QAbstractScrollArea::wheelEvent(event);
}

void HexView::focusInEvent(QFocusEvent *event)
{
	QAbstractScrollArea::focusInEvent(event);
	m_caretShown = true;
	m_blink->start();
	viewport()->update();
	emit focused();
}

/** Upstream's kill_focus: the siblings take the caret and the entering
    mode, so that the next pane focused stands where this one did. */
void HexView::focusOutEvent(QFocusEvent *event)
{
	QAbstractScrollArea::focusOutEvent(event);
	m_blink->stop();
	viewport()->update();
	for (HexView *sibling : m_siblings)
	{
		if (sibling == this)
			continue;
		sibling->m_area = m_area;
		sibling->m_curByte = qMin(m_curByte, sibling->m_data.size());
		sibling->m_curNibble = m_curNibble;
		sibling->viewport()->update();
	}
}

// --- status -----------------------------------------------------------------

QString HexView::statusPositionText() const
{
	if (m_selected)
	{
		const qsizetype lower = selectionLower();
		const qsizetype upper = selectionUpper();
		return tr("Selected: Offset %1=0x%2 to %3=0x%4 (%n byte(s))", nullptr,
			static_cast<int>(upper - lower + 1))
			.arg(lower).arg(QString::number(lower, 16))
			.arg(upper).arg(QString::number(upper, 16));
	}
	return tr("Offset %1=0x%2").arg(m_curByte).arg(QString::number(m_curByte, 16));
}

QString HexView::statusBitsText() const
{
	if (m_selected || m_curByte >= m_data.size())
		return QString();
	const auto byte = static_cast<unsigned char>(m_data.at(m_curByte));
	return tr("Bits") + QLatin1Char('=')
		+ QString::number(byte, 2).rightJustified(8, QLatin1Char('0'));
}

QString HexView::statusValuesText() const
{
	if (m_selected)
		return QString();
	const qsizetype available = m_data.size() - m_curByte;
	if (available < 1)
		return tr("END");
	// the byte, the word and the longword at the caret, in the byte order
	// and the signedness the settings ask for
	const auto at = [this](qsizetype i) {
		return static_cast<quint32>(static_cast<unsigned char>(m_data.at(m_curByte + i)));
	};
	const bool big = m_settings.bigEndian;
	const bool sign = !m_settings.unsignedValues;
	const quint32 byte = at(0);
	QString text = (sign ? tr("Signed") : tr("Unsigned")) + QStringLiteral(": ") + tr("B")
		+ QLatin1Char(':') + (sign ? QString::number(static_cast<qint8>(byte)) : QString::number(byte));
	if (available >= 2)
	{
		const quint32 word = big ? (at(0) << 8 | at(1)) : (at(1) << 8 | at(0));
		text += QLatin1Char(',') + tr("W") + QLatin1Char(':')
			+ (sign ? QString::number(static_cast<qint16>(word)) : QString::number(word));
		if (available >= 4)
		{
			const quint32 longword = big ? (word << 16 | at(2) << 8 | at(3))
				: (word | (at(3) << 8 | at(2)) << 16);
			text += QLatin1Char(',') + tr("L") + QLatin1Char(':')
				+ (sign ? QString::number(static_cast<qint32>(longword)) : QString::number(longword));
		}
	}
	return text;
}

QString HexView::statusModeText() const
{
	const QString mode = m_readOnly ? tr("READ") : (m_insertMode ? tr("INS") : tr("OVR"));
	return (m_settings.oemCharacterSet ? tr("OEM") : tr("ANSI")) + QStringLiteral(" / ") + mode
		+ QStringLiteral(" / ") + (m_settings.bigEndian ? tr("B") : tr("L"));
}

QString HexView::statusSizeText() const
{
	return tr("Size") + QStringLiteral(": ") + QString::number(m_data.size());
}
