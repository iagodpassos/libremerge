// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "SyntaxHighlighter.h"

#include <QFileInfo>
#include <vector>

#include "SyntaxColors.h"
#include "TextDefinition.h"
#include "Theme.h"

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

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document, const QString &filePath)
	: QSyntaxHighlighter(document)
{
	const std::string ext = QFileInfo(filePath).suffix().toLower().toStdString();
	if (ext.empty())
		return;
	LangServices::TextDefinition *def = LangServices::GetTextType(ext.c_str());
	if (def == nullptr || def->type == LangServices::LanguageId::SRC_PLAIN)
		return;
	m_parse = GetParseFunc(def->type);
	if (m_parse == nullptr)
		return;
	m_languageName = QString::fromUtf8(def->name);

	// foreground only (diff colors own the background); classic light
	// palette or a dark-editor equivalent, per the active theme
	auto set = [this](int index, const QColor &color, bool bold = false) {
		if (index < 0 || index >= 16)
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

void SyntaxHighlighter::highlightBlock(const QString &text)
{
	if (m_parse == nullptr)
		return;

	const int previous = previousBlockState();
	const unsigned cookieIn = previous < 0 ? 0 : static_cast<unsigned>(previous);

	const QByteArray utf8 = text.toUtf8();
	std::vector<LangServices::TEXTBLOCK> blocks;
	const unsigned cookieOut = m_parse(cookieIn, utf8.constData(),
		static_cast<int>(utf8.size()), &blocks);
	setCurrentBlockState(static_cast<int>(cookieOut));

	if (blocks.empty())
		return;
	const std::vector<int> map = byteToU16Map(text, static_cast<int>(utf8.size()));
	for (size_t k = 0; k < blocks.size(); ++k)
	{
		const int colorIndex = blocks[k].m_nColorIndex & ~0xC0000000;
		if (colorIndex <= 0 || colorIndex >= 16)
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
