// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settings.h"

#include <QHash>
#include <QSettings>

namespace lm
{

namespace
{

/** WinMerge's names of the options of the Options pages LibreMerge
    keeps under keys of its own: General, Backup Files, and the patterns
    and settings of Compare > Table, Binary and Image. */
struct Mapped
{
	const char *winMerge;
	const char *key;
};
const Mapped kMapped[] = {
	{ "Settings/ScrollToFirst", "General/ScrollToFirstDiff" },
	{ "Settings/ScrollToFirstInlineDiff", "General/ScrollToFirstInlineDiff" },
	{ "Settings/ShowFileDialog", "General/ShowSelectorAtStartup" },
	{ "Settings/AskClosingMultipleWindows", "General/AskCloseMultipleTabs" },
	{ "Settings/CloseWithEsc", "General/CloseWithEsc" },
	{ "Settings/VerifyOpenPaths", "General/VerifyOpenPaths" },
	{ "Settings/PreserveFiletimes", "General/PreserveFileTime" },
	{ "Settings/CloseWithOK", "General/CloseSelectorOnCompare" },
	{ "Settings/AutoCompleteSource", "General/AutoCompleteSource" },
	{ "Settings/AutoReloadModifiedFiles", "General/AutoReloadModifiedFiles" },
	{ "Backup/EnableFile", "Backup/FileCompare" },
	{ "Settings/CSVFilePatterns", "TableCompare/CSVFilePatterns" },
	{ "Settings/CSVDelimiterCharacter", "TableCompare/CSVDelimiterCharacter" },
	{ "Settings/TSVFilePatterns", "TableCompare/TSVFilePatterns" },
	{ "Settings/DSVFilePatterns", "TableCompare/DSVFilePatterns" },
	{ "Settings/DSVDelimiterCharacter", "TableCompare/DSVDelimiterCharacter" },
	{ "Settings/TableAllowNewlinesInQuotes", "TableCompare/TableAllowNewlinesInQuotes" },
	{ "Settings/TableQuoteCharacter", "TableCompare/TableQuoteCharacter" },
	{ "Settings/BinaryFilePatterns", "BinaryCompare/FilePatterns" },
	{ "Settings/ImageFilePatterns", "ImageCompare/FilePatterns" },
	{ "Settings/EnableImageCompareInFolderCompare", "ImageCompare/EnableInFolderCompare" },
};

QHash<QString, QString> &commandLineSettings()
{
	static QHash<QString, QString> settings;
	return settings;
}

} // namespace

QVariant setting(const QString &key, const QVariant &defaultValue)
{
	const auto found = commandLineSettings().constFind(key);
	if (found != commandLineSettings().constEnd())
		return *found;
	return QSettings().value(key, defaultValue);
}

bool useSetting(const QString &name, const QString &value)
{
	// the name in full, or a part only one name holds
	// (COptionsMgr::ExpandShortName)
	const char *key = nullptr;
	int matched = 0;
	for (const Mapped &mapped : kMapped)
	{
		const QString winMerge = QLatin1String(mapped.winMerge);
		if (winMerge == name)
		{
			key = mapped.key;
			matched = 1;
			break;
		}
		if (winMerge.contains(name))
		{
			key = mapped.key;
			++matched;
		}
	}
	if (matched != 1)
		return false;
	commandLineSettings().insert(QLatin1String(key), value);
	return true;
}

void forgetCommandLineSettings()
{
	commandLineSettings().clear();
}

} // namespace lm
