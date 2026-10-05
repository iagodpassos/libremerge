// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReplaceLists.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include "MessageBoxes.h"

namespace
{

QString &baseOverride()
{
	static QString base;
	return base;
}

std::function<QString(const QString &, bool)> &chooserStandIn()
{
	static std::function<QString(const QString &, bool)> chooser;
	return chooser;
}

std::function<void(const QString &)> &openerStandIn()
{
	static std::function<void(const QString &)> opener;
	return opener;
}

QString translated(const char *text)
{
	return QCoreApplication::translate("ReplaceLists", text);
}

} // namespace

namespace lm
{

QString replaceListFolder(bool regex)
{
	QString base = baseOverride();
	if (base.isEmpty())
	{
		// next to the settings, as the user's own file filters are
#ifdef Q_OS_MACOS
		const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#else
		const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
#endif
		base = QDir(data).filePath(QCoreApplication::organizationName());
	}
	return QDir(base).filePath(regex ? QStringLiteral("RegexReplaceLists")
		: QStringLiteral("ReplaceLists"));
}

QStringList replaceLists(bool regex)
{
	const QString folder = replaceListFolder(regex);
	QDir dir(folder);
	if (!dir.exists() && !QDir().mkpath(folder))
		return {};
	QStringList lists;
	for (const QString &name : dir.entryList(QDir::Files, QDir::Name))
		lists.append(dir.filePath(name));
	return lists;
}

bool createReplaceListTemplate(const QString &path, bool regex)
{
	const QString text = regex
		? translated(QT_TRANSLATE_NOOP("ReplaceLists",
			"# Regex replacement list\n"
			"# Format: regex<TAB>replacement\n"
			"# Backreferences like $1, $2 are supported\n"
			"\n"
			"(\\d{4})-(\\d{2})-(\\d{2})\t$1_$2_$3\n"))
		: translated(QT_TRANSLATE_NOOP("ReplaceLists",
			"# Replacement list\n"
			"# Format: search<TAB>replacement\n"
			"# Lines starting with # are ignored\n"
			"\n"
			"from1\tto1\n"
			"from2\tto2\n"));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	const QByteArray bytes = text.toUtf8();
	return file.write(bytes) == bytes.size();
}

QString replaceListPathForExpression(const QString &path)
{
	const QString home = QDir::homePath();
	if (!home.isEmpty() && home != QStringLiteral("/")
		&& (path == home || path.startsWith(home + QLatin1Char('/'))))
		return QStringLiteral("%HOME%") + path.mid(home.size());
	return path;
}

std::optional<QString> createAndSelectReplaceList(QWidget *parent, bool regex)
{
	const QString folder = replaceListFolder(regex);
	if (!QDir(folder).exists() && !QDir().mkpath(folder))
	{
		showError(parent, translated(QT_TRANSLATE_NOOP("ReplaceLists",
			"Failed to create folder:\n%1")).arg(folder));
		return std::nullopt;
	}

	// the menu item's text is the dialog's title, as upstream has it
	QString title = regex
		? QCoreApplication::translate("LineFilterMenu", "Create &Regex Replace List and Insert...")
		: QCoreApplication::translate("LineFilterMenu", "&Create String Replace List and Insert...");
	title.remove(QLatin1Char('&'));
	QString path;
	if (chooserStandIn())
	{
		path = chooserStandIn()(folder, regex);
	}
	else
	{
		QFileDialog dialog(parent, title, folder, translated(QT_TRANSLATE_NOOP("ReplaceLists",
			"Tab-Separated Values (*.tsv *.txt);;All Files (*)")));
		dialog.setAcceptMode(QFileDialog::AcceptSave);
		dialog.setDefaultSuffix(QStringLiteral("tsv"));
		dialog.setWindowModality(Qt::WindowModal);
		if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
			path = dialog.selectedFiles().constFirst();
	}
	if (path.isEmpty())
		return std::nullopt;

	if (!createReplaceListTemplate(path, regex))
	{
		showError(parent, translated(QT_TRANSLATE_NOOP("ReplaceLists",
			"Failed to create file:\n%1")).arg(path));
		return std::nullopt;
	}

	// for the user to fill it in
	if (openerStandIn())
		openerStandIn()(path);
	else
	{
#ifdef Q_OS_MACOS
		// the default text editor: a .tsv may belong to a spreadsheet
		QProcess::startDetached(QStringLiteral("/usr/bin/open"), { QStringLiteral("-t"), path });
#else
		QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
	}
	return path;
}

void openReplaceListFolder(bool regex)
{
	const QString folder = replaceListFolder(regex);
	if (!QDir(folder).exists() && !QDir().mkpath(folder))
		return;
	if (openerStandIn())
		openerStandIn()(folder);
	else
		QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void setReplaceListBaseForTest(const QString &base)
{
	baseOverride() = base;
}

void setReplaceListChooserForTest(std::function<QString(const QString &, bool)> chooser)
{
	chooserStandIn() = std::move(chooser);
}

void setReplaceListOpenerForTest(std::function<void(const QString &)> opener)
{
	openerStandIn() = std::move(opener);
}

} // namespace lm
