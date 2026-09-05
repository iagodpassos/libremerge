// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QString>

namespace lm
{

/** True when the path looks like an archive LibreMerge can open. Like
    WinMerge's ArchiveGuessFormat, the decision is by filename
    extension. */
bool isArchivePath(const QString &path);

/** Extract an archive into destDir (an existing, empty directory).
    Compressed tar variants (tar.gz, tar.xz, ...) unpack in a single
    pass; an archive whose entire content is one nested archive is
    unwrapped again, like WinMerge's DecompressArchive loop; a plain
    compressed file (log.gz) extracts to the file it wraps. onEntry
    runs once per entry and may return false to cancel. On failure the
    return is false with *error set; a cancelled extraction returns
    false with *error left empty. */
bool extractArchive(const QString &archivePath, const QString &destDir,
	QString *error,
	const std::function<bool(const QString &entryName)> &onEntry = {});

} // namespace lm
