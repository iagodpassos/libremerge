// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QString>

namespace lm
{

/** True when the path looks like an archive LibreMerge can open, by its
    filename extension (7-Zip's GuessFormatByExtension). */
bool isArchivePath(const QString &path);

/** True when the file's content is one of those archives, whatever its
    name says (7-Zip's GuessFormatBySignature): a zip, a 7z, a tar,
    compressed or not, or a single compressed file. */
bool hasArchiveSignature(const QString &path);

/** WinMerge's IsArchiveFile (ArchiveGuessFormat): whether a file opens
    as a folder. Never with Options > Archive Support's "Enable archive
    file support" off; with "Detect archive type from file signature"
    on, the content decides, else the extension. */
bool isArchiveFile(const QString &path);

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
