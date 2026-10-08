// SPDX-License-Identifier: GPL-3.0-or-later
#include "FindText.h"

#include <QHash>
#include <QRegularExpression>
#include <QSettings>

namespace
{

/** xisalnum: a letter or digit, "_", or any character past ASCII. */
bool isWordChar(QChar c)
{
	const ushort u = c.unicode();
	if (u > 0x7f || u == '_')
		return true;
	return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z');
}

/** Upstream compiles the expression at every search; the last few are
    kept here instead. */
const QRegularExpression &compiled(const QString &pattern, bool matchCase)
{
	static QHash<QString, QRegularExpression> cache;
	const QString key = (matchCase ? QLatin1Char('1') : QLatin1Char('0')) + pattern;
	auto it = cache.constFind(key);
	if (it != cache.constEnd())
		return it.value();
	if (cache.size() >= 16)
		cache.clear();
	return cache.insert(key, QRegularExpression(pattern, matchCase
		? QRegularExpression::NoPatternOption
		: QRegularExpression::CaseInsensitiveOption)).value();
}

const QString kSection = QStringLiteral("Editor/");
constexpr int kRememberCount = 64;

struct History
{
	QHash<QString, QStringList> groups;
	bool loaded = false;
};

History &history()
{
	static History store;
	if (!store.loaded)
	{
		store.loaded = true;
		QSettings settings;
		for (const QString &group : { QStringLiteral("FindText"), QStringLiteral("ReplaceText") })
		{
			const QStringList items = settings.value(kSection + group).toStringList();
			if (!items.isEmpty())
				store.groups.insert(group, items);
		}
	}
	return store;
}

} // namespace

