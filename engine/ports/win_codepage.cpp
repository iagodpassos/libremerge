// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: Win32 codepage conversion API implemented over ICU.
// The vendored engine (unicoder.cpp and friends) converts between arbitrary
// "codepage numbers" and wide strings via MultiByteToWideChar and
// WideCharToMultiByte; here those become ICU converters. On this build
// tchar_t is char and the internal encoding is UTF-8 (GetACP() == CP_UTF8);
// wchar_t is UTF-32.
#ifndef _WIN32

#include <cstdio>
#include <cstring>
#include <string>
#include <unicode/ucnv.h>
#include <unicode/ustring.h>

// Redeclare here what posix_compat.h (force-included) already declared;
// this file provides the definitions.

// Resolve a Windows codepage number to an ICU converter name.
// (shared with ports/exconverter_icu.cpp via ports/lm_icu.h)
std::string lmCodepageToConverterName(unsigned codepage)
{
	switch (codepage)
	{
	case 0:     // CP_ACP
	case 1:     // CP_OEMCP
	case 3:     // CP_THREAD_ACP
	case 65001: return "UTF-8";
	case 1200:  return "UTF-16LE";
	case 1201:  return "UTF-16BE";
	case 12000: return "UTF-32LE";
	case 12001: return "UTF-32BE";
	case 65000: return "UTF-7";
	case 20127: return "US-ASCII";
	default:
	{
		char name[32];
		std::snprintf(name, sizeof(name), "windows-%u", codepage);
		return name;
	}
	}
}

UConverter *lmOpenConverter(unsigned codepage)
{
	UErrorCode err = U_ZERO_ERROR;
	std::string name = lmCodepageToConverterName(codepage);
	UConverter *conv = ucnv_open(name.c_str(), &err);
	if (U_FAILURE(err) && name.rfind("windows-", 0) == 0)
	{
		char alt[32];
		std::snprintf(alt, sizeof(alt), "cp%u", codepage);
		err = U_ZERO_ERROR;
		conv = ucnv_open(alt, &err);
	}
	return U_FAILURE(err) ? nullptr : conv;
}

namespace
{
inline UConverter *openConverter(unsigned codepage) { return lmOpenConverter(codepage); }
} // namespace

UINT GetACP(void)
{
	return 65001; // the internal "ANSI codepage" of POSIX builds is UTF-8
}

BOOL IsValidCodePage(UINT codepage)
{
	UConverter *conv = openConverter(codepage);
	if (conv == nullptr)
		return FALSE;
	ucnv_close(conv);
	return TRUE;
}

int LmMultiByteToUtf16LE(UINT codepage, DWORD flags, const char *src, int srclen,
                         char16_t *dst, int dstlen)
{
	if (src == nullptr)
		return 0;
	if (srclen < 0)
		srclen = static_cast<int>(std::strlen(src));

	UConverter *conv = lmOpenConverter(codepage);
	if (conv == nullptr)
		return 0;
	UErrorCode err = U_ZERO_ERROR;
	if (flags & MB_ERR_INVALID_CHARS)
		ucnv_setToUCallBack(conv, UCNV_TO_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &err);
	err = U_ZERO_ERROR;
	int32_t u16len = ucnv_toUChars(conv, reinterpret_cast<UChar *>(dst),
	                               (dst == nullptr) ? 0 : dstlen, src, srclen, &err);
	ucnv_close(conv);
	if (dst == nullptr || dstlen == 0)
		return (err == U_BUFFER_OVERFLOW_ERROR || U_SUCCESS(err)) ? u16len : 0;
	return U_FAILURE(err) ? 0 : u16len;
}

int LmUtf16LEToMultiByte(UINT codepage, DWORD flags, const char16_t *src, int srclen,
                         char *dst, int dstlen, const char *defaultChar,
                         BOOL *usedDefaultChar)
{
	(void)flags; (void)defaultChar;
	if (usedDefaultChar != nullptr)
		*usedDefaultChar = FALSE;
	if (src == nullptr)
		return 0;
	if (srclen < 0)
	{
		srclen = 0;
		while (src[srclen] != 0)
			++srclen;
	}
	UConverter *conv = lmOpenConverter(codepage);
	if (conv == nullptr)
		return 0;
	// strict pass first to report lossy conversions
	UErrorCode err = U_ZERO_ERROR;
	ucnv_setFromUCallBack(conv, UCNV_FROM_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &err);
	err = U_ZERO_ERROR;
	int32_t outlen = ucnv_fromUChars(conv, dst, (dst == nullptr) ? 0 : dstlen,
	                                 reinterpret_cast<const UChar *>(src), srclen, &err);
	if (U_FAILURE(err) && err != U_BUFFER_OVERFLOW_ERROR)
	{
		if (usedDefaultChar != nullptr)
			*usedDefaultChar = TRUE;
		err = U_ZERO_ERROR;
		ucnv_setFromUCallBack(conv, UCNV_FROM_U_CALLBACK_SUBSTITUTE, nullptr, nullptr, nullptr, &err);
		ucnv_resetFromUnicode(conv);
		err = U_ZERO_ERROR;
		outlen = ucnv_fromUChars(conv, dst, (dst == nullptr) ? 0 : dstlen,
		                         reinterpret_cast<const UChar *>(src), srclen, &err);
	}
	ucnv_close(conv);
	if (dst == nullptr || dstlen == 0)
		return (err == U_BUFFER_OVERFLOW_ERROR || U_SUCCESS(err)) ? outlen : 0;
	return U_FAILURE(err) ? 0 : outlen;
}

