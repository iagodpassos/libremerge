// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDate>
#include <QFrame>
#include <QObject>

/**
 * The one request LibreMerge makes of its users: a star on GitHub. It is
 * LibreMerge's own, WinMerge has nothing of the kind, and it is made to
 * stay out of the way:
 *
 * - it is a strip on the "Select Files or Folders" screen, never a window
 *   of its own and never over a comparison;
 * - it waits for someone who has been using the application: on a number
 *   of days, for a number of comparisons;
 * - an answer is final, the button and the close alike; left unanswered,
 *   the strip comes up in a few sessions and then no more;
 * - nothing reaches the network: the button hands the project's page to
 *   the browser, and what is counted stays in the settings.
 *
 * Options > Message Boxes does not bring it back: it is not one of the
 * messages that page hides and shows again.
 */
namespace lm
{

class StarPrompt : public QObject
{
	Q_OBJECT
public:
	static StarPrompt *instance();

	/** The days of use, the comparisons and the sessions it goes by. */
	static constexpr int kDaysOfUse = 5;
	static constexpr int kComparisons = 10;
	static constexpr int kSessions = 3;

	/** The application was started to be used: the day counts, once. */
	void noteAppStarted(const QDate &today = QDate::currentDate());
	/** A comparison was opened. */
	void noteComparisonOpened();

	/** The strip is to show: the application was used enough, no answer
	    was given, and the sessions it shows in are not used up. */
	bool due() const;
	/** The strip came on screen: a session of its few, counted once. */
	void noteShown();
	/** The user answered: never again. */
	void finish();

	/** The strip for a parent, or none when the request is not due. */
	QWidget *createBar(QWidget *parent);

	/** Begin another session, as the next run of the application does
	    (for tests). */
	void startSessionForTest() { m_shownThisSession = false; }

signals:
	/** The user answered: the strips on show go. */
	void finished();

private:
	StarPrompt() = default;

	bool m_shownThisSession = false;
};

} // namespace lm

/** The strip itself: the request, the button to the project's page and
    the close. */
class StarPromptBar : public QFrame
{
	Q_OBJECT
public:
	explicit StarPromptBar(QWidget *parent = nullptr);

protected:
	void paintEvent(QPaintEvent *event) override;
	void showEvent(QShowEvent *event) override;
};
