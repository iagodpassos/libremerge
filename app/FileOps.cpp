// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileOps.h"

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

} // namespace lm
