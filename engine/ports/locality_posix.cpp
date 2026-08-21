// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: POSIX implementation of Src/Common/locality.h.
// Replaces the Win32 NLS implementation (locality.cpp), which stays out of
// non-Windows builds. Uses the C locale database (localeconv/strftime) and
// strptime for the formats locality::ParseDateTime is expected to accept.
#ifndef _WIN32

#include "pch.h"
#include "locality.h"
#include <clocale>
#include <cstring>
#include <ctime>

namespace locality {

/**
 * Insert the locale's thousands separators into an ASCII integer string.
 * Mirrors the Win32 GetNumberFormat-based implementation: ASCII digits only,
 * grouping and separators from the user's locale, optional fixed decimals.
 */
String GetLocaleStr(const tchar_t *str, int decimalDigits)
{
	const struct lconv *lc = localeconv();
	const char *thousandsSep = (lc && lc->thousands_sep && *lc->thousands_sep) ? lc->thousands_sep : ",";
	const char *decimalSep = (lc && lc->decimal_point && *lc->decimal_point) ? lc->decimal_point : ".";
	// grouping is a byte string, e.g. "\3" (western) or "\3\2" (Indic);
	// empty means "no grouping".
	const char *grouping = (lc && lc->grouping && *lc->grouping) ? lc->grouping : "\3";

	String in(str);
	String sign;
	if (!in.empty() && (in[0] == '-' || in[0] == '+'))
	{
		sign = in.substr(0, 1);
		in.erase(0, 1);
	}

	String out;
	int groupIdx = 0;
	int digitsInGroup = 0;
	int groupSize = grouping[0] == CHAR_MAX ? 0 : grouping[0];
	for (auto it = in.rbegin(); it != in.rend(); ++it)
	{
		if (groupSize > 0 && digitsInGroup == groupSize)
		{
			out.insert(0, thousandsSep);
			digitsInGroup = 0;
			if (grouping[groupIdx + 1] != '\0')
			{
				++groupIdx;
				groupSize = grouping[groupIdx] == CHAR_MAX ? 0 : grouping[groupIdx];
			}
		}
		out.insert(out.begin(), *it);
		++digitsInGroup;
	}
	out.insert(0, sign);

	if (decimalDigits > 0)
	{
		out += decimalSep;
		out.append(decimalDigits, '0');
	}
	return out;
}

String NumToLocaleStr(int n)
{
	return GetLocaleStr(strutils::to_str(n).c_str());
}

String NumToLocaleStr(int64_t n)
{
	return GetLocaleStr(strutils::to_str(n).c_str());
}

/**
 * Convert unix time (seconds since 1970) to a locale-formatted local
 * date+time string, like the Win32 GetDateFormat+GetTimeFormat pair.
 */
String TimeString(const int64_t * tim)
{
	if (tim == nullptr) return _T("---");
	if (*tim == INT64_MIN / 1000 / 1000)
		return String();

	time_t t = static_cast<time_t>(*tim);
	struct tm tmLocal;
	if (localtime_r(&t, &tmLocal) == nullptr)
		return _T("---");

	tchar_t buff[128];
	if (tc::tcsftime(buff, sizeof(buff)/sizeof(buff[0]), _T("%x %X"), &tmLocal) == 0)
		return _T("---");
	return buff;
}

/**
 * Parse a date/time string in the user's locale or in common ISO forms.
 * Returns microseconds since 1970-01-01 00:00:00 UTC, interpreting the
 * input as local time (matching the Win32 VarDateFromStr implementation).
 */
bool ParseDateTime(const String& str, int64_t& result)
{
	if (str.empty())
		return false;

	static const char *formats[] = {
		"%x %X", "%x",
		"%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d",
		"%Y/%m/%d %H:%M:%S", "%Y/%m/%d",
		"%d/%m/%Y %H:%M:%S", "%d/%m/%Y",
		"%H:%M:%S", "%H:%M",
	};

	for (const char *fmt : formats)
	{
		struct tm tmParsed;
		memset(&tmParsed, 0, sizeof(tmParsed));
		tmParsed.tm_mday = 1; // default day for time-only inputs
		const char *end = strptime(str.c_str(), fmt, &tmParsed);
		if (end == nullptr || *end != '\0')
			continue;
		if (fmt[1] == 'H') // time-only: anchor at the epoch date
		{
			tmParsed.tm_year = 70;
			tmParsed.tm_mon = 0;
			tmParsed.tm_mday = 1;
		}
		tmParsed.tm_isdst = -1;
		time_t t = mktime(&tmParsed);
		if (t == static_cast<time_t>(-1))
			continue;
		result = static_cast<int64_t>(t) * 1000000;
		return true;
	}
	return false;
}

}; // namespace locality

#endif // !_WIN32
