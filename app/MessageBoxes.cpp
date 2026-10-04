// SPDX-License-Identifier: GPL-3.0-or-later
#include "MessageBoxes.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFile>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QSettings>
#include <QtConcurrent/QtConcurrent>

namespace
{

const QString kGroup = QStringLiteral("MessageBoxes/");
const QString kFilesSame = QStringLiteral("FilesSame");         // IDS_FILESSAME
const QString kFileToItself = QStringLiteral("FileToItself");   // IDS_FILE_TO_ITSELF
const QString kFileChangedRescan = QStringLiteral("FileChangedRescan"); // IDS_FILECHANGED_RESCAN

std::function<void(const QString &)> &sink()
{
	static std::function<void(const QString &)> function;
	return function;
}

std::function<bool(const QString &, bool *)> &questionSink()
{
	static std::function<bool(const QString &, bool *)> function;
	return function;
}

QString filesSameText()
{
	return QCoreApplication::translate("MessageBoxes", "Selected files are identical.");
}

QString fileToItselfText()
{
	return QCoreApplication::translate("MessageBoxes", "Same file is opened in both panes.");
}

QString fileChangedRescanText()
{
	return QCoreApplication::translate("MessageBoxes",
		"Another application updated\n%1\nsince last scan.\n\nReload?");
}

/** A Yes/No warning box. With a key it carries WinMerge's "Don't ask this
    question again" (MB_DONT_ASK_AGAIN): ticked, the answer given is kept
    and comes back without asking. */
bool askYesNo(QWidget *parent, const QString &text, const QString &key = QString())
{
	if (!key.isEmpty())
	{
		const lm::MessageAnswer remembered = lm::rememberedAnswer(key);
		if (remembered == lm::AnswerYes || remembered == lm::AnswerNo)
			return remembered == lm::AnswerYes;
	}
	bool yes = false;
	bool dontAskAgain = false;
	if (questionSink())
	{
		yes = questionSink()(text, &dontAskAgain);
	}
	else
	{
		QMessageBox box(QMessageBox::Warning, QStringLiteral("LibreMerge"), text,
			QMessageBox::Yes | QMessageBox::No, parent);
		box.setWindowModality(Qt::WindowModal);
		QCheckBox *dontAsk = nullptr;
		if (!key.isEmpty())
		{
			dontAsk = new QCheckBox(QCoreApplication::translate("MessageBoxes",
				"Don't ask this question again."), &box);
			box.setCheckBox(dontAsk);
		}
		yes = box.exec() == QMessageBox::Yes;
		dontAskAgain = dontAsk != nullptr && dontAsk->isChecked();
	}
	if (!key.isEmpty() && dontAskAgain)
		lm::setRememberedAnswer(key, yes ? lm::AnswerYes : lm::AnswerNo);
	return yes;
}

enum class Identity { Same, Different, Failed };

/** Byte by byte, like the BinaryCompare of WinMerge's AsyncCompareTask. */
Identity byteIdentity(const QStringList &paths)
{
	QFile first(paths.value(0));
	if (!first.open(QIODevice::ReadOnly))
		return Identity::Failed;
	for (int i = 1; i < paths.size(); ++i)
	{
		QFile other(paths.at(i));
		if (!other.open(QIODevice::ReadOnly))
			return Identity::Failed;
		if (other.size() != first.size())
			return Identity::Different;
		first.seek(0);
		while (!first.atEnd())
		{
			const QByteArray a = first.read(1 << 16);
			if (a != other.read(a.size()))
				return Identity::Different;
		}
	}
	return Identity::Same;
}

QString identityText(Identity identity)
{
	switch (identity)
	{
	case Identity::Same:
		return QCoreApplication::translate("MessageBoxes", "Selected files are identical (binary match).");
	case Identity::Different:
		return QCoreApplication::translate("MessageBoxes", "Selected files are identical (with current settings).\n"
			"But differ at the binary level.");
	case Identity::Failed:
		break;
	}
	return QCoreApplication::translate("MessageBoxes", "Selected files are identical (with current settings).\n"
		"But binary comparison failed.");
}

/** An information box with WinMerge's "Don't display this message
    again": ticking it hides the message under its key. While a check is
    pending the box shows its first text and switches on completion. */
void showHideable(QWidget *parent, const QString &key, const QString &text,
	QFuture<Identity> *pending = nullptr)
{
	if (sink())
	{
		sink()(pending != nullptr ? identityText(pending->result()) : text);
		return;
	}
	QMessageBox box(QMessageBox::Information, QStringLiteral("LibreMerge"), text,
		QMessageBox::Ok, parent);
	box.setWindowModality(Qt::WindowModal);
	auto *dontShow = new QCheckBox(QCoreApplication::translate("MessageBoxes", "Don't display this message again."), &box);
	box.setCheckBox(dontShow);
	QFutureWatcher<Identity> watcher;
	if (pending != nullptr)
	{
		QObject::connect(&watcher, &QFutureWatcher<Identity>::finished, &box,
			[&box, &watcher]() { box.setText(identityText(watcher.result())); });
		watcher.setFuture(*pending);
	}
	box.exec();
	if (dontShow->isChecked())
		lm::setMessageHidden(key, true);
}

} // namespace

