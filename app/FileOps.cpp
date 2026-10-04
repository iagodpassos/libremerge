// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileOps.h"
#include "OptionsDialog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace lm
{

bool copyRecursively(const QString &src, const QString &dst)
{
	const QFileInfo info(src);
	if (info.isDir())
	{
		if (!QDir().mkpath(dst))
			return false;
		const QDir dir(src);
		const QFileInfoList entries = dir.entryInfoList(
			QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
		for (const QFileInfo &entry : entries)
		{
			if (!copyRecursively(entry.filePath(), dst + QLatin1Char('/') + entry.fileName()))
				return false;
		}
		return true;
	}
	if (QFile::exists(dst))
		QFile::moveToTrash(dst); // overwrite goes through the trash, recoverable
	if (!QDir().mkpath(QFileInfo(dst).path()))
		return false;
	return QFile::copy(src, dst);
}

QDateTime timeToPreserve(const QString &path)
{
	const QFileInfo info(path);
	return OptionsDialog::preserveFileTime() && info.exists()
		? info.lastModified() : QDateTime();
}

void restoreFileTime(const QString &path, const QDateTime &time)
{
	if (!time.isValid())
		return;
	// like CMergeDoc::DoSave, a failure here does not fail the save
	QFile file(path);
	if (file.open(QIODevice::Append))
		file.setFileTime(time, QFileDevice::FileModificationTime);
}

FileStamp fileStamp(const QString &path)
{
	FileStamp stamp;
	const QFileInfo info(path);
	if (!path.isEmpty() && info.exists())
	{
		stamp.modified = info.lastModified();
		stamp.size = info.size();
	}
	return stamp;
}

FileChange fileChangedOnDisk(const QString &path, const FileStamp &stamp)
{
	// "we assume file existed, so disappearing means removal"
	const QFileInfo info(path);
	if (path.isEmpty() || !info.exists())
		return FileChange::Removed;
	// any time difference counts: WinMerge's two-second tolerance comes
	// with OPT_IGNORE_SMALL_FILETIME, off by default and not offered here
	if (info.lastModified() != stamp.modified || info.size() != stamp.size)
		return FileChange::Changed;
	return FileChange::NoChange;
}

} // namespace lm
