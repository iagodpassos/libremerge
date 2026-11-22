// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "FileOps.h"
#include "EngineOptions.h"
#include "MessageBoxes.h"
#include "OptionsDialog.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QCoreApplication>
#include <QProcess>

#include "Environment.h"
#include "OptionsDef.h"
#include "OptionsMgr.h"

#ifdef LM_HAVE_PORTAL
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace lm
{

namespace
{

bool &trashPortalForTest()
{
	static bool on = false;
	return on;
}

#ifdef LM_HAVE_PORTAL
/** org.freedesktop.portal.Trash's TrashFile, handed the file open for
    reading and writing (what the portal asks of an application allowed
    to trash it), a folder as a path only, as GLib's
    g_trash_portal_trash_file opens them. The portal takes no path opened
    without following a link, and following one would trash what it
    points to: a link is not trashed. */
bool trashThroughPortal(const QString &path)
{
	const QByteArray name = QFile::encodeName(path);
	int fd = ::open(name.constData(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
#ifdef O_PATH
	if (fd < 0 && errno == EISDIR)
		fd = ::open(name.constData(), O_PATH | O_CLOEXEC);
#endif
	if (fd < 0)
		return false;
	QDBusMessage call = QDBusMessage::createMethodCall(
		QStringLiteral("org.freedesktop.portal.Desktop"),
		QStringLiteral("/org/freedesktop/portal/desktop"),
		QStringLiteral("org.freedesktop.portal.Trash"), QStringLiteral("TrashFile"));
	call << QVariant::fromValue(QDBusUnixFileDescriptor(fd)); // a copy of it
	::close(fd);
	// 1 when the file went to the Trash, 0 when it did not
	const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 30000);
	return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
		&& reply.arguments().constFirst().toUInt() == 1;
}
#endif

} // namespace

bool moveToTrash(const QString &path)
{
#ifdef LM_HAVE_PORTAL
	// a Flatpak sandbox: its Trash would be the sandbox's own folder,
	// another mount the files cannot be moved to (GLib's
	// glib_should_use_portal looks for the same file)
	static const bool sandboxed = QFile::exists(QStringLiteral("/.flatpak-info"));
	if (sandboxed || trashPortalForTest())
		return trashThroughPortal(path);
#endif
	return QFile::moveToTrash(path);
}

void setTrashPortalForTest(bool on)
{
	trashPortalForTest() = on;
}

bool copyRecursively(const QString &src, const QString &dst)
{
	const QFileInfo info(src);
	if (info.isDir())
	{
		if (!QDir().mkpath(dst))
			return false;
		const QDir dir(src);
		const QFileInfoList entries = dir.entryInfoList(
			QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
		for (const QFileInfo &entry : entries)
		{
			if (!copyRecursively(entry.filePath(), dst + QLatin1Char('/') + entry.fileName()))
				return false;
		}
		return true;
	}
	// what is written over goes through the Trash, recoverable, unless
	// Options > System says not to use it
	if (QFile::exists(dst))
	{
		if (lm::useRecycleBin())
			moveToTrash(dst);
		else
			QFile::remove(dst);
	}
	if (!QDir().mkpath(QFileInfo(dst).path()))
		return false;
	return QFile::copy(src, dst);
}

QDateTime timeToPreserve(const QString &path)
{
	const QFileInfo info(path);
	return OptionsDialog::preserveFileTime() && info.exists()
		? info.lastModified() : QDateTime();
}

void restoreFileTime(const QString &path, const QDateTime &time)
{
	if (!time.isValid())
		return;
	// like CMergeDoc::DoSave, a failure here does not fail the save
	QFile file(path);
	if (file.open(QIODevice::Append))
		file.setFileTime(time, QFileDevice::FileModificationTime);
}

FileStamp fileStamp(const QString &path)
{
	FileStamp stamp;
	const QFileInfo info(path);
	if (!path.isEmpty() && info.exists())
	{
		stamp.modified = info.lastModified();
		stamp.size = info.size();
	}
	return stamp;
}

FileChange fileChangedOnDisk(const QString &path, const FileStamp &stamp)
{
	// "we assume file existed, so disappearing means removal"
	const QFileInfo info(path);
	if (path.isEmpty() || !info.exists())
		return FileChange::Removed;
	// any time difference counts: WinMerge's two-second tolerance comes
	// with OPT_IGNORE_SMALL_FILETIME, off by default and not offered here
	if (info.lastModified() != stamp.modified || info.size() != stamp.size)
		return FileChange::Changed;
	return FileChange::NoChange;
}

namespace
{
std::function<QString(const QString &)> s_saveAsAnswer;
}

QString askSaveAsPath(QWidget *parent, const QString &title, const QString &start,
	const QString &filter)
{
	if (s_saveAsAnswer)
		return s_saveAsAnswer(start);
	return QFileDialog::getSaveFileName(parent, title, start, filter);
}

void setSaveAsPathForTest(std::function<QString(const QString &)> answer)
{
	s_saveAsAnswer = std::move(answer);
}

namespace
{
std::function<bool(const QStringList &)> &externalEditorForTest()
{
	static std::function<bool(const QStringList &)> run;
	return run;
}
} // namespace

QStringList externalEditorCommand(const QString &file, int line)
{
	COptionsMgr *mgr = GetOptionsMgr();
	QString command = QString::fromStdString(env::ExpandEnvironmentVariables(mgr != nullptr
		? mgr->GetString(OPT_EXT_EDITOR_CMD) : defaultExternalEditor().toStdString()));
	command.replace(QLatin1String("$linenum"), QString::number(line));
	// upstream's quoting: the file in quotes where $file is, the closing
	// one before the next blank after it; or the file at the end
	const int at = command.indexOf(QLatin1String("$file"));
	if (at >= 0)
	{
		const QString quoted = QLatin1Char('"') + file;
		command.replace(QLatin1String("$file"), quoted);
		const int blank = command.indexOf(QLatin1Char(' '), at + quoted.size());
		if (blank >= 0)
			command.insert(blank, QLatin1Char('"'));
		else
			command += QLatin1Char('"');
	}
	else
		command += QStringLiteral(" \"") + file + QLatin1Char('"');
	// CreateProcess takes a command line; here it is split as a shell would
	return QProcess::splitCommand(command);
}

void setExternalEditorForTest(std::function<bool(const QStringList &)> run)
{
	externalEditorForTest() = std::move(run);
}

bool openInExternalEditor(QWidget *parent, const QString &file, int line)
{
	const QStringList command = externalEditorCommand(file, line);
	bool started = false;
	if (externalEditorForTest())
		started = externalEditorForTest()(command);
	else
		started = !command.isEmpty() && QProcess::startDetached(command.first(), command.mid(1));
	if (!started)
		showError(parent, QCoreApplication::translate("FileOps",
			"Failed to execute external editor: %1").arg(command.join(QLatin1Char(' '))));
	return started;
}

} // namespace lm
