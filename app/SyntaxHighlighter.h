// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include "CrystalLineSyntaxParser.h" // engine: ParseFunc + language lookup

/**
 * Syntax highlighting over the engine's CrystalEdit line parsers
 * (47 languages). The parser cookie travels through Qt's block state,
 * so multi-line constructs (block comments, strings) propagate and
 * re-highlight automatically on edits.
 */
class SyntaxHighlighter : public QSyntaxHighlighter
{
	Q_OBJECT
public:
	SyntaxHighlighter(QTextDocument *document, const QString &filePath);

	bool hasLanguage() const { return m_parse != nullptr; }
	QString languageName() const { return m_languageName; }

protected:
	void highlightBlock(const QString &text) override;

private:
	ParseFunc m_parse = nullptr;
	QString m_languageName;
	QTextCharFormat m_formats[16];
};
