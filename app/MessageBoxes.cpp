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

std::function<void(const QString &)> &sink()
{
	static std::function<void(const QString &)> function;
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

QList<HideableMessage> hideableMessages()
{
	return { { kFilesSame, filesSameText() }, { kFileToItself, fileToItselfText() } };
}

bool messageHidden(const QString &key)
{
	return QSettings().value(kGroup + key, false).toBool();
}

void setMessageHidden(const QString &key, bool hidden)
{
	QSettings settings;
	if (hidden)
		settings.setValue(kGroup + key, true);
	else
		settings.remove(kGroup + key);
}

void resetHiddenMessages()
{
	for (const HideableMessage &message : hideableMessages())
		setMessageHidden(message.key, false);
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

void setMessageSinkForTest(std::function<void(const QString &)> function)
{
	sink() = std::move(function);
}

} // namespace lm
