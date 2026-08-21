// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: IExconverter implemented over ICU, replacing the
// Win32 IMultiLanguage2 (mlang) implementation in ExConverter.cpp. Handles
// codepage-to-codepage byte conversion and charset detection (ucsdet).
#ifndef _WIN32

#include "pch.h"
#include "ExConverter.h"
#include "charsets.h"
#include "lm_icu.h"
#include <cstring>
#include <unicode/ucnv.h>
#include <unicode/ucsdet.h>
#include <unicode/ustring.h>

namespace
{

class ExconverterICU : public IExconverter
{
public:
	bool initialize() override
	{
		return true;
	}

	bool convert(int srcCodepage, int dstCodepage, const unsigned char * src, size_t * srcbytes,
	             unsigned char * dest, size_t * destbytes) override
	{
		UConverter *from = lmOpenConverter(static_cast<unsigned>(srcCodepage));
		UConverter *to = lmOpenConverter(static_cast<unsigned>(dstCodepage));
		if (from == nullptr || to == nullptr)
		{
			if (from) ucnv_close(from);
			if (to) ucnv_close(to);
			return false;
		}
		UErrorCode err = U_ZERO_ERROR;
		char *target = reinterpret_cast<char *>(dest);
		const char *source = reinterpret_cast<const char *>(src);
		ucnv_convertEx(to, from,
			&target, target + *destbytes,
			&source, source + *srcbytes,
			nullptr, nullptr, nullptr, nullptr,
			TRUE, TRUE, &err);
		ucnv_close(from);
		ucnv_close(to);
		if (U_FAILURE(err) && err != U_BUFFER_OVERFLOW_ERROR)
			return false;
		*srcbytes = source - reinterpret_cast<const char *>(src);
		*destbytes = target - reinterpret_cast<char *>(dest);
		return true;
	}

	bool convertFromUnicode(int dstCodepage, const wchar_t * src, size_t * srcchars,
	                        char * dest, size_t *destbytes) override
	{
		// wchar_t (UTF-32) -> UTF-16 -> target codepage
		UErrorCode err = U_ZERO_ERROR;
		int32_t u16len = 0;
		std::u16string u16(*srcchars * 2 + 8, u'\0');
		u_strFromWCS(reinterpret_cast<UChar *>(&u16[0]), static_cast<int32_t>(u16.size()),
		             &u16len, src, static_cast<int32_t>(*srcchars), &err);
		if (U_FAILURE(err))
			return false;
		UConverter *to = lmOpenConverter(static_cast<unsigned>(dstCodepage));
		if (to == nullptr)
			return false;
		err = U_ZERO_ERROR;
		int32_t outlen = ucnv_fromUChars(to, dest, static_cast<int32_t>(*destbytes),
		                                 reinterpret_cast<const UChar *>(u16.data()), u16len, &err);
		ucnv_close(to);
		if (U_FAILURE(err))
			return false;
		*destbytes = static_cast<size_t>(outlen);
		return true;
	}

	bool convertToUnicode(int srcCodepage, const char * src, size_t * srcbytes,
	                      wchar_t * dest, size_t *destchars) override
	{
		UConverter *from = lmOpenConverter(static_cast<unsigned>(srcCodepage));
		if (from == nullptr)
			return false;
		UErrorCode err = U_ZERO_ERROR;
		std::u16string u16(*srcbytes + 8, u'\0');
		int32_t u16len = ucnv_toUChars(from, reinterpret_cast<UChar *>(&u16[0]),
		                               static_cast<int32_t>(u16.size()), src,
		                               static_cast<int32_t>(*srcbytes), &err);
		ucnv_close(from);
		if (U_FAILURE(err))
			return false;
		int32_t wlen = 0;
		err = U_ZERO_ERROR;
		u_strToWCS(dest, static_cast<int32_t>(*destchars), &wlen,
		           reinterpret_cast<const UChar *>(u16.data()), u16len, &err);
		if (U_FAILURE(err))
			return false;
		*destchars = static_cast<size_t>(wlen);
		return true;
	}

	void clearCookie() override
	{
	}

	int detectInputCodepage(int autodetectType, int defcodepage, const char *data, size_t size) override
	{
		(void)autodetectType;
		UErrorCode err = U_ZERO_ERROR;
		UCharsetDetector *det = ucsdet_open(&err);
		if (U_FAILURE(err))
			return defcodepage;
		int result = defcodepage;
		ucsdet_setText(det, data, static_cast<int32_t>(size), &err);
		const UCharsetMatch *match = ucsdet_detect(det, &err);
		if (U_SUCCESS(err) && match != nullptr)
		{
			const char *name = ucsdet_getName(match, &err);
			if (U_SUCCESS(err) && name != nullptr)
			{
				unsigned cp = GetEncodingCodePageFromName(name);
				if (cp != 0)
					result = static_cast<int>(cp);
			}
		}
		ucsdet_close(det);
		return result;
	}

	std::vector<CodePageInfo> enumCodePages() override
	{
		std::vector<CodePageInfo> list;
		const int32_t count = ucnv_countAvailable();
		for (int32_t i = 0; i < count; ++i)
		{
			const char *name = ucnv_getAvailableName(i);
			unsigned cp = GetEncodingCodePageFromName(name);
			if (cp == 0)
				continue;
			CodePageInfo info;
			info.codepage = static_cast<int>(cp);
			info.desc = name;
			list.push_back(std::move(info));
		}
		return list;
	}

	bool getCodepageFromCharsetName(const String& sCharsetName, int& codepage) override
	{
		unsigned cp = GetEncodingCodePageFromName(sCharsetName.c_str());
		if (cp == 0)
			return false;
		codepage = static_cast<int>(cp);
		return true;
	}

	bool getCodepageDescription(int codepage, String& sCharsetName) override
	{
		const char *name = GetEncodingNameFromCodePage(static_cast<unsigned>(codepage));
		if (name == nullptr)
			return false;
		sCharsetName = name;
		return true;
	}

	bool isValidCodepage(int codepage) override
	{
		UConverter *conv = lmOpenConverter(static_cast<unsigned>(codepage));
		if (conv == nullptr)
			return false;
		ucnv_close(conv);
		return true;
	}

	bool getCodePageInfo(int codePage, CodePageInfo *pCodePageInfo) override
	{
		if (pCodePageInfo == nullptr)
			return false;
		pCodePageInfo->codepage = codePage;
		return getCodepageDescription(codePage, pCodePageInfo->desc);
	}
};

} // namespace

IExconverter *Exconverter::getInstance()
{
	static ExconverterICU converter;
	return &converter;
}

#endif // !_WIN32
