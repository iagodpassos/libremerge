// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

/**
 * Message boxes the user can hide with "Don't display this message again"
 * or "Don't ask this question again", WinMerge's MB_DONT_DISPLAY_AGAIN and
 * MB_DONT_ASK_AGAIN (CMessageBoxDialog), and the ones built on them.
 * Options > Message Boxes lists and resets them.
 */
namespace lm
{

/** What a hidden message box answers by itself from then on; the values
    are WinMerge's IDOK, IDYES and IDNO, as it stores them. */
enum MessageAnswer
{
	NoAnswer = 0, ///< not hidden: the box is shown
	AnswerOk = 1,
	AnswerYes = 6,
	AnswerNo = 7,
};
/** "OK", "Yes" or "No", as the Options page shows the answer. */
QString answerText(MessageAnswer answer);

struct HideableMessage
{
	QString key;  ///< where the answer is kept, under "MessageBoxes/"
	QString text; ///< the message, as the Options page lists it
	/** What it can answer once hidden: OK for an information box, Yes
	    or No for a question. */
	QList<MessageAnswer> answers;
};

/** The hideable messages, in the order the Options page lists them. */
QList<HideableMessage> hideableMessages();
MessageAnswer rememberedAnswer(const QString &key);
void setRememberedAnswer(const QString &key, MessageAnswer answer);
/** An information box hidden with "Don't display this message again". */
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

/** CMergeDoc::CheckFileChanged's question: another application changed a
    compared file since it was loaded, reload it? "Don't ask this question
    again" keeps the answer, so Yes reloads silently from then on. */
bool askReloadChangedFile(QWidget *parent, const QString &path);
/** CMergeDoc::DoSave's question before writing over a file another
    application changed since it was loaded. */
bool askOverwriteChangedFile(QWidget *parent, const QString &path);
/** CMainFrame::OnToolsFilters' question when the filters were changed
    with a folder comparison in front: refresh the open folder comparisons
    now, or leave it for later? */
bool askRefreshFolderCompares(QWidget *parent);

/** Collect the messages instead of showing them (for tests; pass an
    empty function to show them again). */
void setMessageSinkForTest(std::function<void(const QString &)> sink);
/** Answer the questions instead of asking them (for tests): called with
    the question's text, returns true for Yes and may tick "Don't ask this
    question again" through the pointer. A remembered answer still comes
    first. */
void setQuestionSinkForTest(
	std::function<bool(const QString &text, bool *dontAskAgain)> sink);

} // namespace lm
