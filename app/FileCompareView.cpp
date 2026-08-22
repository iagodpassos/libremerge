// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "FileCompareView.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStringList>
#include <QTextBlock>
#include <QVBoxLayout>

// engine
#include "DiffWrapper.h"
#include "DiffList.h"
#include "PathContext.h"
#include "UniFile.h"
#include "unicoder.h"

namespace
{

/** Read a text file with the engine's encoding handling (BOM, codepage
    detection input) into UTF-8 lines. */
bool readLines(const QString &path, QStringList *lines, QString *error)
{
	UniMemFile file;
	const String enginePath = path.toStdString();
	if (!file.OpenReadOnly(enginePath))
	{
		if (error != nullptr)
			*error = QObject::tr("cannot open %1").arg(path);
		return false;
	}
	file.ReadBom();
	String line, eol;
	bool lossy = false;
	while (file.ReadString(line, eol, &lossy))
		lines->append(QString::fromUtf8(line.data(), static_cast<int>(line.size())));
	file.Close();
	return true;
}

const QColor kDiffColor(255, 243, 176);        // changed lines
const QColor kTrivialColor(230, 230, 230);     // whitespace-only/trivial
const QColor kMissingColor(224, 236, 255);     // block exists only on other side

} // namespace

FileCompareView::FileCompareView(QWidget *parent)
	: QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto *panes = new QHBoxLayout;
	panes->setContentsMargins(0, 0, 0, 0);
	panes->setSpacing(1);
	const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	for (int i = 0; i < 2; ++i)
	{
		m_panes[i] = new QPlainTextEdit(this);
		m_panes[i]->setReadOnly(true);
		m_panes[i]->setLineWrapMode(QPlainTextEdit::NoWrap);
		m_panes[i]->setFont(mono);
		panes->addWidget(m_panes[i]);
		connect(m_panes[i]->verticalScrollBar(), &QScrollBar::valueChanged,
			this, [this, i](int value) { syncScroll(i, value); });
	}
	layout->addLayout(panes, 1);

	m_status = new QLabel(this);
	m_status->setContentsMargins(6, 3, 6, 3);
	layout->addWidget(m_status);
}

bool FileCompareView::compare(const QString &leftPath, const QString &rightPath, QString *error)
{
	// Run the engine diff
	CDiffWrapper wrapper;
	DIFFOPTIONS options{};
	DiffList diffList;
	wrapper.SetCreateDiffList(&diffList);
	wrapper.SetPaths({ leftPath.toStdString(), rightPath.toStdString() }, false);
	wrapper.SetOptions(&options);
	if (!wrapper.RunFileDiff())
	{
		if (error != nullptr)
			*error = tr("the diff engine failed on these files");
		return false;
	}

	QStringList lines[2];
	if (!readLines(leftPath, &lines[0], error) || !readLines(rightPath, &lines[1], error))
		return false;
	m_panes[0]->setPlainText(lines[0].join(QChar('\n')));
	m_panes[1]->setPlainText(lines[1].join(QChar('\n')));

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
	applyHighlights();

	m_status->setText(m_diffCount == 0
		? tr("Files are identical")
		: tr("%n difference(s)", nullptr, m_diffCount));
	return true;
}

void FileCompareView::applyHighlights()
{
	for (int side = 0; side < 2; ++side)
	{
		QList<QTextEdit::ExtraSelection> selections;
		QTextDocument *doc = m_panes[side]->document();
		for (const Block &block : m_blocks)
		{
			const bool empty = block.end[side] < block.begin[side];
			QColor color = block.trivial ? kTrivialColor : kDiffColor;
			int first = block.begin[side];
			int last = empty ? block.begin[side] - 1 : block.end[side];
			if (empty)
			{
				// mark the line where the other side's block would insert
				color = kMissingColor;
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
		m_panes[side]->setExtraSelections(selections);
	}
}

void FileCompareView::syncScroll(int pane, int value)
{
	if (m_syncing)
		return;
	m_syncing = true;
	m_panes[1 - pane]->verticalScrollBar()->setValue(value);
	m_syncing = false;
}
