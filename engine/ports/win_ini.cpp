// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: POSIX implementation of the Win32 profile (INI)
// API surface used by the vendored engine (COptionsMgr::ExportOptions).
// The format written here matches what COptionsMgr::ReadIniFile parses:
// "[Section]" headers and "key=value" lines, UTF-8, no BOM.
#ifndef _WIN32

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

struct IniLine
{
	std::string raw;      // verbatim line (kept for comments/unknown content)
	std::string section;  // section this line belongs to ("" before the first)
	std::string key;      // parsed key, empty when not a key=value line
};

std::string trim(const std::string &s)
{
	const char *ws = " \t\r\n";
	const size_t b = s.find_first_not_of(ws);
	if (b == std::string::npos)
		return {};
	const size_t e = s.find_last_not_of(ws);
	return s.substr(b, e - b + 1);
}

std::vector<IniLine> readLines(const std::string &path)
{
	std::vector<IniLine> lines;
	std::ifstream in(path);
	std::string cur;
	std::string section;
	while (std::getline(in, cur))
	{
		if (!cur.empty() && cur.back() == '\r')
			cur.pop_back();
		IniLine line{cur, section, {}};
		const std::string t = trim(cur);
		if (!t.empty() && t.front() == '[' && t.back() == ']')
		{
			section = t.substr(1, t.size() - 2);
			line.section = section;
		}
		else if (!t.empty() && t.front() != ';')
		{
			const size_t eq = t.find('=');
			if (eq != std::string::npos)
				line.key = trim(t.substr(0, eq));
		}
		lines.push_back(std::move(line));
	}
	return lines;
}

bool writeLines(const std::string &path, const std::vector<IniLine> &lines)
{
	std::ostringstream out;
	for (const IniLine &line : lines)
		out << line.raw << '\n';
	const std::string tmp = path + ".tmp";
	{
		std::ofstream f(tmp, std::ios::trunc);
		if (!f)
			return false;
		f << out.str();
		if (!f.good())
			return false;
	}
	return std::rename(tmp.c_str(), path.c_str()) == 0;
}

} // namespace

extern "C" int WritePrivateProfileString(const char *appName, const char *keyName,
                                         const char *value, const char *fileName)
{
	if (appName == nullptr || fileName == nullptr)
		return 0;

	std::vector<IniLine> lines = readLines(fileName);

	if (keyName == nullptr)
	{
		// Delete the whole section (header included).
		std::vector<IniLine> kept;
		for (IniLine &line : lines)
			if (line.section != appName)
				kept.push_back(std::move(line));
		return writeLines(fileName, kept) ? 1 : 0;
	}

	// Find the section and the key inside it.
	long sectionHeader = -1, lastInSection = -1, keyLine = -1;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		if (lines[i].section == appName)
		{
			if (sectionHeader < 0)
				sectionHeader = static_cast<long>(i);
			lastInSection = static_cast<long>(i);
			if (!lines[i].key.empty() && lines[i].key == keyName)
				keyLine = static_cast<long>(i);
		}
	}

	if (value == nullptr)
	{
		if (keyLine >= 0)
			lines.erase(lines.begin() + keyLine);
		return writeLines(fileName, lines) ? 1 : 0;
	}

	IniLine entry{std::string(keyName) + "=" + value, appName, keyName};
	if (keyLine >= 0)
		lines[keyLine] = std::move(entry);
	else if (sectionHeader >= 0)
		lines.insert(lines.begin() + lastInSection + 1, std::move(entry));
	else
	{
		lines.push_back(IniLine{std::string("[") + appName + "]", appName, {}});
		lines.push_back(std::move(entry));
	}
	return writeLines(fileName, lines) ? 1 : 0;
}

#endif // !_WIN32
