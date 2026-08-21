// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: Unicode normalization via ICU, replacing the
// Win32 NormalizeString (normaliz.dll) path used by ucr::normalizeString.
// The form values follow the Win32 NORM_FORM enum that the engine passes:
// C = 0x1, D = 0x2, KC = 0x5, KD = 0x6.
#ifndef _WIN32

#include <vector>
#include "UnicodeString.h"
#include <unicode/normalizer2.h>
#include <unicode/unistr.h>
#include <unicode/translit.h>

namespace lm_ports
{

String normalizeUtf8(const String& str, int winNormForm)
{
	UErrorCode err = U_ZERO_ERROR;
	const icu::Normalizer2 *norm = nullptr;
	switch (winNormForm)
	{
	case 0x1: norm = icu::Normalizer2::getNFCInstance(err); break;
	case 0x2: norm = icu::Normalizer2::getNFDInstance(err); break;
	case 0x5: norm = icu::Normalizer2::getNFKCInstance(err); break;
	case 0x6: norm = icu::Normalizer2::getNFKDInstance(err); break;
	default: return str;
	}
	if (U_FAILURE(err) || norm == nullptr)
		return str;

	icu::UnicodeString input = icu::UnicodeString::fromUTF8(
		icu::StringPiece(str.data(), static_cast<int32_t>(str.size())));
	icu::UnicodeString normalized = norm->normalize(input, err);
	if (U_FAILURE(err))
		return str;

	std::string out;
	normalized.toUTF8String(out);
	return out;
}

String icuToUpper(const String& s)
{
	icu::UnicodeString u = icu::UnicodeString::fromUTF8(
		icu::StringPiece(s.data(), static_cast<int32_t>(s.size())));
	u.toUpper();
	std::string out;
	u.toUTF8String(out);
	return out;
}

String icuToLower(const String& s)
{
	icu::UnicodeString u = icu::UnicodeString::fromUTF8(
		icu::StringPiece(s.data(), static_cast<int32_t>(s.size())));
	u.toLower();
	std::string out;
	u.toUTF8String(out);
	return out;
}

String icuTransliterate(const String& s, const char *translitId)
{
	UErrorCode err = U_ZERO_ERROR;
	std::unique_ptr<icu::Transliterator> translit(
		icu::Transliterator::createInstance(translitId, UTRANS_FORWARD, err));
	if (U_FAILURE(err) || translit == nullptr)
		return s;
	icu::UnicodeString u = icu::UnicodeString::fromUTF8(
		icu::StringPiece(s.data(), static_cast<int32_t>(s.size())));
	translit->transliterate(u);
	std::string out;
	u.toUTF8String(out);
	return out;
}

} // namespace lm_ports

#endif // !_WIN32
