// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "ProjectFile.h"
#include "Environment.h"

#include <limits>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace lm
{

namespace
{

// upstream's element names (ProjectFile.cpp)
const QLatin1String kRoot("project");
const QLatin1String kPaths("paths");
const QLatin1String kLeft("left");
const QLatin1String kMiddle("middle");
const QLatin1String kRight("right");
const QLatin1String kLeftDesc("left-desc");
const QLatin1String kMiddleDesc("middle-desc");
const QLatin1String kRightDesc("right-desc");
const QLatin1String kFilter("filter");
const QLatin1String kSubfolders("subfolders");
const QLatin1String kLeftReadOnly("left-readonly");
const QLatin1String kMiddleReadOnly("middle-readonly");
const QLatin1String kRightReadOnly("right-readonly");
const QLatin1String kWindowType("window-type");
const QLatin1String kTableDelimiter("table-delimiter");
const QLatin1String kTableQuote("table-quote");
const QLatin1String kTableAllowNewLines("table-allownewlinesinquotes");
const QLatin1String kWhiteSpaces("white-spaces");
const QLatin1String kIgnoreBlankLines("ignore-blank-lines");
const QLatin1String kIgnoreCase("ignore-case");
const QLatin1String kIgnoreCrDiff("ignore-carriage-return-diff");
const QLatin1String kIgnoreNumbers("ignore-numbers");
const QLatin1String kIgnoreCodepageDiff("ignore-codepage-diff");
const QLatin1String kIgnoreCommentDiff("ignore-comment-diff");
const QLatin1String kIgnoreMissingTrailingEol("ignore-missing-trailing-eol");
const QLatin1String kIgnoreLineBreaks("ignore-line-breaks");
const QLatin1String kCompareMethod("compare-method");
const QLatin1String kHiddenList("hidden-list");
const QLatin1String kHiddenItem("hidden-item");

/** C's atoi, which upstream reads the numbers with: the leading digits,
    after blanks and a sign, 0 when there are none. */
int atoi(const QString &text)
{
	int i = 0;
	while (i < text.size() && text.at(i).isSpace())
		++i;
	bool negative = false;
	if (i < text.size() && (text.at(i) == QLatin1Char('-') || text.at(i) == QLatin1Char('+')))
		negative = text.at(i++) == QLatin1Char('-');
	qint64 value = 0;
	for (; i < text.size() && text.at(i) >= QLatin1Char('0') && text.at(i) <= QLatin1Char('9'); ++i)
	{
		value = value * 10 + (text.at(i).unicode() - '0');
		if (value > std::numeric_limits<int>::max())
			value = std::numeric_limits<int>::max();
	}
	return static_cast<int>(negative ? -value : value);
}

/** ProjectFileHandler::characters: a piece of an element's text, which
    the string values gather and the numbers are read from. */
void takeText(ProjectFileItem &item, const QStringView element, const QString &text)
{
	if (element == kLeft)
	{
		item.left += text;
		item.hasLeft = true;
	}
	else if (element == kMiddle)
	{
		item.middle += text;
		item.hasMiddle = true;
	}
	else if (element == kRight)
	{
		item.right += text;
		item.hasRight = true;
	}
	// a description alone still makes its side (an empty path)
	else if (element == kLeftDesc)
	{
		item.leftDesc = item.leftDesc.value_or(QString()) + text;
		item.hasLeft = true;
	}
	else if (element == kMiddleDesc)
	{
		item.middleDesc = item.middleDesc.value_or(QString()) + text;
		item.hasMiddle = true;
	}
	else if (element == kRightDesc)
	{
		item.rightDesc = item.rightDesc.value_or(QString()) + text;
		item.hasRight = true;
	}
	else if (element == kFilter)
		item.filter = item.filter.value_or(QString()) + text;
	else if (element == kSubfolders)
		item.subfolders = atoi(text);
	else if (element == kLeftReadOnly)
		item.leftReadOnly = atoi(text) != 0;
	else if (element == kMiddleReadOnly)
		item.middleReadOnly = atoi(text) != 0;
	else if (element == kRightReadOnly)
		item.rightReadOnly = atoi(text) != 0;
	else if (element == kWindowType)
		item.windowType = atoi(text);
	else if (element == kTableDelimiter)
		item.tableDelimiter = text.at(0);
	else if (element == kTableQuote)
		item.tableQuote = text.at(0);
	else if (element == kTableAllowNewLines)
		item.tableAllowNewLinesInQuotes = atoi(text) != 0;
	else if (element == kWhiteSpaces)
		item.ignoreWhite = atoi(text);
	else if (element == kIgnoreBlankLines)
		item.ignoreBlankLines = atoi(text) != 0;
	else if (element == kIgnoreCase)
		item.ignoreCase = atoi(text) != 0;
	else if (element == kIgnoreCrDiff)
		item.ignoreEol = atoi(text) != 0;
	else if (element == kIgnoreNumbers)
		item.ignoreNumbers = atoi(text) != 0;
	else if (element == kIgnoreCodepageDiff)
		item.ignoreCodepage = atoi(text) != 0;
	else if (element == kIgnoreMissingTrailingEol)
		item.ignoreMissingTrailingEol = atoi(text) != 0;
	else if (element == kIgnoreLineBreaks)
		item.ignoreLineBreaks = atoi(text) != 0;
	else if (element == kIgnoreCommentDiff)
		item.filterCommentsLines = atoi(text) != 0;
	else if (element == kCompareMethod)
		item.compareMethod = atoi(text);
	else if (element == kHiddenItem)
		item.hiddenItems.append(text);
}

QString flag(bool on)
{
	return on ? QStringLiteral("1") : QStringLiteral("0");
}

} // namespace

QStringList ProjectFileItem::paths() const
{
	if (hasMiddle)
		return { left, middle, right };
	if (hasRight)
		return { left, right };
	if (hasLeft)
		return { left };
	return {};
}

bool ProjectFile::isProjectFile(const QString &path)
{
	return QFileInfo(path).suffix().compare(extension(), Qt::CaseInsensitive) == 0;
}

QString ProjectFile::resolvePath(const QString &path, const QString &projectFile)
{
	const QString expanded = QString::fromStdString(
		env::ExpandEnvironmentVariables(path.toStdString()));
	// an absolute path, a URL and no path at all stay as they are
	if (expanded.isEmpty() || QDir::isAbsolutePath(expanded)
		|| expanded.contains(QStringLiteral("://")))
		return expanded;
	return QFileInfo(projectFile).absolutePath() + QLatin1Char('/') + expanded;
}

bool ProjectFile::read(const QString &path, QString *error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		if (error != nullptr)
			*error = file.errorString();
		return false;
	}
	// the text of every element of a comparison is taken, as upstream's
	// SAX handler does; nothing comes before the first <paths>
	QXmlStreamReader xml(&file);
	QList<QString> stack;
	while (!xml.atEnd())
	{
		switch (xml.readNext())
		{
		case QXmlStreamReader::StartElement:
			if (xml.name() == kPaths)
				items.append(ProjectFileItem());
			stack.append(xml.name().toString());
			break;
		case QXmlStreamReader::EndElement:
			if (!stack.isEmpty())
				stack.removeLast();
			break;
		case QXmlStreamReader::Characters:
			if (!items.isEmpty() && !stack.isEmpty() && !xml.text().isEmpty())
				takeText(items.last(), stack.last(), xml.text().toString());
			break;
		default:
			break;
		}
	}
	if (xml.hasError())
	{
		if (error != nullptr)
			*error = QStringLiteral("%1 (%2:%3)").arg(xml.errorString())
				.arg(xml.lineNumber()).arg(xml.columnNumber());
		return false;
	}
	return true;
}

