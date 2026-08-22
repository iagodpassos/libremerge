// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace lm
{

/** Copy a file or directory tree; an existing destination file is moved
    to the Trash first (recoverable overwrite). */
bool copyRecursively(const QString &src, const QString &dst);

} // namespace lm
