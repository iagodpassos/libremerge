// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

/**
 * Message boxes the user can hide with "Don't display this message again",
 * WinMerge's MB_DONT_DISPLAY_AGAIN (CMessageBoxDialog), and the ones built
 * on them. Options > Message Boxes lists and resets them.
 */
namespace lm
{

struct HideableMessage
{
	QString key;  ///< where the answer is kept, under "MessageBoxes/"
	QString text; ///< the message, as the Options page lists it
};

/** The hideable messages, in the order the Options page lists them. */
QList<HideableMessage> hideableMessages();
bool messageHidden(const QString &key);
void setMessageHidden(const QString &key, bool hidden);
/** Show every message again (the page's Reset button). */
void resetHiddenMessages();

/** CMergeFrameCommon::ShowIdenticalMessage: tell that the compared files
    are identical, or that the same file sits in two panes. exactCheck
    (every pane saved to disk) adds WinMerge's binary identity check,
    since the ignore options may hide byte differences. */
void showIdenticalMessage(QWidget *parent, const QStringList &paths,
	bool exactCheck);

/** A plain information box, window-modal (sheets on macOS). */
void showInformation(QWidget *parent, const QString &text);

/** Collect the messages instead of showing them (for tests; pass an
    empty function to show them again). */
void setMessageSinkForTest(std::function<void(const QString &)> sink);

} // namespace lm