bool ProjectFile::save(const QString &path, QString *error) const
{
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
	{
		if (error != nullptr)
			*error = file.errorString();
		return false;
	}
	// upstream's XMLWriter: the declaration, then one element a line,
	// indented with tabs
	QXmlStreamWriter xml(&file);
	xml.setAutoFormatting(true);
	xml.setAutoFormattingIndent(-1);
	xml.writeStartDocument();
	xml.writeStartElement(kRoot);
	for (const ProjectFileItem &item : items)
	{
		xml.writeStartElement(kPaths);
		const auto write = [&xml](QLatin1String name, const QString &text) {
			xml.writeTextElement(name, text);
		};
		if (!item.left.isEmpty())
			write(kLeft, item.left);
		if (!item.middle.isEmpty())
			write(kMiddle, item.middle);
		if (!item.right.isEmpty())
			write(kRight, item.right);
		if (!item.leftDesc.value_or(QString()).isEmpty())
			write(kLeftDesc, *item.leftDesc);
		if (!item.middleDesc.value_or(QString()).isEmpty())
			write(kMiddleDesc, *item.middleDesc);
		if (!item.rightDesc.value_or(QString()).isEmpty())
			write(kRightDesc, *item.rightDesc);
		if (item.saveFilter && !item.filter.value_or(QString()).isEmpty())
			write(kFilter, *item.filter);
		if (item.saveSubfolders)
			write(kSubfolders, flag(item.subfolders.value_or(-1) != 0));
		write(kLeftReadOnly, flag(item.leftReadOnly));
		if (!item.middle.isEmpty())
			write(kMiddleReadOnly, flag(item.middleReadOnly));
		write(kRightReadOnly, flag(item.rightReadOnly));
		if (item.windowType.value_or(-1) != -1)
			write(kWindowType, QString::number(*item.windowType));
		if (item.windowType.value_or(-1) == 2) // table
		{
			write(kTableDelimiter, QString(item.tableDelimiter.value_or(QLatin1Char(','))));
			write(kTableQuote, QString(item.tableQuote.value_or(QLatin1Char('"'))));
			write(kTableAllowNewLines, flag(item.tableAllowNewLinesInQuotes.value_or(true)));
		}
		if (item.saveCompareOptions)
		{
			write(kWhiteSpaces, QString::number(item.ignoreWhite.value_or(0)));
			write(kIgnoreBlankLines, flag(item.ignoreBlankLines.value_or(false)));
			write(kIgnoreCase, flag(item.ignoreCase.value_or(false)));
			write(kIgnoreCrDiff, flag(item.ignoreEol.value_or(false)));
			write(kIgnoreNumbers, flag(item.ignoreNumbers.value_or(false)));
			write(kIgnoreCodepageDiff, flag(item.ignoreCodepage.value_or(false)));
			write(kIgnoreMissingTrailingEol, flag(item.ignoreMissingTrailingEol.value_or(false)));
			write(kIgnoreLineBreaks, flag(item.ignoreLineBreaks.value_or(false)));
			write(kIgnoreCommentDiff, flag(item.filterCommentsLines.value_or(false)));
			write(kCompareMethod, QString::number(item.compareMethod.value_or(0)));
		}
		if (item.saveHiddenItems && !item.hiddenItems.isEmpty())
		{
			xml.writeStartElement(kHiddenList);
			for (const QString &hidden : item.hiddenItems)
				write(kHiddenItem, hidden);
			xml.writeEndElement();
		}
		xml.writeEndElement();
	}
	xml.writeEndElement();
	xml.writeEndDocument();
	if (xml.hasError() || !file.commit())
	{
		if (error != nullptr)
			*error = file.errorString();
		return false;
	}
	return true;
}

} // namespace lm
