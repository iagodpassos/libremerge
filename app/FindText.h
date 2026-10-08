// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QRegularExpressionMatch>
#include <QString>
#include <QStringList>
#include <map>

/**
 * WinMerge's text search, as its editor does it: Crystal Edit's
 * FindTextHelper (how a line is searched, the regular expressions and
 * their replacements), the history of the search fields (CMemComboBox)
 * and the markers that show where texts are (CCrystalTextMarkers): the
 * text last searched for and the user's own.
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
 * The markers (CCrystalTextMarkers): texts marked wherever they show, in
 * every pane of every file window. Every search sets the one of the text
 * searched for ("EDITOR_MARKER"), found or not, with the search's flags;
 * the Marker dialog keeps the user's own, each with one of three colors,
 * shown or not, and the switch that turns them all off, the search's
 * too. A marker is drawn over the differences' colors, the user's over
 * the search's, each over those made before it.
 */
class TextMarkers : public QObject
{
	Q_OBJECT
public:
	/** The marker colors, upstream's COLORINDEX_MARKERBKGND0 to 3: the
	    search's, then the three the Marker dialog offers. */
	enum Color
	{
		SearchColor = 0,
		MarkerColor1,
		MarkerColor2,
		MarkerColor3,
	};
	struct Marker
	{
		QString findWhat;
		unsigned flags = 0;
		int color = SearchColor;
		bool userDefined = false;
		bool visible = false;
	};
	/** By key, in the order they are drawn in (std::map, as upstream's). */
	using Map = std::map<QString, Marker>;
	/** A stretch of a line a marker marks, in the marker's color. */
	struct Stretch
	{
		int start = 0;
		int length = 0;
		int color = SearchColor;
	};

	static TextMarkers *instance();
	/** The key of the search's marker. */
	static QString searchKey();
	/** MakeNewId: "MARKER" and the next number, four wide. */
	static QString makeNewId(const Map &markers);

	/** SetMarker; the views are told only when the markers are on. */
	void setMarker(const QString &key, const QString &findWhat, unsigned flags,
		int color, bool userDefined = true, bool visible = true);
	void deleteMarker(const QString &key);
	const Map &markers() const { return m_markers; }
	bool enabled() const { return m_enabled; }
	/** The Marker dialog's Apply: its markers, and whether they show. */
	void setMarkers(const Map &markers, bool enabled);
	/** GetMarkerTextBlocks: what the markers that show mark in a line, in
	    the order they are drawn; nothing when they are off. */
	QList<Stretch> stretches(const QString &line) const;

	/** SaveToRegistry and LoadFromRegistry: the user's markers and the
	    switch, in the settings. */
	void save() const;
	void load();

signals:
	/** UpdateViews: the panes draw the markers again. */
	void changed();

private:
	TextMarkers();
	Map m_markers;
	bool m_enabled = true;
};

} // namespace lm