int MultiByteToWideChar(UINT codepage, DWORD flags, const char *src, int srclen,
                        wchar_t *dst, int dstlen)
{
	if (src == nullptr)
		return 0;
	if (srclen < 0)
		srclen = static_cast<int>(std::strlen(src));

	UConverter *conv = openConverter(codepage);
	if (conv == nullptr)
		return 0;

	UErrorCode err = U_ZERO_ERROR;
	if (flags & MB_ERR_INVALID_CHARS)
		ucnv_setToUCallBack(conv, UCNV_TO_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &err);

	// codepage bytes -> UTF-16
	std::u16string u16(static_cast<size_t>(srclen) + 16, u'\0');
	err = U_ZERO_ERROR;
	int32_t u16len = ucnv_toUChars(conv, reinterpret_cast<UChar *>(&u16[0]),
	                               static_cast<int32_t>(u16.size()), src, srclen, &err);
	if (err == U_BUFFER_OVERFLOW_ERROR)
	{
		u16.resize(static_cast<size_t>(u16len) + 1);
		err = U_ZERO_ERROR;
		ucnv_resetToUnicode(conv);
		u16len = ucnv_toUChars(conv, reinterpret_cast<UChar *>(&u16[0]),
		                       static_cast<int32_t>(u16.size()), src, srclen, &err);
	}
	ucnv_close(conv);
	if (U_FAILURE(err))
		return 0;

	// UTF-16 -> platform wchar_t
	int32_t wlen = 0;
	err = U_ZERO_ERROR;
	u_strToWCS(nullptr, 0, &wlen, reinterpret_cast<const UChar *>(u16.data()), u16len, &err);
	if (err != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(err))
		return 0;
	if (dstlen == 0 || dst == nullptr)
		return wlen;
	if (wlen > dstlen)
		return 0; // ERROR_INSUFFICIENT_BUFFER behavior
	err = U_ZERO_ERROR;
	u_strToWCS(dst, dstlen, &wlen, reinterpret_cast<const UChar *>(u16.data()), u16len, &err);
	return U_FAILURE(err) ? 0 : wlen;
}

int WideCharToMultiByte(UINT codepage, DWORD flags, const wchar_t *src, int srclen,
                        char *dst, int dstlen, const char *defaultChar,
                        BOOL *usedDefaultChar)
{
	(void)flags; (void)defaultChar;
	if (usedDefaultChar != nullptr)
		*usedDefaultChar = FALSE;
	if (src == nullptr)
		return 0;
	if (srclen < 0)
		srclen = static_cast<int>(wcslen(src));

	// platform wchar_t -> UTF-16
	int32_t u16cap = srclen * 2 + 16;
	std::u16string u16(static_cast<size_t>(u16cap), u'\0');
	int32_t u16len = 0;
	UErrorCode err = U_ZERO_ERROR;
	u_strFromWCS(reinterpret_cast<UChar *>(&u16[0]), u16cap, &u16len, src, srclen, &err);
	if (U_FAILURE(err))
		return 0;

	UConverter *conv = openConverter(codepage);
	if (conv == nullptr)
		return 0;

	// First try a strict conversion to detect lossy mappings.
	err = U_ZERO_ERROR;
	ucnv_setFromUCallBack(conv, UCNV_FROM_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &err);
	std::string out(static_cast<size_t>(u16len) * 4 + 16, '\0');
	err = U_ZERO_ERROR;
	int32_t outlen = ucnv_fromUChars(conv, &out[0], static_cast<int32_t>(out.size()),
	                                 reinterpret_cast<const UChar *>(u16.data()), u16len, &err);
	if (err == U_BUFFER_OVERFLOW_ERROR)
	{
		out.resize(static_cast<size_t>(outlen) + 1);
		err = U_ZERO_ERROR;
		ucnv_resetFromUnicode(conv);
		outlen = ucnv_fromUChars(conv, &out[0], static_cast<int32_t>(out.size()),
		                         reinterpret_cast<const UChar *>(u16.data()), u16len, &err);
	}
	if (U_FAILURE(err))
	{
		// Lossy: redo with substitution, report the default-char use.
		if (usedDefaultChar != nullptr)
			*usedDefaultChar = TRUE;
		err = U_ZERO_ERROR;
		ucnv_setFromUCallBack(conv, UCNV_FROM_U_CALLBACK_SUBSTITUTE, nullptr, nullptr, nullptr, &err);
		ucnv_resetFromUnicode(conv);
		err = U_ZERO_ERROR;
		outlen = ucnv_fromUChars(conv, &out[0], static_cast<int32_t>(out.size()),
		                         reinterpret_cast<const UChar *>(u16.data()), u16len, &err);
		if (err == U_BUFFER_OVERFLOW_ERROR)
		{
			out.resize(static_cast<size_t>(outlen) + 1);
			err = U_ZERO_ERROR;
			ucnv_resetFromUnicode(conv);
			outlen = ucnv_fromUChars(conv, &out[0], static_cast<int32_t>(out.size()),
			                         reinterpret_cast<const UChar *>(u16.data()), u16len, &err);
		}
	}
	ucnv_close(conv);
	if (U_FAILURE(err))
		return 0;

	if (dstlen == 0 || dst == nullptr)
		return outlen;
	if (outlen > dstlen)
		return 0;
	std::memcpy(dst, out.data(), static_cast<size_t>(outlen));
	return outlen;
}

#endif // !_WIN32
