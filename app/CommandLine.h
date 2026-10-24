// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <utility>
#include <QChar>
#include <QList>
#include <QString>
#include <QStringList>

namespace lm
{

/**
 * WinMerge's command line (MergeCmdLineInfo), read from the arguments as
 * the shell hands them over rather than from one line of text. Its
 * switches start with "-" only: "/" starts a path here. As upstream, a
 * switch's name may be written in any case and some take a value after a
 * colon ("-r:0", "-ignorews:1", "-cfg:Key=Value"), the others the next
 * argument; whatever is no switch is a path. What LibreMerge has no use
 * for is noted in unsupported, what upstream rejects in errors.
 */
struct CommandLine
{
	/** The window the paths open in (-t). */
	enum class WindowType { Automatic, Text, Table, Binary, Image, WebPage, Folder };
	/** The dialog shown in place of a comparison (-show-dialog). */
	enum class DialogType { None, Options, About };
	/** Closing when the files are identical (-x, -xq). */
	enum class ExitNoDiff { Disabled, Exit, ExitQuiet };
	/** The main window's state (-minimize, -maximize). */
	enum class ShowWindow { Normal, Maximize, Minimize };
	/** A side's behavior, upstream's FFILEOPEN_* flags. */
	enum Flag : unsigned
	{
		NoMru = 0x0001,     ///< not added to the recent comparisons (-u...)
		ReadOnly = 0x0002,  ///< opened read-only (-wl, -wm, -wr)
		SetFocus = 0x0040,  ///< the pane that takes the focus (-fl...)
		AutoMerge = 0x0080, ///< auto-merged at (-al...)
	};

	ShowWindow showWindow = ShowWindow::Normal;
	WindowType windowType = WindowType::Automatic;
	DialogType dialogType = DialogType::None;
	bool escShutdown = false;
	ExitNoDiff exitIfNoDiff = ExitNoDiff::Disabled;
	/** WinMerge's compare methods, CMP_CONTENT to CMP_EXISTENCE (-m). */
	std::optional<int> compareMethod;
	/** Folders compared with their subfolders or not (-r, -r-, -r:0);
	    "Include subfolders" when not given. */
	std::optional<bool> recursive;
	bool showUsage = false;
	bool showVersion = false; ///< --version, Qt's switch kept
	bool selfCompare = false;
	bool newCompare = false;
	bool gotoFirstDiff = false; ///< --goto-first-diff, kept
	/** No message boxes (each one answered Cancel, its text on the
	    terminal) and closing once the comparison is done. */
	bool nonInteractive = false;
	/** The last comparison's result as the exit code: 0 identical, 1
	    different, 2 an error or none. */
	bool enableExitCode = false;
	/** No saved options read, none saved (-noprefs). */
	bool noPrefs = false;
	QString outputPath; ///< where a file is saved (-o)
	QString iniFile;    ///< the options' file (-inifile)
	int lineIndex = -1; ///< from 0 (-l)
	int charIndex = -1; ///< from 0 (-c)
	std::optional<QChar> tableDelimiter;
	std::optional<QChar> tableQuote;
	std::optional<bool> tableAllowNewlinesInQuotes;
	unsigned leftFlags = 0;
	unsigned middleFlags = 0;
	unsigned rightFlags = 0;
	QString leftDesc;
	QString middleDesc;
	QString rightDesc;
	QString fileFilter; ///< mask or expression (-f)
	QString fileExt;    ///< with its dot (-fileext)
	/** The default codepage for this run, 0 for the saved one (-cp). */
	int codepage = 0;
	QStringList paths;
	/** Options by name, set for this run (-ignorews..., -cfg). */
	QList<std::pair<QString, QString>> options;
	QStringList errors;
	/** Upstream's switches LibreMerge does not act on (yet). */
	QStringList unsupported;

	/** The arguments after the program's name. */
	static CommandLine parse(const QStringList &arguments);
	/** What -? prints. */
	static QString usage();
};

} // namespace lm
