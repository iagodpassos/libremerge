// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "FileCompareView.h"

#include <QAction>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QToolBar>
#include <QVBoxLayout>

#include "EngineOptions.h"

// engine
#include "DiffWrapper.h"
#include "DiffList.h"
#include "PathContext.h"
#include "UniFile.h"
#include "unicoder.h"
#include "stringdiffs.h"

namespace
{

const QColor kDiffColor(255, 243, 176);        // changed lines
const QColor kCurrentDiffColor(255, 213, 100); // the selected difference
const QColor kTrivialColor(230, 230, 230);     // whitespace-only/trivial
const QColor kMissingColor(224, 236, 255);     // block exists only on other side
const QColor kWordDiffColor(255, 199, 90);     // changed words within a line
const QColor kWordDiffCurrentColor(255, 158, 40);

// performance guards for intra-line highlighting
constexpr int kMaxWordDiffLinesPerBlock = 400;
constexpr int kMaxWordDiffLineLength = 2048;

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

} // namespace

FileCompareView::FileCompareView(QWidget *parent)
	: QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto *toolbar = new QToolBar(this);
	toolbar->setIconSize(QSize(16, 16));
	auto addToolAction = [this, toolbar](const QString &text, const QKeySequence &shortcut,
		auto slot) -> QAction * {
		QAction *action = toolbar->addAction(text);
		action->setShortcut(shortcut);
		action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		addAction(action);
		connect(action, &QAction::triggered, this, slot);
		return action;
	};
	addToolAction(tr("\xE2\x96\xB2 Prev"), QKeySequence(Qt::ALT | Qt::Key_Up),
		[this]() { gotoPrevDiff(); });
	addToolAction(tr("\xE2\x96\xBC Next"), QKeySequence(Qt::ALT | Qt::Key_Down),
		[this]() { gotoNextDiff(); });
	toolbar->addSeparator();
	addToolAction(tr("Copy \xE2\x86\x92 Right"), QKeySequence(Qt::ALT | Qt::Key_Right),
		[this]() { copyCurrentDiff(0); });
	addToolAction(tr("Copy \xE2\x86\x90 Left"), QKeySequence(Qt::ALT | Qt::Key_Left),
		[this]() { copyCurrentDiff(1); });
	toolbar->addSeparator();
	addToolAction(tr("Recompare"), QKeySequence(Qt::Key_F5),
		[this]() { recompare(); });
	m_actSave = addToolAction(tr("Save"), QKeySequence::Save,
		[this]() { QString error; saveModified(&error); });
	m_actSave->setEnabled(false);
	layout->addWidget(toolbar);

	auto *panes = new QHBoxLayout;
	panes->setContentsMargins(0, 0, 0, 0);
	panes->setSpacing(1);
	const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	for (int i = 0; i < 2; ++i)
	{
		m_panes[i] = new QPlainTextEdit(this);
		m_panes[i]->setLineWrapMode(QPlainTextEdit::NoWrap);
		m_panes[i]->setFont(mono);
		panes->addWidget(m_panes[i]);
		connect(m_panes[i]->verticalScrollBar(), &QScrollBar::valueChanged,
			this, [this, i](int value) { syncScroll(i, value); });
		connect(m_panes[i]->document(), &QTextDocument::contentsChanged,
			this, [this, i]() {
				if (m_syncing)
					return;
				m_diffStale = true;
				setSideModified(i, true);
			});
	}
	layout->addLayout(panes, 1);

	m_status = new QLabel(this);
	m_status->setContentsMargins(6, 3, 6, 3);
	layout->addWidget(m_status);
}

bool FileCompareView::compare(const QString &leftPath, const QString &rightPath, QString *error)
{
	if (!loadSide(0, leftPath, error) || !loadSide(1, rightPath, error))
		return false;
	if (!runDiff(error))
		return false;
	m_current = nextNonTrivial(-1, +1);
	applyHighlights();
	updateStatus();
	if (m_current >= 0)
		gotoDiff(m_current);
	return true;
}

