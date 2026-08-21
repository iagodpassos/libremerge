// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: stand-in for MSVC <io.h> on POSIX systems.
// The vendored engine includes <io.h> for low-level file APIs (_read, _open,
// ...); on POSIX those live in <unistd.h>/<fcntl.h>, with the underscore
// aliases provided by posix_compat.h (force-included by the build).
#pragma once

#ifdef _WIN32
#error "ports/io.h must not be picked up on Windows builds"
#endif

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
