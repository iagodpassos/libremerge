// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

/**
 * Applications-menu integration for the Linux AppImage: a user-level
 * .desktop entry pointing at the AppImage file plus the hicolor icons,
 * the job AppImageLauncher or a distribution package would otherwise do.
 * Used by `--install-desktop-integration` / `--remove-desktop-integration`
 * (also what the Homebrew cask runs after installing on Linux).
 */
namespace lm
{

/** $XDG_DATA_HOME, or ~/.local/share. */
QString desktopDataHome();

/** The file the menu entry launches: the AppImage ($APPIMAGE) when
    running from one, else this executable. */
QString desktopLauncher();

/** Write <dataHome>/applications/libremerge.desktop and the icons. */
bool installDesktopIntegration(const QString &dataHome, const QString &launcher,
	QStringList *written, QString *error);

/** Remove what installDesktopIntegration wrote; a libremerge.desktop
    without LibreMerge's marker (made by hand, or by a package) stays. */
bool removeDesktopIntegration(const QString &dataHome, QStringList *removed,
	QString *error);

/** Handle the two command-line flags before the GUI starts, so they work
    without a display. Returns the exit code, or -1 when neither flag is
    present. */
int runDesktopIntegrationCommand(int argc, char *argv[]);

} // namespace lm
