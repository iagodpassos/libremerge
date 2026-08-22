// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace lm
{

/** One row of a 2-way folder comparison result. */
struct FolderCompareItem
{
	enum Category
	{
		Identical,
		Different,
		LeftOnly,
		RightOnly,
		Skipped,
		Error,
	};

	QString name;         ///< item filename
	QString folder;       ///< relative folder inside the compared roots
	QString leftPath;     ///< full path on the left side (empty if missing)
	QString rightPath;    ///< full path on the right side (empty if missing)
	qint64 size[2] = {-1, -1};
	QDateTime mtime[2];
	Category category = Identical;
	bool isDir = false;
};

struct FolderCompareResult
{
	QList<FolderCompareItem> items;
	int identical = 0;
	int different = 0;
	int unique = 0;
	bool ok = false;
	QString error;
};

/** Run a synchronous 2-way folder comparison (full content compare)
    with the engine's DirScan machinery. */
FolderCompareResult compareFolders(const QString &leftDir, const QString &rightDir,
	bool recursive);

} // namespace lm
