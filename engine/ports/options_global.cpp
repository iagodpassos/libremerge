// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: definition of the global options-manager accessor
// declared in OptionsMgr.h. Upstream defines it in the MFC application layer
// (MergeApp); here the host application (or test fixture) installs one.
#include "pch.h"
#include "OptionsMgr.h"
#include "options_global.h"

static COptionsMgr *g_pOptionsMgr = nullptr;

COptionsMgr * GetOptionsMgr()
{
	return g_pOptionsMgr;
}

void SetOptionsMgr(COptionsMgr *pOptionsMgr)
{
	g_pOptionsMgr = pOptionsMgr;
}

// GetSysColor's answer (posix_compat.h): the application's, once it sets it
extern "C" unsigned long (*lm_sysColorProvider)(int index) = nullptr;