bool FileCompareView::loadSide(int side, const QString &path, QString *error)
{
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
	m_syncing = false;
	s.modified = false;
	return true;
}

bool FileCompareView::runDiff(QString *error)
{
	// Diff the current pane contents through temp files (the engine's
	// diff core operates on files, like upstream does for edited buffers).
	QTemporaryFile temp[2];
	for (int i = 0; i < 2; ++i)
	{
		if (!temp[i].open())
		{
			if (error != nullptr)
				*error = tr("cannot create temporary file");
			return false;
		}
		const QByteArray bytes = m_panes[i]->toPlainText().toUtf8();
		temp[i].write(bytes);
		temp[i].flush();
	}

	CDiffWrapper wrapper;
	DIFFOPTIONS options = lm::currentDiffOptions();
	DiffList diffList;
	wrapper.SetCreateDiffList(&diffList);
	wrapper.SetPaths({ temp[0].fileName().toStdString(),
		temp[1].fileName().toStdString() }, false);
	wrapper.SetOptions(&options);
	if (!wrapper.RunFileDiff())
	{
		if (error != nullptr)
			*error = tr("the diff engine failed");
		return false;
	}

	m_blocks.clear();
	m_diffCount = 0;
	for (int i = 0; i < diffList.GetSize(); ++i)
	{
		DIFFRANGE dr;
		diffList.GetDiff(i, dr);
		Block block{};
		for (int side = 0; side < 2; ++side)
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
	computeWordSpans();
	return true;
}

/** Intra-line (word-level) diff spans for every changed block, computed
    once per diff run with the engine's stringdiffs. */
void FileCompareView::computeWordSpans()
{
	m_wordSpans.clear();
	const DIFFOPTIONS options = lm::currentDiffOptions();
	QTextDocument *docs[2] = { m_panes[0]->document(), m_panes[1]->document() };

	for (size_t b = 0; b < m_blocks.size(); ++b)
	{
		const Block &block = m_blocks[b];
		if (block.trivial)
			continue;
		const int count0 = block.end[0] - block.begin[0] + 1;
		const int count1 = block.end[1] - block.begin[1] + 1;
		if (count0 <= 0 || count1 <= 0)
			continue; // insertion/deletion: whole-line highlight is enough
		const int pairs = qMin(count0, count1);
		if (pairs > kMaxWordDiffLinesPerBlock)
			continue;

		for (int k = 0; k < pairs; ++k)
		{
			const int lineNo[2] = { block.begin[0] + k, block.begin[1] + k };
			const QString lines[2] = {
				docs[0]->findBlockByNumber(lineNo[0]).text(),
				docs[1]->findBlockByNumber(lineNo[1]).text(),
			};
			if (lines[0] == lines[1]
				|| lines[0].size() > kMaxWordDiffLineLength
				|| lines[1].size() > kMaxWordDiffLineLength)
				continue;

			const std::vector<strdiff::wdiff> wdiffs = strdiff::ComputeWordDiffs(
				lines[0].toStdString(), lines[1].toStdString(),
				!options.bIgnoreCase, strdiff::EOL_STRICT,
				options.nIgnoreWhitespace, options.bIgnoreNumbers,
				0 /*breakType*/, false /*byte_level*/);

			for (const strdiff::wdiff &wd : wdiffs)
			{
				for (int side = 0; side < 2; ++side)
				{
					if (wd.end[side] < wd.begin[side])
						continue; // nothing on this side
					WordSpan span;
					span.side = side;
					span.line = lineNo[side];
					span.blockIndex = static_cast<int>(b);
					byteRangeToU16(lines[side], wd.begin[side], wd.end[side],
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
	for (int side = 0; side < 2; ++side)
	{
		QList<QTextEdit::ExtraSelection> selections;
		QTextDocument *doc = m_panes[side]->document();
		for (size_t b = 0; b < m_blocks.size(); ++b)
		{
			const Block &block = m_blocks[b];
			const bool current = (static_cast<int>(b) == m_current);
			const bool empty = block.end[side] < block.begin[side];
			QColor color = block.trivial ? kTrivialColor
				: (current ? kCurrentDiffColor : kDiffColor);
			int first = block.begin[side];
			int last = empty ? block.begin[side] - 1 : block.end[side];
			if (empty)
			{
				color = current ? kCurrentDiffColor : kMissingColor;
				first = last = qMax(0, block.begin[side] - 1);
			}
			for (int line = first; line <= last; ++line)
			{
				const QTextBlock textBlock = doc->findBlockByNumber(line);
				if (!textBlock.isValid())
					continue;
				QTextEdit::ExtraSelection selection;
				selection.format.setBackground(color);
				selection.format.setProperty(QTextFormat::FullWidthSelection, true);
				selection.cursor = QTextCursor(textBlock);
				selections.append(selection);
			}
		}
		// word-level spans on top of the line backgrounds
		for (const WordSpan &span : m_wordSpans)
		{
			if (span.side != side)
				continue;
			const QTextBlock textBlock = doc->findBlockByNumber(span.line);
			if (!textBlock.isValid())
				continue;
			QTextEdit::ExtraSelection selection;
			selection.format.setBackground(span.blockIndex == m_current
				? kWordDiffCurrentColor : kWordDiffColor);
			QTextCursor cursor(textBlock);
			cursor.setPosition(textBlock.position() + span.start);
			cursor.setPosition(textBlock.position() + span.start + span.length,
				QTextCursor::KeepAnchor);
			selection.cursor = cursor;
			selections.append(selection);
		}
		m_panes[side]->setExtraSelections(selections);
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
				if (!m_blocks[b].trivial)
					++index;
		}
		text = index > 0
			? tr("Difference %1 of %2").arg(index).arg(m_diffCount)
			: tr("%n difference(s)", nullptr, m_diffCount);
	}
	if (m_sides[0].modified || m_sides[1].modified)
		text += tr("  \xE2\x80\xA2 unsaved changes");
	m_status->setText(text);
}

int FileCompareView::nextNonTrivial(int from, int direction) const
{
	for (int b = from + direction; b >= 0 && b < static_cast<int>(m_blocks.size()); b += direction)
		if (!m_blocks[b].trivial)
			return b;
	return -1;
}

void FileCompareView::gotoDiff(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(m_blocks.size()))
		return;
	m_current = blockIndex;
	for (int side = 0; side < 2; ++side)
	{
		const int line = m_blocks[blockIndex].begin[side];
		QTextCursor cursor(m_panes[side]->document()->findBlockByNumber(qMax(0, line)));
		m_syncing = true;
		m_panes[side]->setTextCursor(cursor);
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
	const int next = nextNonTrivial(m_current, +1);
	if (next >= 0)
		gotoDiff(next);
}

void FileCompareView::gotoPrevDiff()
{
	if (m_diffStale)
		recompare();
	const int prev = nextNonTrivial(m_current < 0 ? static_cast<int>(m_blocks.size()) : m_current, -1);
	if (prev >= 0)
		gotoDiff(prev);
}

void FileCompareView::recompare()
{
	QString error;
	if (!runDiff(&error))
		return;
	if (m_current >= static_cast<int>(m_blocks.size()) || m_current < 0
		|| m_blocks.empty())
		m_current = nextNonTrivial(-1, +1);
	else if (m_blocks[m_current].trivial)
		m_current = nextNonTrivial(m_current, +1);
	applyHighlights();
	updateStatus();
}

void FileCompareView::copyCurrentDiff(int sourceSide)
{
	if (m_diffStale)
		recompare();
	if (m_current < 0 || m_current >= static_cast<int>(m_blocks.size()))
		return;
	const Block block = m_blocks[m_current];
	const int target = 1 - sourceSide;

	// Collect the source block's lines
	QStringList newLines;
	if (block.end[sourceSide] >= block.begin[sourceSide])
	{
		QTextDocument *doc = m_panes[sourceSide]->document();
		for (int line = block.begin[sourceSide]; line <= block.end[sourceSide]; ++line)
			newLines.append(doc->findBlockByNumber(line).text());
	}

	spliceLines(target, block.begin[target], block.end[target], newLines);
	setSideModified(target, true);

	recompare();
	// land on the difference that now occupies this position (or the next one)
	int next = -1;
	for (int b = 0; b < static_cast<int>(m_blocks.size()); ++b)
	{
		if (!m_blocks[b].trivial && m_blocks[b].begin[sourceSide] >= block.begin[sourceSide])
		{
			next = b;
			break;
		}
	}
	if (next < 0)
		next = nextNonTrivial(static_cast<int>(m_blocks.size()), -1);
	if (next >= 0)
		gotoDiff(next);
}

/** Replace lines [firstLine..lastLine] (inclusive; last < first inserts at
    firstLine) with newLines, as a single undoable edit. */
void FileCompareView::spliceLines(int side, int firstLine, int lastLine,
	const QStringList &newLines)
{
	QTextDocument *doc = m_panes[side]->document();
	const int blockCount = doc->blockCount();
	const bool removing = lastLine >= firstLine;

	QTextCursor cursor(doc);
	cursor.beginEditBlock();
	m_syncing = true; // suppress per-keystroke stale marking; we recompare below

	if (removing)
	{
		const QTextBlock firstBlock = doc->findBlockByNumber(firstLine);
		cursor.setPosition(firstBlock.position());
		if (lastLine + 1 < blockCount)
		{
			cursor.setPosition(doc->findBlockByNumber(lastLine + 1).position(),
				QTextCursor::KeepAnchor);
		}
		else
		{
			cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
			// also remove the newline preceding the removed tail block
			if (firstLine > 0 && newLines.isEmpty())
			{
				const int prevEnd = doc->findBlockByNumber(firstLine - 1).position()
					+ doc->findBlockByNumber(firstLine - 1).length() - 1;
				cursor.setPosition(prevEnd);
				cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
			}
		}
		cursor.removeSelectedText();
		if (!newLines.isEmpty())
		{
			const bool atEnd = lastLine + 1 >= blockCount;
			cursor.insertText(newLines.join(QChar('\n')) + (atEnd ? QString() : QStringLiteral("\n")));
		}
	}
	else if (!newLines.isEmpty())
	{
		// pure insertion before firstLine (or append at the end)
		if (firstLine < blockCount)
		{
			cursor.setPosition(doc->findBlockByNumber(firstLine).position());
			cursor.insertText(newLines.join(QChar('\n')) + QStringLiteral("\n"));
		}
		else
		{
			cursor.movePosition(QTextCursor::End);
			cursor.insertText(QStringLiteral("\n") + newLines.join(QChar('\n')));
		}
	}

	m_syncing = false;
	cursor.endEditBlock();
}

bool FileCompareView::isModified() const
{
	return m_sides[0].modified || m_sides[1].modified;
}

void FileCompareView::setSideModified(int side, bool modified)
{
	const bool was = isModified();
	m_sides[side].modified = modified;
	if (m_actSave != nullptr)
		m_actSave->setEnabled(isModified());
	updateStatus();
	if (was != isModified())
		emit modifiedChanged(isModified());
}

bool FileCompareView::saveModified(QString *error)
{
	for (int side = 0; side < 2; ++side)
	{
		if (m_sides[side].modified && !saveSide(side, error))
			return false;
	}
	return true;
}

bool FileCompareView::saveSide(int side, QString *error)
{
	const Side &s = m_sides[side];
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

	const QStringList lines = m_panes[side]->toPlainText().split(QChar('\n'));
	const std::string eol = s.eol.toStdString();
	for (int i = 0; i < lines.size(); ++i)
	{
		file.WriteString(lines.at(i).toStdString());
		if (i + 1 < lines.size() || s.hadFinalEol)
			file.WriteString(eol);
	}
	file.Close();
	setSideModified(side, false);
	return true;
}

void FileCompareView::syncScroll(int pane, int value)
{
	if (m_syncing)
		return;
	m_syncing = true;
	m_panes[1 - pane]->verticalScrollBar()->setValue(value);
	m_syncing = false;
}