namespace lm
{

QString answerText(MessageAnswer answer)
{
	switch (answer)
	{
	case AnswerOk: return QCoreApplication::translate("MessageBoxes", "OK");
	case AnswerYes: return QCoreApplication::translate("MessageBoxes", "Yes");
	case AnswerNo: return QCoreApplication::translate("MessageBoxes", "No");
	case NoAnswer: break;
	}
	return QString();
}

QList<HideableMessage> hideableMessages()
{
	// WinMerge's order (PropMessageBoxes.cpp)
	return {
		{ kFilesSame, filesSameText(), { AnswerOk } },
		{ kFileToItself, fileToItselfText(), { AnswerOk } },
		{ kFileChangedRescan, fileChangedRescanText(), { AnswerYes, AnswerNo } },
	};
}

MessageAnswer rememberedAnswer(const QString &key)
{
	const QVariant value = QSettings().value(kGroup + key);
	if (!value.isValid())
		return NoAnswer;
	// 0.9.7 kept a hidden information box as a plain "true"
	const QString text = value.toString();
	if (text == QStringLiteral("true"))
		return AnswerOk;
	switch (text.toInt())
	{
	case AnswerOk: return AnswerOk;
	case AnswerYes: return AnswerYes;
	case AnswerNo: return AnswerNo;
	default: break;
	}
	return NoAnswer;
}

void setRememberedAnswer(const QString &key, MessageAnswer answer)
{
	QSettings settings;
	if (answer != NoAnswer)
		settings.setValue(kGroup + key, static_cast<int>(answer));
	else
		settings.remove(kGroup + key);
}

bool messageHidden(const QString &key)
{
	return rememberedAnswer(key) != NoAnswer;
}

void setMessageHidden(const QString &key, bool hidden)
{
	setRememberedAnswer(key, hidden ? AnswerOk : NoAnswer);
}

void resetHiddenMessages()
{
	for (const HideableMessage &message : hideableMessages())
		setRememberedAnswer(message.key, NoAnswer);
}

void showIdenticalMessage(QWidget *parent, const QStringList &paths,
	bool exactCheck)
{
	// the same path in two panes: a message of its own, so it can be
	// hidden alone (a case-insensitive match, as upstream's)
	bool samePath = false;
	for (int i = 0; i < paths.size() && !samePath; ++i)
		for (int j = i + 1; j < paths.size() && !samePath; ++j)
			samePath = !paths.at(i).isEmpty()
				&& paths.at(i).compare(paths.at(j), Qt::CaseInsensitive) == 0;
	if (samePath)
	{
		if (!messageHidden(kFileToItself))
			showHideable(parent, kFileToItself, fileToItselfText());
		return;
	}
	if (messageHidden(kFilesSame))
		return;
	if (!exactCheck)
	{
		showHideable(parent, kFilesSame, filesSameText());
		return;
	}
	QFuture<Identity> check = QtConcurrent::run([paths]() { return byteIdentity(paths); });
	if (sink())
		check.waitForFinished();
	showHideable(parent, kFilesSame,
		QCoreApplication::translate("MessageBoxes", "Selected files are identical (with current settings).\n"
			"Checking binary identity..."), &check);
}

void showInformation(QWidget *parent, const QString &text)
{
	if (sink())
	{
		sink()(text);
		return;
	}
	QMessageBox box(QMessageBox::Information, QStringLiteral("LibreMerge"), text,
		QMessageBox::Ok, parent);
	box.setWindowModality(Qt::WindowModal);
	box.exec();
}

void showWarning(QWidget *parent, const QString &text)
{
	if (sink())
	{
		sink()(text);
		return;
	}
	QMessageBox box(QMessageBox::Warning, QStringLiteral("LibreMerge"), text,
		QMessageBox::Ok, parent);
	box.setWindowModality(Qt::WindowModal);
	box.exec();
}

void showError(QWidget *parent, const QString &text)
{
	if (sink())
	{
		sink()(text);
		return;
	}
	QMessageBox box(QMessageBox::Critical, QStringLiteral("LibreMerge"), text,
		QMessageBox::Ok, parent);
	box.setWindowModality(Qt::WindowModal);
	box.exec();
}

bool askWarning(QWidget *parent, const QString &text)
{
	return askYesNo(parent, text);
}

bool askReloadChangedFile(QWidget *parent, const QString &path)
{
	return askYesNo(parent, fileChangedRescanText().arg(path), kFileChangedRescan);
}

bool askOverwriteChangedFile(QWidget *parent, const QString &path)
{
	return askYesNo(parent, QCoreApplication::translate("MessageBoxes",
		"Another application updated\n%1\nsince LibreMerge loaded it.\n\nOverwrite?")
		.arg(path));
}

bool askRefreshFolderCompares(QWidget *parent)
{
	return askYesNo(parent, QCoreApplication::translate("MessageBoxes",
		"Filters updated. Refresh all open folder compares?\n\n"
		"Select 'No' to refresh later."));
}

void setMessageSinkForTest(std::function<void(const QString &)> function)
{
	sink() = std::move(function);
}

void setQuestionSinkForTest(std::function<bool(const QString &, bool *)> function)
{
	questionSink() = std::move(function);
}

} // namespace lm
