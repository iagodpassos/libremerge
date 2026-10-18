// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "SyntaxHighlighter.h"

#include <QFileInfo>
#include <QTextBlock>
#include <QTextDocument>
#include <string>
#include <vector>

#include "ISyntaxParser.h"
#include "ITextBuffer.h"
#include "OptionsDef.h"
#include "OptionsMgr.h"
#include "SyntaxColors.h"
#include "SyntaxParserRegistry.h"
#include "TextDefinition.h"
#include "Theme.h"
#include "TreeSitterParser.h"

namespace
{

/** Build the byte→UTF-16 offset map for one line. */
std::vector<int> byteToU16Map(const QString &text, int byteLength)
{
	std::vector<int> map(byteLength + 1, text.size());
	int bytePos = 0;
	for (int i = 0; i < text.size();)
	{
		const QChar ch = text.at(i);
		int u16len = 1;
		char32_t cp = ch.unicode();
		if (ch.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate())
		{
			cp = QChar::surrogateToUcs4(ch, text.at(i + 1));
			u16len = 2;
		}
		const int u8len = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
		for (int b = 0; b < u8len && bytePos + b <= byteLength; ++b)
			map[bytePos + b] = i;
		bytePos += u8len;
		i += u16len;
	}
	return map;
}

} // namespace

/** The document's lines as the engine's parsers read them (UTF-8), taken
    when it is parsed again. */
class DocumentTextBuffer : public LangServices::ITextBuffer
{
public:
	void load(const QTextDocument *document)
	{
		m_lines.clear();
		m_lines.reserve(static_cast<size_t>(document->blockCount()));
		for (QTextBlock block = document->begin(); block.isValid(); block = block.next())
			m_lines.push_back(block.text().toStdString());
	}
	/** Whether the blocks from one position to another read as when they
	    were taken (a reformat marks blocks dirty without changing them). */
	bool same(const QTextDocument *document, int from, int to) const
	{
		if (document->blockCount() != GetLineCount())
			return false;
		const QTextBlock last = document->findBlock(to);
		for (QTextBlock block = document->findBlock(from); block.isValid(); block = block.next())
		{
			if (block.text().toStdString() != m_lines[static_cast<size_t>(block.blockNumber())])
				return false;
			if (block == last)
				break;
		}
		return true;
	}

	int GetLineCount() const override { return static_cast<int>(m_lines.size()); }
	const tchar_t *GetLineChars(int line) const override { return m_lines[static_cast<size_t>(line)].c_str(); }
	int GetLineLength(int line) const override
	{
		return static_cast<int>(m_lines[static_cast<size_t>(line)].size());
	}
	int GetFullLineLength(int line) const override
	{
		return GetLineLength(line) + (line + 1 < GetLineCount() ? 1 : 0);
	}

private:
	std::vector<std::string> m_lines;
};

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document, const QString &filePath)
	// the document comes last (setDocument below): a parser of the whole
	// document then hears of an edit before the highlighting does
	: QSyntaxHighlighter(static_cast<QObject *>(document))
{
	const std::string ext = QFileInfo(filePath).suffix().toLower().toStdString();
	LangServices::TextDefinition *def = ext.empty() ? nullptr : LangServices::GetTextType(ext.c_str());
	// WinMerge's "Highlight syntax" off: plain text (SRC_PLAIN)
	const COptionsMgr *options = GetOptionsMgr();
	if (options != nullptr && !options->GetBool(OPT_SYNTAX_HIGHLIGHT))
		def = nullptr;
	if (def != nullptr && def->type != LangServices::LanguageId::SRC_PLAIN)
	{
		// the parser WinMerge's view takes, the registry's for the
		// language; a built-in one runs here line by line
		auto parser = LangServices::SyntaxParserRegistry::GetInstance().CreateParser(def->type);
		if (std::dynamic_pointer_cast<CTreeSitterParser>(parser))
		{
			m_buffer = std::make_unique<DocumentTextBuffer>();
			m_parser = parser;
			m_parser->SetTextBuffer(m_buffer.get());
			connect(document, &QTextDocument::contentsChange, this, &SyntaxHighlighter::documentChanged);
			// an edit can change the colors of lines far from it (a string
			// opened): all of them, once the typing stops
			m_rehighlightTimer.setSingleShot(true);
			m_rehighlightTimer.setInterval(250);
			connect(&m_rehighlightTimer, &QTimer::timeout, this, [this]() {
				m_rehighlighting = true;
				rehighlight();
				m_rehighlighting = false;
			});
		}
		else if (parser)
			m_parse = GetParseFunc(def->type);
		if (hasLanguage())
			m_languageName = QString::fromUtf8(def->name);
	}
	setDocument(document);
	if (!hasLanguage())
		return;

	// foreground only (diff colors own the background); classic light
	// palette or a dark-editor equivalent, per the active theme
	auto set = [this](int index, const QColor &color, bool bold = false) {
		if (index < 0 || index >= kFormatCount)
			return;
		m_formats[index].setForeground(color);
		if (bold)
			m_formats[index].setFontWeight(QFont::DemiBold);
	};
	if (lm::Theme::instance()->dark())
	{
		set(COLORINDEX_KEYWORD, QColor(0x56, 0x9c, 0xd6), true);
		set(COLORINDEX_FUNCNAME, QColor(0xdc, 0xdc, 0xaa));
		set(COLORINDEX_COMMENT, QColor(0x6a, 0x99, 0x55));
		set(COLORINDEX_NUMBER, QColor(0xb5, 0xce, 0xa8));
		set(COLORINDEX_OPERATOR, QColor(0xc8, 0xc8, 0xc8));
		set(COLORINDEX_STRING, QColor(0xce, 0x91, 0x78));
		set(COLORINDEX_PREPROCESSOR, QColor(0x4e, 0xc9, 0xb0));
		set(COLORINDEX_USER1, QColor(0x4f, 0xc1, 0xff));
		set(COLORINDEX_USER2, QColor(0xd7, 0xba, 0x7d));
	}
	else
	{
		set(COLORINDEX_KEYWORD, QColor(0x00, 0x00, 0xbf), true);
		set(COLORINDEX_FUNCNAME, QColor(0x6f, 0x00, 0x8a));
		set(COLORINDEX_COMMENT, QColor(0x00, 0x80, 0x00));
		set(COLORINDEX_NUMBER, QColor(0xa8, 0x40, 0x00));
		set(COLORINDEX_OPERATOR, QColor(0x40, 0x40, 0x40));
		set(COLORINDEX_STRING, QColor(0xa3, 0x15, 0x15));
		set(COLORINDEX_PREPROCESSOR, QColor(0x00, 0x60, 0x70));
		set(COLORINDEX_USER1, QColor(0x00, 0x70, 0x70));
		set(COLORINDEX_USER2, QColor(0x70, 0x40, 0x00));
	}
}

