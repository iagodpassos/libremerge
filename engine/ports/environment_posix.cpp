// SPDX-License-Identifier: GPL-2.0-or-later
// LibreMerge ports layer: POSIX implementation of Src/Common/Environment.h.
// Replaces the Win32 implementation (Environment.cpp), which stays out of
// non-Windows builds. Mirrors the upstream structure; the Poco-based parts
// are carried over as-is (© WinMerge contributors).
#ifndef _WIN32

#include "pch.h"
#include "Environment.h"
#include <cstdlib>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
#include <Poco/Path.h>
#include <Poco/Process.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include "paths.h"
#include "unicoder.h"

using Poco::Path;
using Poco::Process;

namespace env
{

static String strTempPath;
static String strProgPath;

void SetTemporaryPath(const String& path)
{
	strTempPath = paths::AddTrailingSlash(paths::GetLongPath(path));
	paths::CreateIfNeeded(strTempPath);
}

String GetTemporaryPath()
{
	if (strTempPath.empty())
	{
		strTempPath = GetSystemTempPath();
		if (strTempPath.empty())
			return strTempPath;

		paths::CreateIfNeeded(strTempPath);
	}
	return strTempPath;
}

String GetTemporaryFileName(const String& lpPathName, const String& lpPrefixString, int * pnerr /*= nullptr*/)
{
	paths::CreateIfNeeded(lpPathName);
	String templ = paths::ConcatPath(lpPathName, lpPrefixString + _T("XXXXXX"));
	std::string buf(templ);
	int fd = mkstemp(&buf[0]);
	if (fd == -1)
	{
		if (pnerr != nullptr)
			*pnerr = errno;
		return _T("");
	}
	close(fd);
	return buf;
}

String GetTempChildPath()
{
	String path;
	do
	{
		path = paths::ConcatPath(GetTemporaryPath(), strutils::format(_T("%08x"), rand()));
	} while (paths::IsDirectory(path) || !paths::CreateIfNeeded(path));
	return path;
}

void SetProgPath(const String& path)
{
	strProgPath = paths::AddTrailingSlash(path);
}

String GetProgPath()
{
	if (strProgPath.empty())
	{
		char buf[4096] = {0};
#ifdef __APPLE__
		uint32_t size = sizeof(buf);
		if (_NSGetExecutablePath(buf, &size) != 0)
			buf[0] = '\0';
#else
		ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
		if (n > 0)
			buf[n] = '\0';
#endif
		strProgPath = paths::GetPathOnly(buf);
	}
	return strProgPath;
}

String GetWindowsDirectory()
{
	return _T(""); // no such thing here
}

String GetMyDocuments()
{
	const char *home = getenv("HOME");
	if (home == nullptr)
		return _T("");
	return paths::ConcatPath(home, _T("Documents"));
}

String GetAppDataPath()
{
	const char *home = getenv("HOME");
#ifdef __APPLE__
	if (home == nullptr)
		return _T("");
	return paths::ConcatPath(home, _T("Library/Application Support"));
#else
	if (const char *xdg = getenv("XDG_CONFIG_HOME"))
		return xdg;
	if (home == nullptr)
		return _T("");
	return paths::ConcatPath(home, _T(".config"));
#endif
}

String GetPerInstanceString(const String& name)
{
	std::basic_stringstream<tchar_t> stream;
	stream << name << Process::id();
	return stream.str();
}

String GetSystemTempPath()
{
	try
	{
		return ucr::toTString(Path::temp());
	}
	catch (...)
	{
		return _T("");
	}
}

String ExpandEnvironmentVariables(const String& text)
{
	// %VAR% expansion, the convention the engine's option strings use
	String out;
	size_t i = 0;
	while (i < text.size())
	{
		if (text[i] == '%')
		{
			size_t end = text.find('%', i + 1);
			if (end != String::npos && end > i + 1)
			{
				std::string name = text.substr(i + 1, end - i - 1);
				if (const char *value = getenv(name.c_str()))
				{
					out += value;
					i = end + 1;
					continue;
				}
			}
		}
		out += text[i++];
	}
	return out;
}

bool LoadRegistryFromFile(const String& sRegFilePath)
{
	(void)sRegFilePath;
	return false; // no registry on POSIX
}

bool SaveRegistryToFile(const String& sRegFilePath, const String& sRegDir)
{
	(void)sRegFilePath; (void)sRegDir;
	return false; // no registry on POSIX
}

}

#endif // !_WIN32
