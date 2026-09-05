// SPDX-License-Identifier: GPL-3.0-or-later
#include "ArchiveCompare.h"

#include <archive.h>
#include <archive_entry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace
{

// like WinMerge, detection is by extension (7-Zip's format guess starts
// from the extension too); every entry here is a format or filter
// libarchive reads out of the box
const char *const kArchiveSuffixes[] = {
	".zip", ".jar", ".7z", ".tar",
	".tar.gz", ".tgz", ".tar.bz2", ".tbz2", ".tar.xz", ".txz",
	".tar.zst", ".gz", ".bz2", ".xz", ".zst",
};

QString archiveError(struct archive *a)
{
	const char *msg = archive_error_string(a);
	return msg != nullptr ? QString::fromUtf8(msg)
	                      : QStringLiteral("unknown error");
}

/** The name a bare compressed file (log.gz) extracts to: the archive's
    own name without the compression suffix, WinMerge's GetDefaultName. */
QString rawEntryName(const QString &archivePath)
{
	const QString base = QFileInfo(archivePath).fileName();
	const int dot = base.lastIndexOf(QLatin1Char('.'));
	return dot > 0 ? base.left(dot) : base + QStringLiteral(".out");
}

/** One extraction pass of one archive into destDir. */
bool extractOnce(const QString &archivePath, const QString &destDir,
	QString *error, const std::function<bool(const QString &)> &onEntry)
{
	// resolve symlinked temp roots (macOS's /var -> /private/var), or
	// the writer's SECURE_SYMLINKS check refuses the destination itself
	const QString canonicalDest = QFileInfo(destDir).canonicalFilePath();
	const QString dest = canonicalDest.isEmpty() ? destDir : canonicalDest;

	struct archive *reader = archive_read_new();
	archive_read_support_filter_all(reader);
	archive_read_support_format_all(reader);
	// raw catches single-file compression (gz/bz2/xz around a lone file)
	archive_read_support_format_raw(reader);
	archive_read_support_format_empty(reader);

	struct archive *writer = archive_write_disk_new();
	// no SECURE_NOABSOLUTEPATHS: the target pathname built below is
	// absolute on purpose; entry-supplied absolute paths are rejected
	// by the sanitizing above it
	archive_write_disk_set_options(writer, ARCHIVE_EXTRACT_TIME
		| ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_SECURE_NODOTDOT);
	archive_write_disk_set_standard_lookup(writer);

	bool ok = true;
	if (archive_read_open_filename(reader,
		QFile::encodeName(archivePath).constData(), 10240) != ARCHIVE_OK)
	{
		if (error != nullptr)
			*error = archiveError(reader);
		ok = false;
	}

	while (ok)
	{
		struct archive_entry *entry = nullptr;
		const int r = archive_read_next_header(reader, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r < ARCHIVE_WARN)
		{
			if (error != nullptr)
				*error = archiveError(reader);
			ok = false;
			break;
		}

		QString name = archive_format(reader) == ARCHIVE_FORMAT_RAW
			? rawEntryName(archivePath)
			: QFile::decodeName(archive_entry_pathname(entry));
		// belt and braces on top of the writer's SECURE flags: never
		// let an entry escape destDir
		name = QDir::cleanPath(name);
		if (name.isEmpty() || name.startsWith(QLatin1Char('/'))
			|| name == QStringLiteral("..")
			|| name.startsWith(QStringLiteral("../")))
			continue;

		if (onEntry && !onEntry(name))
		{
			ok = false; // cancelled: *error stays empty
			break;
		}

		const QString target = dest + QLatin1Char('/') + name;
		archive_entry_set_pathname(entry, QFile::encodeName(target).constData());
		if (archive_write_header(writer, entry) < ARCHIVE_WARN)
		{
			if (error != nullptr)
				*error = archiveError(writer);
			ok = false;
			break;
		}
		const void *buff = nullptr;
		size_t size = 0;
		la_int64_t offset = 0;
		int dr;
		while ((dr = archive_read_data_block(reader, &buff, &size, &offset))
			== ARCHIVE_OK)
		{
			if (archive_write_data_block(writer, buff, size, offset)
				< ARCHIVE_WARN)
			{
				dr = ARCHIVE_FATAL;
				break;
			}
		}
		if (dr != ARCHIVE_EOF && dr < ARCHIVE_WARN)
		{
			if (error != nullptr)
				*error = archiveError(reader);
			ok = false;
			break;
		}
		archive_write_finish_entry(writer);
	}

	archive_write_free(writer);
	archive_read_free(reader);
	return ok;
}

} // namespace

namespace lm
{

bool isArchivePath(const QString &path)
{
	const QString lower = path.toLower();
	for (const char *suffix : kArchiveSuffixes)
		if (lower.endsWith(QLatin1String(suffix)))
			return true;
	return false;
}

bool extractArchive(const QString &archivePath, const QString &destDir,
	QString *error, const std::function<bool(const QString &)> &onEntry)
{
	if (error != nullptr)
		error->clear();
	if (!extractOnce(archivePath, destDir, error, onEntry))
		return false;

	// WinMerge's DecompressArchive unwraps until the result stops being
	// an archive; compressed tars are already handled in one pass by
	// libarchive's filters, so this only fires for genuinely nested
	// archives (a zip holding one zip, a lone inner tarball)
	for (int depth = 0; depth < 5; ++depth)
	{
		QDir dir(destDir);
		const QFileInfoList items = dir.entryInfoList(
			QDir::AllEntries | QDir::NoDotAndDotDot);
		if (items.size() != 1 || !items.at(0).isFile()
			|| !isArchivePath(items.at(0).fileName()))
			break;
		const QString inner = destDir + QStringLiteral("/.lm-inner-")
			+ items.at(0).fileName();
		if (!QFile::rename(items.at(0).filePath(), inner))
			break;
		const bool ok = extractOnce(inner, destDir, error, onEntry);
		QFile::remove(inner);
		if (!ok)
			return false;
	}
	return true;
}

} // namespace lm
