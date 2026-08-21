// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: stand-in for crystaledit's MFC precompiled header
// (StdAfx.h) on POSIX builds — just the common standard headers.
#pragma once

#ifdef _WIN32
#error "ports/StdAfx.h must not be picked up on Windows builds"
#endif

#include <string>
#include <vector>
#include <map>
#include <list>
#include <array>
#include <memory>
#include <algorithm>
#include <functional>
#include <cassert>
#include <cstdint>
#include <cstring>
