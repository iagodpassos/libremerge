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
    since the ignore options may hide byte differences. always (the
    command line's -x) says so even when the message was hidden, with no
    box to hide it. */
void showIdenticalMessage(QWidget *parent, const QStringList &paths,
	bool exactCheck, bool always = false);

/** CEditReplaceDlg::OnEditReplaceAll's message: how many strings Replace
    All replaced. "Don't display this message again" hides it. */
void showNumReplaced(QWidget *parent, int count);

/** A plain information box, window-modal (sheets on macOS). */
void showInformation(QWidget *parent, const QString &text);
/** The same with the warning icon (MB_OK | MB_ICONEXCLAMATION). */
void showWarning(QWidget *parent, const QString &text);
/** The same with the error icon (MB_ICONSTOP, MB_ICONERROR). */
void showError(QWidget *parent, const QString &text);
/** A Yes/No warning box (MB_ICONWARNING | MB_YESNO): true for Yes. In
    tests the question sink answers. */
bool askWarning(QWidget *parent, const QString &text);

/** CMergeDoc::CheckFileChanged's question: another application changed a
    compared file since it was loaded, reload it? "Don't ask this question
    again" keeps the answer, so Yes reloads silently from then on. */
bool askReloadChangedFile(QWidget *parent, const QString &path);
/** CreateFoldersPair's question when a folder of a comparison without
    its subfolders is on one side only: create the matching one on the
    other side and open the two? "Don't ask this question again" keeps the
    answer. */
bool askCreatePairFolder(QWidget *parent, const QString &path);
/** CMergeDoc::OpenDocs' question when the files' lines end otherwise
    (IDS_SUGGEST_IGNOREEOL): treat every line ending as the same? With
    "Don't ask this question again" the answer is kept, and remembered
    says so. */
bool askSuggestIgnoreEol(QWidget *parent, bool *remembered);
/** WinMerge's question before Copy All (IDS_CONFIRM_COPY_ALL_DIFFS), No
    the default, with "Don't ask this question again". */
bool askCopyAllDiffs(QWidget *parent);
/** A plain Yes/No question (upstream's MB_YESNO box). */
bool askQuestion(QWidget *parent, const QString &text);
/** CMergeDoc::DoSave's question before writing over a file another
    application changed since it was loaded. */
bool askOverwriteChangedFile(QWidget *parent, const QString &path);
/** CMainFrame::OnToolsFilters' question when the filters were changed
    with a folder comparison in front: refresh the open folder comparisons
    now, or leave it for later? */
bool askRefreshFolderCompares(QWidget *parent);

/** An unattended run (the command line's -noninteractive): no message
    box opens, each one's text goes to the terminal and a question is
    answered Cancel (CMergeApp::ShowMessageBox, OutputConsole). */
void setUnattended(bool on);
bool unattended();
void reportUnattended(const QString &text, bool question);

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
