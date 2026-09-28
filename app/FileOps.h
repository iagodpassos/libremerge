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

} // namespace lm
