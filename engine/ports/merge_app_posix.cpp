// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: implementations of the application-layer functions
// the engine expects from its host (declared in MergeApp.h / I18n.h).
// Upstream provides these from the MFC application; here they route to the
// engine Logger and the C runtime. The Qt application will override behavior
// where it matters (message boxes, translations).
#ifndef _WIN32

#include "pch.h"
#include "MergeApp.h"
#include "Logger.h"
#include "unicoder.h"
#include <cerrno>
#include <cstring>
#include <Poco/UnicodeConverter.h>

String GetSysError(int nerr)
{
	if (nerr == -1)
		nerr = errno;
	return std::strerror(nerr);
}

void LogErrorString(const String& sz)
{
	RootLogger::Error(sz);
}

void LogErrorStringUTF8(const std::string& sz)
{
	RootLogger::Error(sz);
}

void AppErrorMessageBox(const String& msg)
{
	RootLogger::Error(msg);
}

void* AppGetMainHWND()
{
	return nullptr;
}

namespace AppMsgBox
{

int error(const String& msg, int type)
{
	(void)type;
	RootLogger::Error(msg);
	return OK;
}

int warning(const String& msg, int type)
{
	(void)type;
	RootLogger::Warn(msg);
	return OK;
}

int information(const String& msg, int type)
{
	(void)type;
	RootLogger::Info(msg);
	return OK;
}

} // namespace AppMsgBox

namespace I18n
{

String tr(const std::string &str)
{
	return str; // no translation catalog wired yet (Phase 1: gettext/.po)
}

String tr(const std::wstring &str)
{
	std::string utf8;
	Poco::UnicodeConverter::toUTF8(str, utf8);
	return utf8;
}

String tr(const char *msgctxt, const std::string &str)
{
	(void)msgctxt;
	return str;
}

} // namespace I18n

#endif // !_WIN32
