// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "TableFiles.h"

#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

#include "FileFilters.h"

// engine
#include "TableProps.h"
#include "UnicodeString.h"

namespace lm
{

namespace
{
// WinMerge's option names (OptionsDef.h), under a group of their own
const QString kCsvFilePatterns = QStringLiteral("TableCompare/CSVFilePatterns");
const QString kCsvDelimiter = QStringLiteral("TableCompare/CSVDelimiterCharacter");
const QString kTsvFilePatterns = QStringLiteral("TableCompare/TSVFilePatterns");
const QString kDsvFilePatterns = QStringLiteral("TableCompare/DSVFilePatterns");
const QString kDsvDelimiter = QStringLiteral("TableCompare/DSVDelimiterCharacter");
const QString kAllowNewlines = QStringLiteral("TableCompare/TableAllowNewlinesInQuotes");
const QString kQuote = QStringLiteral("TableCompare/TableQuoteCharacter");
} // namespace

TableSettings TableSettings::load()
{
	const TableSettings defaults;
	QSettings settings;
	TableSettings loaded;
	loaded.csvFilePatterns = settings.value(kCsvFilePatterns, defaults.csvFilePatterns).toString();
	loaded.csvDelimiter = settings.value(kCsvDelimiter, defaults.csvDelimiter).toString();
	loaded.tsvFilePatterns = settings.value(kTsvFilePatterns, defaults.tsvFilePatterns).toString();
	loaded.dsvFilePatterns = settings.value(kDsvFilePatterns, defaults.dsvFilePatterns).toString();
	loaded.dsvDelimiter = settings.value(kDsvDelimiter, defaults.dsvDelimiter).toString();
	loaded.allowNewlinesInQuotes =
		settings.value(kAllowNewlines, defaults.allowNewlinesInQuotes).toBool();
	loaded.quote = settings.value(kQuote, defaults.quote).toString();
	return loaded;
}

void TableSettings::save() const
{
	QSettings settings;
	settings.setValue(kCsvFilePatterns, csvFilePatterns);
	settings.setValue(kCsvDelimiter, csvDelimiter);
	settings.setValue(kTsvFilePatterns, tsvFilePatterns);
	settings.setValue(kDsvFilePatterns, dsvFilePatterns);
	settings.setValue(kDsvDelimiter, dsvDelimiter);
	settings.setValue(kAllowNewlines, allowNewlinesInQuotes);
	settings.setValue(kQuote, quote);
}

TableProps tablePropertiesByFileName(const QString &path)
{
	const TableSettings settings = TableSettings::load();
	TableProps props;
	props.quote = characterOfField(settings.quote);
	props.allowNewlinesInQuotes = settings.allowNewlinesInQuotes;
	if (filePatternsTake(settings.csvFilePatterns, path))
	{
		props.isTable = true;
		props.delimiter = characterOfField(settings.csvDelimiter);
	}
	else if (filePatternsTake(settings.tsvFilePatterns, path))
	{
		props.isTable = true;
		props.delimiter = QLatin1Char('\t');
	}
	else if (filePatternsTake(settings.dsvFilePatterns, path))
	{
		props.isTable = true;
		props.delimiter = characterOfField(settings.dsvDelimiter);
	}
	return props;
}

QList<TableProps> tablePropertiesOfSides(const QStringList &paths)
{
	QList<TableProps> props;
	int tableSide = -1;
	for (const QString &path : paths)
	{
		props.append(tablePropertiesByFileName(path));
		if (props.last().isTable)
			tableSide = static_cast<int>(props.size()) - 1;
	}
	if (tableSide < 0)
	{
		const QChar delimiter = characterOfField(TableSettings::load().dsvDelimiter);
		for (TableProps &side : props)
		{
			side.isTable = true;
			side.delimiter = delimiter;
		}
		return props;
	}
	for (TableProps &side : props)
		if (!side.isTable)
			side = props.at(tableSide);
	return props;
}

::TableProps engineTableProps(const TableProps &props)
{
	bool fits = true;
	const auto byte = [&fits](QChar ch) -> char {
		if (ch.unicode() < 0x80)
			return static_cast<char>(ch.unicode());
		fits = false;
		return 0;
	};
	::TableProps engine{};
	engine.delimiter = byte(props.delimiter);
	engine.quote = byte(props.quote);
	engine.istable = props.isTable && fits;
	engine.allowNewlinesInQuotes = props.allowNewlinesInQuotes;
	return engine;
}

::TableProps engineTablePropertiesByFileName(const std::string &path)
{
	return engineTableProps(tablePropertiesByFileName(QString::fromStdString(path)));
}

QChar characterOfField(const QString &text)
{
	if (text.isEmpty())
		return QChar();
	// the escapes and names are ASCII; otherwise the field's first
	// character, a whole one (upstream's tchar_t is UTF-16, the port's a
	// byte of UTF-8)
	const char ch = strutils::from_charstr(text.toStdString());
	if (static_cast<unsigned char>(ch) < 0x80)
		return QChar(static_cast<ushort>(static_cast<unsigned char>(ch)));
	return text.at(0);
}

QString removeDuplicatePatterns(const QString &patterns)
{
	static const QRegularExpression separators(QStringLiteral("[; ]"));
	// PathMatchSpec: a name against a list of wildcards, without case
	const auto pathMatchSpec = [](const QString &name, const QString &specs) {
		for (const QString &spec : specs.split(separators, Qt::SkipEmptyParts))
		{
			const QRegularExpression wildcard(QRegularExpression::anchoredPattern(
				QRegularExpression::wildcardToRegularExpression(spec,
					QRegularExpression::UnanchoredWildcardConversion)),
				QRegularExpression::CaseInsensitiveOption);
			if (wildcard.match(name).hasMatch())
				return true;
		}
		return false;
	};
	// upstream walks the list once: a pattern the rest of the list takes
	// in goes with the separator after it; the last one always stays
	QString list = patterns;
	qsizetype i = 0;
	for (;;)
	{
		const qsizetype j = list.indexOf(separators, i);
		if (j < 0 || j + 1 == list.size())
			break;
		const qsizetype k = j + 1;
		if (pathMatchSpec(list.mid(i, j - i), list.mid(k)))
			list.remove(i, k - i);
		else
			i = k;
	}
	return list;
}

} // namespace lm