namespace lm
{

unsigned searchInfosToFlags(const LastSearchInfos &infos)
{
	unsigned flags = 0;
	if (infos.matchCase)
		flags |= FindMatchCase;
	if (infos.wholeWord)
		flags |= FindWholeWord;
	if (infos.regExp)
		flags |= FindRegExp;
	if (infos.direction == 0)
		flags |= FindDirectionUp;
	if (infos.noWrap)
		flags |= FindNoWrap;
	if (infos.noClose)
		flags |= FindNoClose;
	return flags;
}

void flagsToSearchInfos(LastSearchInfos *infos, unsigned flags)
{
	infos->matchCase = (flags & FindMatchCase) != 0;
	infos->wholeWord = (flags & FindWholeWord) != 0;
	infos->regExp = (flags & FindRegExp) != 0;
	infos->direction = (flags & FindDirectionUp) == 0 ? 1 : 0;
	infos->noWrap = (flags & FindNoWrap) != 0;
	infos->noClose = (flags & FindNoClose) != 0;
}

int findStringHelper(const QString &line, int from, const QString &what,
	unsigned flags, int *length, QRegularExpressionMatch *match, int end)
{
	const int limit = end < 0 ? line.size() : qMin(end, static_cast<int>(line.size()));
	from = qBound(0, from, limit);
	if (flags & FindRegExp)
	{
		// an expression that starts with "^" matches at the line's start only
		if (what.startsWith(QLatin1Char('^')) && from != 0)
			return -1;
		const QRegularExpression &expression = compiled(what, (flags & FindMatchCase) != 0);
		if (!expression.isValid())
			return -1;
		const QString subject = limit < line.size() ? line.left(limit) : line;
		const QRegularExpressionMatch found = expression.match(subject, from);
		if (!found.hasMatch())
			return -1;
		int pos = static_cast<int>(found.capturedStart(0));
		int len = static_cast<int>(found.capturedLength(0));
		// a match that starts on the LF of a CR LF takes the CR in
		if (pos >= 1 && pos < subject.size() && subject.at(pos) == QLatin1Char('\n')
			&& subject.at(pos - 1) == QLatin1Char('\r'))
		{
			--pos;
			++len;
		}
		*length = len;
		if (match != nullptr)
			*match = found;
		return pos;
	}

	const int n = static_cast<int>(what.size());
	*length = n;
	if (n == 0)
		return -1;
	const Qt::CaseSensitivity cs = (flags & FindMatchCase) ? Qt::CaseSensitive
		: Qt::CaseInsensitive;
	const QStringView searched = QStringView(line).left(limit);
	for (int start = from;;)
	{
		const int pos = static_cast<int>(searched.indexOf(what, start, cs));
		if (pos < 0)
			return -1;
		if ((flags & FindWholeWord) == 0)
			return pos;
		if ((pos > 0 && isWordChar(line.at(pos - 1)))
			|| (pos + n < line.size() && isWordChar(line.at(pos + n))))
		{
			start = pos + 1;
			continue;
		}
		return pos;
	}
}

int howManyEols(QStringView text)
{
	int count = 0;
	bool wasCr = false;
	for (const QChar c : text)
	{
		if (c == QLatin1Char('\n'))
		{
			if (wasCr)
				wasCr = false;
			else
				++count;
		}
		else if (c == QLatin1Char('\r'))
		{
			++count;
			wasCr = true;
		}
		else
			wasCr = false;
	}
	return count;
}

bool rxReplace(const QString &replacement, const QRegularExpressionMatch &match,
	QString *result)
{
	enum { UpCase = 1, DownCase = 2, UpNext = 4, DownNext = 8 };
	QString dest;
	int flag = 0;
	const auto add = [&dest, &flag](QString piece) {
		if (piece.isEmpty())
			return;
		if (flag & UpCase)
		{
			for (QChar &c : piece)
				c = c.toUpper();
		}
		else if (flag & DownCase)
		{
			for (QChar &c : piece)
				c = c.toLower();
		}
		if (flag & UpNext)
		{
			piece[0] = piece.at(0).toUpper();
			flag &= ~UpNext;
		}
		else if (flag & DownNext)
		{
			piece[0] = piece.at(0).toLower();
			flag &= ~DownNext;
		}
		dest += piece;
	};
	// \xHH, \dDDD, \oOOO: a character by the digits that follow
	const auto code = [&replacement](int *at, int digits, int base, int *value) {
		*value = 0;
		for (int k = 0; k < digits; ++k)
		{
			if (*at >= replacement.size())
				return false;
			const int digit = QString(replacement.at(*at)).toInt(nullptr, 16);
			const QChar c = replacement.at(*at).toUpper();
			const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
				|| (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
			if (!hex || digit >= base)
				return false;
			*value = *value * base + digit;
			++*at;
		}
		return true;
	};

	int i = 0;
	while (i < replacement.size())
	{
		const QChar ch = replacement.at(i++);
		if (ch != QLatin1Char('\\'))
		{
			add(QString(ch));
			continue;
		}
		if (i >= replacement.size())
			return false; // a "\" with nothing after it
		const QChar escaped = replacement.at(i++);
		const ushort e = escaped.unicode();
		if (e >= '0' && e <= '9')
		{
			const int group = e - '0';
			if (match.capturedStart(group) < 0)
				return false;
			add(match.captured(group));
			continue;
		}
		int value = 0;
		switch (e)
		{
		case 'r': add(QStringLiteral("\r")); break;
		case 'n': add(QStringLiteral("\n")); break;
		case 'b': add(QString(QChar(0x08))); break;
		case 'a': add(QString(QChar(0x07))); break;
		case 't': add(QStringLiteral("\t")); break;
		case 'U': flag |= UpCase; break;
		case 'u': flag |= UpNext; break;
		case 'L': flag |= DownCase; break;
		case 'l': flag |= DownNext; break;
		case 'E':
		case 'e': flag &= ~(UpCase | DownCase); break;
		case 'x':
		case 'd':
		case 'o':
			if (!code(&i, e == 'x' ? 2 : 3, e == 'x' ? 16 : e == 'd' ? 10 : 8, &value))
			{
				result->clear();
				return true;
			}
			add(QString(QChar(static_cast<ushort>(value))));
			break;
		default:
			add(QString(escaped));
			break;
		}
	}
	*result = dest;
	return true;
}

QStringList searchHistory(const QString &group)
{
	return history().groups.value(group);
}

void fillSearchHistory(const QString &group, const QString &text)
{
	if (text.isEmpty())
		return;
	QStringList &items = history().groups[group];
	// FindStringExact, which leaves case aside
	for (int i = 0; i < items.size(); ++i)
	{
		if (items.at(i).compare(text, Qt::CaseInsensitive) == 0)
		{
			items.removeAt(i);
			break;
		}
	}
	items.prepend(text);
	while (items.size() > kRememberCount)
		items.removeLast();
}

void saveSearchHistory()
{
	QSettings settings;
	const History &store = history();
	for (auto it = store.groups.cbegin(); it != store.groups.cend(); ++it)
		settings.setValue(kSection + it.key(), it.value());
}

void resetSearchHistoryForTest()
{
	History &store = history();
	store.groups.clear();
	QSettings settings;
	settings.remove(kSection + QStringLiteral("FindText"));
	settings.remove(kSection + QStringLiteral("ReplaceText"));
}

SearchMarker *SearchMarker::instance()
{
	static SearchMarker *marker = new SearchMarker;
	return marker;
}

void SearchMarker::setMarker(const QString &text, unsigned flags)
{
	if (text == m_text && flags == m_flags)
		return;
	m_text = text;
	m_flags = flags;
	emit changed();
}

QList<QPair<int, int>> SearchMarker::stretches(const QString &line) const
{
	QList<QPair<int, int>> found;
	if (!isSet())
		return found;
	for (int at = 0; at < line.size();)
	{
		int length = 0;
		const int pos = findStringHelper(line, at, m_text, m_flags | FindNoWrap, &length);
		if (pos < 0)
			break;
		const int shown = qMin(length, static_cast<int>(line.size()) - pos);
		if (shown > 0)
			found.append({ pos, shown });
		at = pos + (length == 0 ? 1 : length);
	}
	return found;
}

void SearchMarker::clearForTest()
{
	setMarker(QString(), 0);
}

} // namespace lm
