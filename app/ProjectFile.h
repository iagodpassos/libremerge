// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <QChar>
#include <QList>
#include <QString>
#include <QStringList>

namespace lm
{

/**
 * One comparison of a project file, WinMerge's ProjectFileItem: its
 * paths, descriptions and read-only sides, and what it compares them
 * with. Each value is there or not, as the file gives it; a value that is
 * not there leaves the one in use alone. Upstream's plugins (unpacker,
 * prediffer) are left out: LibreMerge runs none.
 */
struct ProjectFileItem
{
	bool hasLeft = false;
	bool hasMiddle = false;
	bool hasRight = false;
	QString left;
	QString middle;
	QString right;
	std::optional<QString> leftDesc;
	std::optional<QString> middleDesc;
	std::optional<QString> rightDesc;
	std::optional<QString> filter;
	std::optional<int> subfolders;
	bool leftReadOnly = false;
	bool middleReadOnly = false;
	bool rightReadOnly = false;
	/** 1 text, 2 table, 3 binary, 4 image, 5 web page (the order of
	    upstream's ID_MERGE_COMPARE_*). */
	std::optional<int> windowType;
	std::optional<QChar> tableDelimiter;
	std::optional<QChar> tableQuote;
	std::optional<bool> tableAllowNewLinesInQuotes;
	std::optional<int> ignoreWhite;
	std::optional<bool> ignoreBlankLines;
	std::optional<bool> ignoreCase;
	std::optional<bool> ignoreEol;
	std::optional<bool> ignoreNumbers;
	std::optional<bool> ignoreCodepage;
	std::optional<bool> ignoreMissingTrailingEol;
	std::optional<bool> ignoreLineBreaks;
	std::optional<bool> filterCommentsLines;
	std::optional<int> compareMethod;
	QStringList hiddenItems;

	// what saving writes (upstream's m_bSave*: Options > Project's Save
	// column, the compare options all together)
	bool saveFilter = true;
	bool saveSubfolders = true;
	bool saveCompareOptions = true;
	bool saveHiddenItems = true;

	/** The paths in the order of the panes, as upstream's PathContext
	    holds them: a middle path makes three, a right one two. */
	QStringList paths() const;
};

/** WinMerge's ProjectFile: the comparisons of a .WinMerge file. */
class ProjectFile
{
public:
	/** Read the file (ProjectFile::Read); false with the parser's
	    message when it is no project file. */
	bool read(const QString &path, QString *error);
	/** Write it (ProjectFile::Save), UTF-8 XML as upstream writes it. */
	bool save(const QString &path, QString *error) const;

	QList<ProjectFileItem> items;

	/** PROJECTFILE_EXT, without its dot. */
	static QString extension() { return QStringLiteral("WinMerge"); }
	/** CMergeApp::IsProjectFile: by the extension, in any case. */
	static bool isProjectFile(const QString &path);
	/** A path of a project file as LoadAndOpenProjectFile takes it: the
	    %VAR% environment variables expanded, a relative path made one in
	    the project file's folder. */
	static QString resolvePath(const QString &path, const QString &projectFile);
};

} // namespace lm
