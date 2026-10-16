// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QStringList>

namespace lm
{

/** WinMerge's default binary file patterns (OPT_CMP_BIN_FILEPATTERNS). */
inline QString defaultBinaryFilePatterns()
{
	return QStringLiteral("*.bin;*.frx");
}

inline QString binaryFilePatterns()
{
	return QSettings().value(QStringLiteral("BinaryCompare/FilePatterns"),
		defaultBinaryFilePatterns()).toString();
}

/** Whether a path matches the binary patterns: WinMerge's ShowAutoMergeDoc
    opens such a file in its binary comparison. */
inline bool matchesBinaryFilePatterns(const QString &path)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	if (suffix.isEmpty())
		return false;
	const QStringList patterns = binaryFilePatterns().split(QLatin1Char(';'),
		Qt::SkipEmptyParts);
	for (const QString &pattern : patterns)
	{
		const QString trimmed = pattern.trimmed();
		if (trimmed.startsWith(QStringLiteral("*."))
			&& trimmed.mid(2).toLower() == suffix)
			return true;
	}
	return false;
}

/** Whether a file looks binary: a NUL among its first 8 KB that no
    UTF-16 or UTF-32 byte order mark explains, the heuristic diffutils
    uses. */
inline bool looksBinary(const QString &path)
{
	if (path.isEmpty())
		return false;
	QFile probe(path);
	if (!probe.open(QIODevice::ReadOnly))
		return false;
	const QByteArray head = probe.read(8192);
	const bool utf16or32Bom = head.startsWith("\xFF\xFE")
		|| head.startsWith("\xFE\xFF")
		|| head.startsWith(QByteArray("\x00\x00\xFE\xFF", 4));
	return !utf16or32Bom && head.contains('\0');
}

} // namespace lm
