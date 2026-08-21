// SPDX-License-Identifier: GPL-2.0-or-later
// LibreMerge ports layer: POSIX implementation of Src/Common/paths.h.
// Replaces the Win32 implementation (paths.cpp), which stays out of
// non-Windows builds. Pure string-logic functions are carried over from
// the upstream file (© WinMerge contributors); filesystem-facing ones are
// reimplemented with POSIX primitives. The canonical separator here is '/',
// but like upstream both '/' and '\\' are recognized on input.
#ifndef _WIN32

#include "pch.h"
#include "paths.h"
#include <cassert>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include "PathContext.h"
#include "unicoder.h"

namespace paths
{

static bool IsSlash(const String& pszStart, size_t nPos)
{
	return pszStart[nPos] == '/' || pszStart[nPos] == '\\';
}

bool EndsWithSlash(const String& s)
{
	if (size_t len = s.length())
		return IsSlash(s, (int)len - 1);
	return false;
}

/** Expand %VAR% environment references (the convention the engine uses). */
static String ExpandEnvVars(const String& text)
{
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

PATH_EXISTENCE DoesPathExist(const String& szPath, bool (*IsArchiveFile)(const String&) /*= nullptr*/)
{
	if (szPath.empty())
		return DOES_NOT_EXIST;

	String sPath = szPath;
	if (sPath.find('%') != String::npos)
		sPath = ExpandEnvVars(sPath);

	struct stat st;
	if (stat(sPath.c_str(), &st) != 0)
	{
		if (IsArchiveFile && IsArchiveFile(szPath))
			return IS_EXISTING_DIR;
		return DOES_NOT_EXIST;
	}
	if (S_ISDIR(st.st_mode))
		return IS_EXISTING_DIR;
	if (IsArchiveFile && IsArchiveFile(szPath))
		return IS_EXISTING_DIR;
	return IS_EXISTING_FILE;
}

String FindFileName(const String& path)
{
	const tchar_t *filename = path.c_str();
	while (const tchar_t *slash = tc::tcspbrk(filename, _T("\\/")))
	{
		if (*(slash + 1) == '\0')
			break;
		filename = slash + 1;
	}
	return filename;
}

String FindExtension(const String& path)
{
	// Like shlwapi's PathFindExtension: the extension includes the dot.
	String name = FindFileName(path);
	size_t dot = name.rfind('.');
	return dot == String::npos ? String() : name.substr(dot);
}

String RemoveExtension(const String& path)
{
	String ext = FindExtension(path);
	return path.substr(0, path.length() - ext.length());
}

void normalize(String & sPath)
{
	size_t len = sPath.length();
	if (!len)
		return;

	sPath = GetLongPath(sPath);

	// Do not remove the trailing slash from the filesystem root
	if (sPath.length() > 1 && EndsWithSlash(sPath))
		sPath.resize(sPath.length() - 1);
}

/**
 * Convert a path to canonical absolute form: environment references
 * expanded, made absolute against the current directory, and "."/".."
 * and duplicate slashes collapsed lexically. Symlinks are NOT resolved
 * (mirroring Win32 GetFullPathName, which does not either).
 */
String GetLongPath(const String& szPath, bool bExpandEnvs)
{
	String sPath = szPath;
	if (sPath.empty())
		return sPath;

	if (bExpandEnvs && sPath.find('%') != String::npos)
		sPath = ExpandEnvVars(sPath);

	// make absolute
	if (!sPath.empty() && sPath[0] != '/')
	{
		char cwd[4096];
		if (getcwd(cwd, sizeof(cwd)) != nullptr)
			sPath = String(cwd) + _T("/") + sPath;
	}

	// collapse '.', '..' and duplicate slashes
	std::vector<String> parts;
	String comp;
	for (size_t i = 0; i <= sPath.size(); ++i)
	{
		if (i == sPath.size() || IsSlash(sPath, i))
		{
			if (comp == _T(".."))
			{
				if (!parts.empty())
					parts.pop_back();
			}
			else if (!comp.empty() && comp != _T("."))
				parts.push_back(comp);
			comp.clear();
		}
		else
			comp += sPath[i];
	}
	String out;
	for (const String& part : parts)
	{
		out += '/';
		out += part;
	}
	return out.empty() ? _T("/") : out;
}

bool CreateIfNeeded(const String& szPath)
{
	if (szPath.empty())
		return false;

	String sPath = GetLongPath(szPath);
	if (IsDirectory(sPath))
		return true;

	// walk down the components creating what is missing
	size_t pos = 0;
	while (pos != String::npos)
	{
		pos = sPath.find('/', pos + 1);
		String prefix = (pos == String::npos) ? sPath : sPath.substr(0, pos);
		if (prefix.empty() || IsDirectory(prefix))
			continue;
		if (mkdir(prefix.c_str(), 0777) != 0 && !IsDirectory(prefix))
			return false;
	}
	return IsDirectory(sPath);
}

PATH_EXISTENCE GetPairComparability(const PathContext & paths, bool (*IsArchiveFile)(const String&) /*= nullptr*/)
{
	// fail if not both specified
	if (paths.GetSize() < 2 || paths[0].empty() || paths[1].empty())
		return DOES_NOT_EXIST;
	PATH_EXISTENCE p1 = DoesPathExist(paths[0], IsArchiveFile);
	// short circuit testing right if left doesn't exist
	if (p1 == DOES_NOT_EXIST)
		return DOES_NOT_EXIST;
	PATH_EXISTENCE p2 = DoesPathExist(paths[1], IsArchiveFile);
	if (p1 != p2)
	{
		p1 = DoesPathExist(paths[0]);
		p2 = DoesPathExist(paths[1]);
		if (p1 != p2)
			return DOES_NOT_EXIST;
	}
	if (paths.GetSize() < 3) return p1;
	PATH_EXISTENCE p3 = DoesPathExist(paths[2], IsArchiveFile);
	if (p2 != p3)
	{
		p1 = DoesPathExist(paths[0]);
		p2 = DoesPathExist(paths[1]);
		p3 = DoesPathExist(paths[2]);
		if (p1 != p2 || p2 != p3)
			return DOES_NOT_EXIST;
	}
	return p1;
}

bool IsShortcut(const String& inPath)
{
	(void)inPath;
	return false; // Windows .lnk shortcuts do not exist on POSIX
}

String ExpandShortcut(const String &inFile)
{
	(void)inFile;
	return _T("");
}

bool IsDirectory(const String &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

String ConcatPath(const String & path, const String & subpath)
{
	if (path.empty())
		return subpath;
	if (subpath.empty())
		return path;
	if (EndsWithSlash(path))
	{
		return String(path).append(subpath.c_str() + (IsSlash(subpath, 0) ? 1 : 0));
	}
	else
	{
		if (IsSlash(subpath, 0))
		{
			return path + subpath;
		}
		else
		{
			return path + _T("/") + subpath;
		}
	}
}

String GetParentPath(const String& path)
{
	String parentPath(path);
	size_t len = parentPath.length();

	// Remove last slash from paths
	if (len > 1 && IsSlash(parentPath, len - 1))
	{
		parentPath.resize(len - 1);
		--len;
	}

	// Remove last part of path
	size_t pos = parentPath.find_last_of(_T("\\/"));
	if (pos != parentPath.npos)
	{
		// Do not remove the slash of the filesystem root
		parentPath.resize(pos == 0 ? 1 : pos);
	}
	return parentPath;
}

String GetLastSubdir(const String & path)
{
	String parentPath(path);
	size_t len = parentPath.length();
	if (len == 0)
		return parentPath;

	// Remove last slash from paths
	if (IsSlash(parentPath, len - 1))
	{
		parentPath.erase(len - 1, 1);
		--len;
	}

	// Find last part of path
	size_t pos = parentPath.find_last_of(_T("\\/"));
	if (pos != String::npos && pos > 0)
		parentPath.erase(0, pos);
	return parentPath;
}

bool IsPathAbsolute(const String &path)
{
	if (path.empty())
		return false;
	if (path[0] == '/')
		return true;
	// accept Windows-style absolute paths too, as the engine handles both
	if (path.length() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
		return true;
	if (path.length() >= 3 && path[0] == '\\' && path[1] == '\\')
		return true;
	return false;
}

String EnsurePathExist(const String & sPath)
{
	int rtn = DoesPathExist(sPath);
	if (rtn == IS_EXISTING_DIR)
		return sPath;
	if (rtn == IS_EXISTING_FILE)
		return _T("");
	if (!CreateIfNeeded(sPath))
		return _T("");
	// Check creating folder succeeded
	if (DoesPathExist(sPath) == IS_EXISTING_DIR)
		return sPath;
	else
		return _T("");
}

/**
 * @brief Return true if *pszChar is a slash (either direction).
 * Unlike Windows, ':' is not a path delimiter on POSIX.
 */
static bool IsSlashOrColon(const tchar_t *pszChar, const tchar_t *begin)
{
	(void)begin;
	return (*pszChar == '/' || *pszChar == '\\');
}

void SplitFilename(const String& pathLeft, String* pPath, String* pFile, String* pExt)
{
	const tchar_t *pszChar = pathLeft.c_str() + pathLeft.length();
	const tchar_t *pend = pszChar;
	const tchar_t *extptr = 0;
	bool ext = false;

	while (pathLeft.c_str() < --pszChar)
	{
		if (*pszChar == '.')
		{
			if (!ext)
			{
				if (pExt != nullptr)
				{
					(*pExt) = pszChar + 1;
				}
				ext = true; // extension is only after last period
				extptr = pszChar;
			}
		}
		else if (IsSlashOrColon(pszChar, pathLeft.c_str()))
		{
			// Ok, found last slash, so we collect any info desired
			// and we're done

			if (pPath != nullptr)
			{
				// Grab directory (omit trailing slash)
				size_t len = pszChar - pathLeft.c_str();
				*pPath = pathLeft;
				pPath->erase(len); // Cut rest of path
			}

			if (pFile != nullptr)
			{
				// Grab file
				*pFile = pszChar + 1;
			}

			goto endSplit;
		}
	}

	// Never found a delimiter
	if (pFile != nullptr)
	{
		*pFile = pathLeft;
	}

endSplit:
	// if both filename & extension requested, remove extension from filename

	if (pFile != nullptr && pExt != nullptr && extptr != nullptr)
	{
		size_t extlen = pend - extptr;
		pFile->erase(pFile->length() - extlen);
	}
}

String GetPathOnly(const String& fullpath)
{
	if (fullpath.empty()) return _T("");
	String spath;
	SplitFilename(fullpath, &spath, 0, 0);
	return spath;
}

bool IsURL(const String& abspath)
{
	for (size_t i = 0; i < abspath.length(); ++i)
	{
		const auto c = abspath[i];
		if (c == '\\' || c == '/')
		{
			// If there is a \ or / before the : character, consider it not a URL.
			return false;
		}
		else if (c == ':')
			return (i != 1);
	}
	return false;
}

bool IsURLorCLSID(const String& path)
{
	return IsURL(path) || path.find(_T("::{")) != String::npos;
}

bool isFileURL(const String& path)
{
	return path.length() >= 5 && tc::tcsnicmp(path.c_str(), _T("file:"), 5) == 0;
}

String FromURL(const String& url)
{
	// file://host/path or file:///path -> /path (percent-decoded)
	String path = url;
	if (isFileURL(path))
	{
		path.erase(0, 5);
		while (path.length() >= 2 && path[0] == '/' && path[1] == '/')
			path.erase(0, 1);
	}
	String out;
	for (size_t i = 0; i < path.size(); ++i)
	{
		if (path[i] == '%' && i + 2 < path.size()
			&& isxdigit((unsigned char)path[i + 1]) && isxdigit((unsigned char)path[i + 2]))
		{
			out += static_cast<tchar_t>(strtol(path.substr(i + 1, 2).c_str(), nullptr, 16));
			i += 2;
		}
		else
			out += path[i];
	}
	return out;
}

String urlEncodeFileName(const String& filename)
{
	String encoded = filename;
	strutils::replace(encoded, _T("%"), _T("%25"));
	strutils::replace(encoded, _T("#"), _T("%23"));
	return encoded;
}

bool IsDecendant(const String& path, const String& ancestor)
{
	return path.length() > ancestor.length() &&
		   strutils::compare_nocase(String(path.c_str(), path.c_str() + ancestor.length()), ancestor) == 0;
}

static void replace_char(tchar_t *s, int target, int repl)
{
	tchar_t *p;
	for (p=s; *p != _T('\0'); p = tc::tcsinc(p))
		if (*p == target)
			*p = (tchar_t)repl;
}

String ToWindowsPath(const String& path)
{
	String winpath = path;
	replace_char(&*winpath.begin(), '/', '\\');
	return winpath;
}

String ToUnixPath(const String& path)
{
	String unixpath = path;
	replace_char(&*unixpath.begin(), '\\', '/');
	return unixpath;
}

bool IsValidName(const String& name)
{
	if (name.empty())
		return false;
	for (String::const_iterator it = name.begin(); it != name.end(); ++it)
		if (*it == '/' || *it == '\0')
			return false;
	return true;
}

bool IsNullDeviceName(const String& name)
{
	return (tc::tcsicmp(name.c_str(), NATIVE_NULL_DEVICE_NAME) == 0 ||
			tc::tcsicmp(name.c_str(), NATIVE_NULL_DEVICE_NAME_LONG) == 0 ||
			tc::tcsicmp(name.c_str(), _T("NUL")) == 0);
}

}

#endif // !_WIN32