SyntaxHighlighter::~SyntaxHighlighter()
{
	// QSyntaxHighlighter's destructor takes the colors off the document,
	// which tells of it: not to this part of the object, gone by then
	if (QTextDocument *doc = document())
		disconnect(doc, &QTextDocument::contentsChange, this, &SyntaxHighlighter::documentChanged);
	m_rehighlightTimer.stop();
}

void SyntaxHighlighter::documentChanged(int position, int removed, int added)
{
	(void)removed;
	if (m_rehighlighting)
		return;
	if (!m_stale && m_buffer->same(document(), position, position + added))
		return;
	m_stale = true;
	m_rehighlightTimer.start();
}

void SyntaxHighlighter::highlightBlock(const QString &text)
{
	if (m_parser != nullptr)
	{
		// parsed again once after a change (CTreeSitterParser's lazy reparse)
		if (m_stale)
		{
			m_buffer->load(document());
			m_parser->Invalidate();
			m_stale = false;
		}
		const int line = currentBlock().blockNumber();
		if (line >= 0 && line < m_buffer->GetLineCount())
			applyBlocks(text, text.toUtf8(), m_parser->ParseLine(line));
		return;
	}
	if (m_parse == nullptr)
		return;

	const int previous = previousBlockState();
	const unsigned cookieIn = previous < 0 ? 0 : static_cast<unsigned>(previous);

	const QByteArray utf8 = text.toUtf8();
	std::vector<LangServices::TEXTBLOCK> blocks;
	const unsigned cookieOut = m_parse(cookieIn, utf8.constData(),
		static_cast<int>(utf8.size()), &blocks);
	setCurrentBlockState(static_cast<int>(cookieOut));
	applyBlocks(text, utf8, blocks);
}

void SyntaxHighlighter::applyBlocks(const QString &text, const QByteArray &utf8,
	const std::vector<LangServices::TEXTBLOCK> &blocks)
{
	if (blocks.empty())
		return;
	const std::vector<int> map = byteToU16Map(text, static_cast<int>(utf8.size()));
	for (size_t k = 0; k < blocks.size(); ++k)
	{
		const int colorIndex = blocks[k].m_nColorIndex & ~0xC0000000;
		if (colorIndex <= 0 || colorIndex >= kFormatCount)
			continue;
		const QTextCharFormat &format = m_formats[colorIndex];
		if (format.foreground().style() == Qt::NoBrush)
			continue;
		const int startByte = qBound(0, blocks[k].m_nCharPos, static_cast<int>(utf8.size()));
		const int endByte = k + 1 < blocks.size()
			? qBound(startByte, blocks[k + 1].m_nCharPos, static_cast<int>(utf8.size()))
			: static_cast<int>(utf8.size());
		const int start = map[startByte];
		const int end = map[endByte];
		if (end > start)
			setFormat(start, end - start, format);
	}
}
