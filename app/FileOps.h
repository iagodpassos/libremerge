// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>

namespace lm
{

/** Copy a file or directory tree; an existing destination file is moved
    to the Trash first (recoverable overwrite). */
bool copyRecursively(const QString &src, const QString &dst);

/** The modification time a file had before a save, when the "Preserve
    file time" option (WinMerge's OPT_PRESERVE_FILETIMES) is on; an
    invalid time otherwise, or when the file does not exist yet. */
QDateTime timeToPreserve(const QString &path);
/** Put a saved file's modification time back to the one from
    timeToPreserve; an invalid time leaves the file alone. */
void restoreFileTime(const QString &path, const QDateTime &time);

/** A file's modification time and size as a comparison found them when
    it loaded or last saved the file (WinMerge's DiffFileInfo, kept in
    m_pRescanFileInfo and m_pSaveFileInfo). */
struct FileStamp
{
	QDateTime modified;
	qint64 size = -1;

	bool operator==(const FileStamp &other) const
	{
		return modified == other.modified && size == other.size;
	}
	bool operator!=(const FileStamp &other) const { return !(*this == other); }
};
FileStamp fileStamp(const QString &path);

enum class FileChange
{
	NoChange,
	Changed, ///< another time or size than the stamp's
	Removed, ///< no such file (an untitled pane's empty path included)
};
/** WinMerge's IsFileChangedOnDisk: what happened to a file since its
    stamp was taken. */
FileChange fileChangedOnDisk(const QString &path, const FileStamp &stamp);

} // namespace lm
