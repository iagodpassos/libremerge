// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QRegularExpressionMatch>
#include <QString>
#include <QStringList>

/**
 * WinMerge's text search, as its editor does it: Crystal Edit's
 * FindTextHelper (how a line is searched, the regular expressions and
 * their replacements), the history of the search fields (CMemComboBox)
 * and the marker that shows every occurrence of the text last searched
 * for (CCrystalTextMarkers' "EDITOR_MARKER").
 */
namespace lm
{

/** The flags of a search (findtext_flags_t), with upstream's values: the
    settings keep them as they are. */
enum FindFlag : unsigned
{
	FindMatchCase = 0x0001,
	FindWholeWord = 0x0002,
	FindRegExp = 0x0004,
	FindDirectionUp = 0x0010,
	FindReplaceSelection = 0x0100,
	FindNoWrap = 0x0200,
	FindNoClose = 0x0400,
};

/** LastSearchInfos: a search's settings, as the dialogs keep them. */
struct LastSearchInfos
{
	int direction = 1; ///< 0 up, 1 down (searches only)
	bool noWrap = false;
	bool matchCase = false;
	QString text;
	bool wholeWord = false;
	bool regExp = false;
	bool noClose = false;
};

/** ConvertSearchInfosToSearchFlags and ConvertSearchFlagsToLastSearchInfos. */
unsigned searchInfosToFlags(const LastSearchInfos &infos);
void flagsToSearchInfos(LastSearchInfos *infos, unsigned flags);

/** A place in a pane's text (CEPoint): a line of the view and a column in
    it, in UTF-16 units. */
struct TextPoint
{
	int x = 0;
	int y = 0;

	friend bool operator==(const TextPoint &a, const TextPoint &b)
	{
		return a.x == b.x && a.y == b.y;
	}
	friend bool operator!=(const TextPoint &a, const TextPoint &b)
	{
		return !(a == b);
	}
	/** On an earlier line, or earlier on the same one. */
	friend bool operator<(const TextPoint &a, const TextPoint &b)
	{
		return a.y < b.y || (a.y == b.y && a.x < b.x);
	}
};

/**
 * FindStringHelper: where the text is in a line, searched from a column
 * on, -1 when it is not. *length gets the length of what was found; a
 * plain search sets it whatever it finds, as upstream's does. A regular
 * expression (PCRE, as upstream's) gives its match to *match, for the
 * replacement, and finds nothing when it does not compile.
 *
 * end, when given, cuts the line there: a match has to end before it.
 * Upstream cuts the string it searches; "Match whole word only" looks
 * past the cut here, at the line's own next character, and at the
 * character before the match even when the search starts on the match,
 * where upstream takes a word's middle for a whole word.
 */
int findStringHelper(const QString &line, int from, const QString &what,
	unsigned flags, int *length, QRegularExpressionMatch *match = nullptr,
	int end = -1);

/** HowManyEOLs: the line breaks in a text (CR LF counts once). */
int howManyEols(QStringView text);

/**
 * RxReplace: the text that replaces a regular expression's match. \0 to
 * \9 stand for its groups, \r \n \t \a \b for their characters, \xHH,
 * \dDDD and \oOOO for a character by its code; \U and \L turn what
 * follows to upper or lower case until \E, \u and \l only its first
 * character. False where upstream gives up and nothing is put in: a
 * group the match does not have, or a lone "\" at the end. A code with
 * a wrong digit makes the replacement empty, as upstream's.
 */
bool rxReplace(const QString &replacement, const QRegularExpressionMatch &match,
	QString *result);

/** The texts typed in the search fields, newest first (CMemComboBox's
    groups "FindText" and "ReplaceText"): up to 64, a text that is there
    already, case aside, moving to the top. Fill keeps them for the
    session, Save writes every group to the settings. */
QStringList searchHistory(const QString &group);
void fillSearchHistory(const QString &group, const QString &text);
void saveSearchHistory();
void resetSearchHistoryForTest();

/**
 * The text last searched for, marked wherever it shows in every pane of
 * every file window: every search sets it, found or not, with the
 * search's flags. Upstream's marker dialog (Edit > Marker...), which
 * turns markers off and clears them, is not here.
 */
class SearchMarker : public QObject
{
	Q_OBJECT
public:
	static SearchMarker *instance();

	void setMarker(const QString &text, unsigned flags);
	QString text() const { return m_text; }
	unsigned flags() const { return m_flags; }
	bool isSet() const { return !m_text.isEmpty(); }
	/** What it marks in a line (GetMarkerTextBlocks): the start and the
	    length of each stretch. */
	QList<QPair<int, int>> stretches(const QString &line) const;
	void clearForTest();

signals:
	void changed();

private:
	SearchMarker() = default;
	QString m_text;
	unsigned m_flags = 0;
};

} // namespace lm
