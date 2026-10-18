// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <vector>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTimer>

#include "CrystalLineSyntaxParser.h" // engine: ParseFunc + language lookup

namespace LangServices { struct ISyntaxParser; }
class DocumentTextBuffer;

/**
 * Syntax highlighting by the parser the engine's registry gives for the
 * file's language, as WinMerge's view takes it: a CrystalEdit line parser
 * (47 languages), whose cookie travels through Qt's block state so that
 * multi-line constructs propagate on edits; or, for the languages those
 * lack, Tree-sitter's (WinMerge's "Built-in first"), which parses the whole
 * document and reads each line's colors from the tree.
 */
class SyntaxHighlighter : public QSyntaxHighlighter
{
	Q_OBJECT
public:
	SyntaxHighlighter(QTextDocument *document, const QString &filePath);
	~SyntaxHighlighter() override;

	bool hasLanguage() const { return m_parse != nullptr || m_parser != nullptr; }
	QString languageName() const { return m_languageName; }
	/** Whether Tree-sitter colors the file (for tests). */
	bool treeSitterForTest() const { return m_parser != nullptr; }
	/** The format of a COLORINDEX (for tests). */
	QTextCharFormat formatForTest(int colorIndex) const
	{
		return colorIndex >= 0 && colorIndex < kFormatCount ? m_formats[colorIndex] : QTextCharFormat();
	}

protected:
	void highlightBlock(const QString &text) override;

private:
	void documentChanged(int position, int removed, int added);
	void applyBlocks(const QString &text, const QByteArray &utf8,
		const std::vector<LangServices::TEXTBLOCK> &blocks);

	ParseFunc m_parse = nullptr;
	// the whole-document parser and the lines it reads, which it points to
	// (declared first, so destroyed last)
	std::unique_ptr<DocumentTextBuffer> m_buffer;
	std::shared_ptr<LangServices::ISyntaxParser> m_parser;
	bool m_stale = true;
	bool m_rehighlighting = false;
	QTimer m_rehighlightTimer;
	QString m_languageName;
	// by COLORINDEX: the syntax colors run up to COLORINDEX_USER2 (21)
	static constexpr int kFormatCount = 32;
	QTextCharFormat m_formats[kFormatCount];
};
