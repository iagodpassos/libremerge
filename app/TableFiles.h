// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <QChar>
#include <QList>
#include <QString>
#include <QStringList>

// the engine's (TableProps.h)
struct TableProps;

namespace lm
{

/** How a file reads as a table (WinMerge's TableProps): whether it is one,
    the character between its cells, the one around a cell, and whether a
    cell between those can run over several lines. */
struct TableProps
{
	bool isTable = false;
	QChar delimiter;
	QChar quote = QLatin1Char('"');
	bool allowNewlinesInQuotes = true;
};

/** WinMerge's Options > Compare > Table (PropCompareTable), with its
    defaults (OptionsInit): the file patterns of the CSV, TSV and custom
    delimiter-separated groups, the delimiters of the first and the last
    (a TSV file's is the tab), and the quote and newline settings every
    table shares. */
struct TableSettings
{
	QString csvFilePatterns = QStringLiteral("*.csv");
	QString csvDelimiter = QStringLiteral(",");
	QString tsvFilePatterns = QStringLiteral("*.tsv");
	QString dsvFilePatterns;
	QString dsvDelimiter = QStringLiteral(";");
	bool allowNewlinesInQuotes = true;
	QString quote = QStringLiteral("\"");

	static TableSettings load();
	void save() const;
};

/** WinMerge's MakeTablePropertiesByFileName: the first group whose
    patterns (a mask or an expression) take the path makes it a table,
    CSV before TSV before the custom group. */
TableProps tablePropertiesByFileName(const QString &path);

/** CMergeDoc::SetTableProperties over the files of a table comparison: a
    side whose file is no table takes the settings of the last side that
    is one. When none is, as when a table comparison is asked for anyway,
    each side takes what MakeTablePropertiesByFileName gives a table
    asked for: the custom group's delimiter. */
QList<TableProps> tablePropertiesOfSides(const QStringList &paths);

/** strutils::from_charstr: the character a delimiter or quote field
    names, by itself or as an escape ("\t", "tab", "0x3b"...); none (a
    null QChar) for an empty field. */
QChar characterOfField(const QString &text);

/** WildcardRemoveDuplicatePatterns: a pattern that a later one of the
    list takes in goes. */
QString removeDuplicatePatterns(const QString &patterns);

/** The engine's TableProps for a side, for the line expressions of the
    prediffer: the delimiter and the quote are one byte there (upstream's
    tchar_t is UTF-16, the port's a byte of UTF-8), so a file whose
    delimiter or quote is past ASCII is no table to the engine. */
::TableProps engineTableProps(const TableProps &props);

/** MakeTablePropertiesByFileName for the engine (the ports layer's hook):
    tablePropertiesByFileName, as engineTableProps gives it. */
::TableProps engineTablePropertiesByFileName(const std::string &path);

} // namespace lm
