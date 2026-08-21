// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: character break iterator over UTF-8 text with the
// same interface subset as crystaledit's ICUBreakIterator wrapper. Used when
// tchar_t is char (POSIX builds): the engine's strings hold UTF-8, and the
// ICU wrapper's UChar (UTF-16) view of them would read garbage.
#pragma once

#include <cstdint>

namespace lm_ports
{

class Utf8CharBreakIterator
{
public:
	Utf8CharBreakIterator() : m_text(nullptr), m_len(0), m_i(0) {}

	void setText(const char *text, int32_t len)
	{
		m_text = text;
		m_len = len;
		m_i = 0;
	}

	int first()
	{
		m_i = 0;
		return m_i;
	}

	int next()
	{
		if (m_i >= m_len)
		{
			m_i = m_len;
			return m_len;
		}
		m_i += charLen(m_i);
		if (m_i > m_len)
			m_i = m_len;
		return m_i;
	}

	int previous()
	{
		if (m_i <= 0)
			return -1; // UBRK_DONE
		m_i = leadStart(m_i - 1);
		return m_i;
	}

	int preceding(int32_t offset)
	{
		if (offset <= 0)
		{
			m_i = 0;
			return 0;
		}
		if (offset > m_len)
			offset = m_len;
		m_i = leadStart(offset - 1);
		return m_i;
	}

	int following(int32_t offset)
	{
		if (offset >= m_len)
		{
			m_i = m_len;
			return m_len;
		}
		if (offset < 0)
			offset = 0;
		int start = leadStart(offset);
		m_i = start + charLen(start);
		if (m_i <= offset)
			m_i = offset + 1;
		if (m_i > m_len)
			m_i = m_len;
		return m_i;
	}

	// Same shape as ICUBreakIterator::getCharacterBreakIterator<N>: one
	// thread-local iterator per slot, re-targeted at each call. The pointer
	// parameter arrives as the engine's UChar* cast; it is really UTF-8.
	template<int N = 1>
	static Utf8CharBreakIterator *getCharacterBreakIterator(const void *text, int32_t len)
	{
		static thread_local Utf8CharBreakIterator iter;
		iter.setText(reinterpret_cast<const char *>(text), len);
		return &iter;
	}

private:
	int charLen(int32_t i) const
	{
		const unsigned char ch = static_cast<unsigned char>(m_text[i]);
		if (ch < 0x80) return 1;
		if (ch < 0xC0) return 1; // stray continuation byte
		if (ch < 0xE0) return 2;
		if (ch < 0xF0) return 3;
		if (ch < 0xF8) return 4;
		return 1;
	}

	int32_t leadStart(int32_t i) const
	{
		while (i > 0 && (static_cast<unsigned char>(m_text[i]) & 0xC0) == 0x80)
			--i;
		return i;
	}

	const char *m_text;
	int32_t m_len;
	int32_t m_i;
};

} // namespace lm_ports
