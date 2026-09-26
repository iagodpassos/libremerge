// SPDX-License-Identifier: GPL-3.0-or-later
#include "DesktopIntegration.h"

#include <cstdio>
#include <cstring>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace
{

const char kInstallFlag[] = "--install-desktop-integration";
const char kRemoveFlag[] = "--remove-desktop-integration";
const char kDataHomePrefix[] = "--data-home=";
// marks the entries LibreMerge wrote, so removal leaves others alone
const QString kMarkerKey = QStringLiteral("X-LibreMerge-Launcher");
const int kIconSizes[] = { 16, 32, 64, 128, 256, 512 };

QString desktopFilePath(const QString &dataHome)
{
	return dataHome + QStringLiteral("/applications/libremerge.desktop");
}

QString iconPath(const QString &dataHome, int size)
{
	return dataHome + QStringLiteral("/icons/hicolor/%1x%1/apps/libremerge.png")
		.arg(size);
}

QString bundledIcon(int size)
{
	return QStringLiteral(":/desktop/icons/libremerge-%1.png").arg(size);
}

/** An Exec argument per the Desktop Entry spec: quoted, with the four
    reserved characters backslash-escaped inside the quotes. */
QString quoteExecArgument(const QString &path)
{
	QString escaped;
	for (const QChar c : path)
	{
		if (c == QLatin1Char('"') || c == QLatin1Char('`')
			|| c == QLatin1Char('$') || c == QLatin1Char('\\'))
			escaped += QLatin1Char('\\');
		escaped += c;
	}
	// the desktop file format escapes backslashes once more in values
	escaped.replace(QLatin1String("\\"), QLatin1String("\\\\"));
	return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QByteArray readAll(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/** Refresh the menu and icon caches when the tools are around; desktops
    that watch the directories pick the changes up without them. */
void refreshCaches(const QString &dataHome)
{
	const QString updateDb =
		QStandardPaths::findExecutable(QStringLiteral("update-desktop-database"));
	if (!updateDb.isEmpty())
		QProcess::execute(updateDb, { dataHome + QStringLiteral("/applications") });
	const QString iconCache =
		QStandardPaths::findExecutable(QStringLiteral("gtk-update-icon-cache"));
	const QString hicolor = dataHome + QStringLiteral("/icons/hicolor");
	if (!iconCache.isEmpty() && QFileInfo(hicolor).isDir())
		QProcess::execute(iconCache,
			{ QStringLiteral("-f"), QStringLiteral("-t"), QStringLiteral("-q"), hicolor });
}

} // namespace

namespace lm
{

QString desktopDataHome()
{
	const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
	if (!xdg.isEmpty() && QDir::isAbsolutePath(xdg))
		return QDir::cleanPath(xdg);
	return QDir::homePath() + QStringLiteral("/.local/share");
}

QString desktopLauncher()
{
	// the AppImage runtime exports the path of the .AppImage file; the
	// running binary is a throwaway mount (or extraction) of it
	const QString appImage = qEnvironmentVariable("APPIMAGE");
	if (!appImage.isEmpty() && QFileInfo(appImage).isFile())
		return QFileInfo(appImage).absoluteFilePath();
	return QCoreApplication::applicationFilePath();
}

bool installDesktopIntegration(const QString &dataHome, const QString &launcher,
	QStringList *written, QString *error)
{
	QString entry = QString::fromUtf8(
		readAll(QStringLiteral(":/desktop/libremerge.desktop")));
	if (entry.isEmpty())
	{
		if (error != nullptr)
			*error = QStringLiteral("the bundled desktop entry is missing");
		return false;
	}
	// same entry as the distribution packages, pointed at this launcher
	QStringList lines;
	for (const QString &line : entry.split(QLatin1Char('\n')))
	{
		if (line.startsWith(QLatin1String("Exec=")))
		{
			lines.append(QStringLiteral("Exec=%1 %F").arg(quoteExecArgument(launcher)));
			lines.append(QStringLiteral("TryExec=%1").arg(launcher));
		}
		else if (!line.startsWith(QLatin1String("TryExec=")))
			lines.append(line);
	}
	while (!lines.isEmpty() && lines.last().isEmpty())
		lines.removeLast();
	lines.append(kMarkerKey + QLatin1Char('=') + launcher);

	const QString desktopFile = desktopFilePath(dataHome);
	QDir().mkpath(QFileInfo(desktopFile).absolutePath());
	QFile out(desktopFile);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		if (error != nullptr)
			*error = QStringLiteral("cannot write %1: %2")
				.arg(desktopFile, out.errorString());
		return false;
	}
	out.write((lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
	out.close();
	if (written != nullptr)
		written->append(desktopFile);

	for (const int size : kIconSizes)
	{
		const QString target = iconPath(dataHome, size);
		QDir().mkpath(QFileInfo(target).absolutePath());
		QFile::remove(target);
		if (!QFile::copy(bundledIcon(size), target))
		{
			if (error != nullptr)
				*error = QStringLiteral("cannot write %1").arg(target);
			return false;
		}
		// resource copies come out read-only
		QFile::setPermissions(target, QFileDevice::ReadOwner
			| QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
		if (written != nullptr)
			written->append(target);
	}
	refreshCaches(dataHome);
	return true;
}

bool removeDesktopIntegration(const QString &dataHome, QStringList *removed,
	QString *error)
{
	const QString desktopFile = desktopFilePath(dataHome);
	if (QFileInfo(desktopFile).isFile())
	{
		if (!QString::fromUtf8(readAll(desktopFile)).contains(kMarkerKey + QLatin1Char('=')))
		{
			if (error != nullptr)
				*error = QStringLiteral("%1 was not written by LibreMerge; left in place")
					.arg(desktopFile);
			return false;
		}
		if (!QFile::remove(desktopFile))
		{
			if (error != nullptr)
				*error = QStringLiteral("cannot remove %1").arg(desktopFile);
			return false;
		}
		if (removed != nullptr)
			removed->append(desktopFile);
	}
	for (const int size : kIconSizes)
	{
		// only icons identical to LibreMerge's own
		const QString target = iconPath(dataHome, size);
		if (QFileInfo(target).isFile() && readAll(target) == readAll(bundledIcon(size))
			&& QFile::remove(target) && removed != nullptr)
			removed->append(target);
	}
	refreshCaches(dataHome);
	return true;
}

int runDesktopIntegrationCommand(int argc, char *argv[])
{
	bool install = false;
	bool remove = false;
	const char *dataHomeArg = nullptr;
	for (int i = 1; i < argc; ++i)
	{
		install = install || std::strcmp(argv[i], kInstallFlag) == 0;
		remove = remove || std::strcmp(argv[i], kRemoveFlag) == 0;
		if (std::strncmp(argv[i], kDataHomePrefix, std::strlen(kDataHomePrefix)) == 0)
			dataHomeArg = argv[i] + std::strlen(kDataHomePrefix);
	}
	if (!install && !remove)
		return -1;
	if (install && remove)
	{
		std::fprintf(stderr, "LibreMerge: use only one of %s and %s\n",
			kInstallFlag, kRemoveFlag);
		return 2;
	}
#ifndef Q_OS_LINUX
	std::fprintf(stderr,
		"LibreMerge: desktop integration applies to the Linux AppImage only\n");
	return 1;
#else
	// no GUI: this runs from package-manager hooks, often without a display
	QCoreApplication app(argc, argv);
	QStringList paths;
	QString error;
	// --data-home=<dir> overrides the XDG location; relative to the working
	// directory, so a sandboxed caller with a stand-in $HOME (Homebrew's
	// install steps) can point at the real ~/.local/share through chdir
	const QString dataHome = dataHomeArg != nullptr
		? QDir::cleanPath(QDir::current().absoluteFilePath(
			QString::fromLocal8Bit(dataHomeArg)))
		: desktopDataHome();
	const bool ok = install
		? installDesktopIntegration(dataHome, desktopLauncher(), &paths, &error)
		: removeDesktopIntegration(dataHome, &paths, &error);
	for (const QString &path : paths)
		std::printf("%s %s\n", install ? "wrote" : "removed", qPrintable(path));
	if (!ok)
	{
		std::fprintf(stderr, "LibreMerge: %s\n", qPrintable(error));
		return 1;
	}
	if (install)
		std::printf("LibreMerge is in the applications menu, launching %s\n",
			qPrintable(desktopLauncher()));
	return 0;
#endif
}

} // namespace lm
