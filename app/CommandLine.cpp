// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "CommandLine.h"

#include <QFileInfo>

#include "OptionsDef.h"
#include "UnicodeString.h"

namespace lm
{

namespace
{

/** strutils::from_charstr: a character by itself or by name ("tab", "\t",
    "sq", "0x3b"...). */
QChar characterOf(const QString &value)
{
	const std::string utf8 = value.toStdString();
	const tchar_t ch = strutils::from_charstr(utf8);
	// a character of more than one byte goes as it is
	if (!value.isEmpty() && value.at(0).unicode() > 0x7F
		&& static_cast<unsigned char>(ch) == static_cast<unsigned char>(utf8.front()))
		return value.at(0);
	return QChar(static_cast<unsigned char>(ch));
}

} // namespace

CommandLine CommandLine::parse(const QStringList &arguments)
{
	CommandLine cmd;
	int next = 0;
	// what follows a switch's colon: as upstream's EatParam reads the
	// line, it is the switch's value (a colon in front of it for the
	// switches that do not look for one), or else the next argument
	QString colonValue;
	bool hasColonValue = false;
	const auto value = [&]() -> QString {
		if (hasColonValue)
		{
			hasColonValue = false;
			return QLatin1Char(':') + colonValue;
		}
		return next < arguments.size() ? arguments.at(next++) : QString();
	};
	// MergeCmdLineInfo::SetOption: "1", or the value after the colon
	const auto setOption = [&](const std::string &key) {
		QString optionValue = QStringLiteral("1");
		if (hasColonValue)
		{
			optionValue = colonValue;
			hasColonValue = false;
		}
		cmd.options.append({ QString::fromStdString(key), optionValue });
	};
	// AddPath: the path in full; the order of the sides is upstream's
	const auto addPath = [&cmd](const QString &path) {
		cmd.paths.append(path.contains(QStringLiteral("://"))
			? path : QFileInfo(path).absoluteFilePath());
	};
	const auto unsupported = [&cmd](const QString &name) {
		const QString option = QLatin1Char('-') + name;
		if (!cmd.unsupported.contains(option))
			cmd.unsupported.append(option);
	};

	while (next < arguments.size())
	{
		const QString argument = arguments.at(next++);
		if (!argument.startsWith(QLatin1Char('-')))
		{
			addPath(argument);
			continue;
		}
		// Qt's own switches, which LibreMerge took before, and its two
		// switches of then
		if (argument.startsWith(QStringLiteral("--")) || argument == QStringLiteral("-h")
			|| argument == QStringLiteral("-v"))
		{
			const QString name = argument.mid(argument.startsWith(QStringLiteral("--")) ? 2 : 1)
				.toLower();
			if (name == QStringLiteral("help") || name == QStringLiteral("h"))
				cmd.showUsage = true;
			else if (name == QStringLiteral("version") || name == QStringLiteral("v"))
				cmd.showVersion = true;
			else if (name == QStringLiteral("new"))
				cmd.newCompare = true;
			else if (name == QStringLiteral("goto-first-diff"))
				cmd.gotoFirstDiff = true;
			else
				cmd.errors.append(QStringLiteral("Unknown option '%1'").arg(argument));
			continue;
		}
		// a switch, its value apart after the last colon
		QString param = argument.mid(1);
		const qsizetype colon = param.lastIndexOf(QLatin1Char(':'));
		hasColonValue = colon >= 0;
		if (hasColonValue)
		{
			colonValue = param.mid(colon + 1);
			param = param.left(colon);
		}
		param = param.toLower();

		if (param == QStringLiteral("?"))
			cmd.showUsage = true;
		else if (param == QStringLiteral("dl"))
			cmd.leftDesc = value();
		else if (param == QStringLiteral("dm"))
			cmd.middleDesc = value();
		else if (param == QStringLiteral("dr"))
			cmd.rightDesc = value();
		else if (param == QStringLiteral("e"))
			cmd.escShutdown = true;
		else if (param == QStringLiteral("f"))
			cmd.fileFilter = value();
		else if (param == QStringLiteral("t"))
		{
			const QString type = value().toLower();
			if (type == QStringLiteral("automatic"))
				cmd.windowType = WindowType::Automatic;
			else if (type == QStringLiteral("text"))
				cmd.windowType = WindowType::Text;
			else if (type == QStringLiteral("table"))
				cmd.windowType = WindowType::Table;
			else if (type == QStringLiteral("binary"))
				cmd.windowType = WindowType::Binary;
			else if (type == QStringLiteral("image"))
				cmd.windowType = WindowType::Image;
			else if (type.startsWith(QStringLiteral("web")))
				cmd.windowType = WindowType::WebPage;
			else if (type == QStringLiteral("folder"))
				cmd.windowType = WindowType::Folder;
			else
				cmd.errors.append(QStringLiteral("Unknown window type '%1' specified").arg(type));
		}
		else if (param == QStringLiteral("show-dialog"))
		{
			const QString type = value().toLower();
			if (type == QStringLiteral("options"))
				cmd.dialogType = DialogType::Options;
			else if (type == QStringLiteral("about"))
				cmd.dialogType = DialogType::About;
			else
				cmd.errors.append(QStringLiteral("Unknown dialog type '%1' specified").arg(type));
		}
		else if (param == QStringLiteral("m"))
		{
			QString method = value().toLower();
			for (const char *word : { "and", "contents", "modified", " " })
				method.remove(QLatin1String(word));
			if (method == QStringLiteral("full"))
				cmd.compareMethod = 0;
			else if (method == QStringLiteral("quick"))
				cmd.compareMethod = 1;
			else if (method == QStringLiteral("binary"))
				cmd.compareMethod = 2;
			else if (method == QStringLiteral("date"))
				cmd.compareMethod = 3;
			else if (method == QStringLiteral("sizedate") || method == QStringLiteral("datesize"))
				cmd.compareMethod = 4;
			else if (method == QStringLiteral("size"))
				cmd.compareMethod = 5;
			else if (method == QStringLiteral("existence"))
				cmd.compareMethod = 6;
			else
				cmd.errors.append(QStringLiteral("Unknown compare method '%1' specified").arg(method));
		}
		else if (param == QStringLiteral("self-compare"))
			cmd.selfCompare = true;
		else if (param == QStringLiteral("new"))
			cmd.newCompare = true;
		else if (param == QStringLiteral("minimize"))
			cmd.showWindow = ShowWindow::Minimize;
		else if (param == QStringLiteral("maximize"))
			cmd.showWindow = ShowWindow::Maximize;
		else if (param == QStringLiteral("wl"))
			cmd.leftFlags |= ReadOnly;
		else if (param == QStringLiteral("wm"))
			cmd.middleFlags |= ReadOnly;
		else if (param == QStringLiteral("wr"))
			cmd.rightFlags |= ReadOnly;
		else if (param == QStringLiteral("ul"))
			cmd.leftFlags |= NoMru;
		else if (param == QStringLiteral("um"))
			cmd.middleFlags |= NoMru;
		else if (param == QStringLiteral("ur"))
			cmd.rightFlags |= NoMru;
		else if (param == QStringLiteral("u") || param == QStringLiteral("ub"))
		{
			cmd.leftFlags |= NoMru;
			cmd.middleFlags |= NoMru;
			cmd.rightFlags |= NoMru;
		}
		else if (param == QStringLiteral("fl"))
			cmd.leftFlags |= SetFocus;
		else if (param == QStringLiteral("fm"))
			cmd.middleFlags |= SetFocus;
		else if (param == QStringLiteral("fr"))
			cmd.rightFlags |= SetFocus;
		else if (param == QStringLiteral("l"))
		{
			cmd.lineIndex = value().toInt();
			if (cmd.lineIndex <= 0)
			{
				cmd.lineIndex = -1;
				cmd.errors.append(QStringLiteral("Invalid line number specified"));
			}
			else
				--cmd.lineIndex;
		}
		else if (param == QStringLiteral("c"))
		{
			cmd.charIndex = value().toInt();
			if (cmd.charIndex <= 0)
			{
				cmd.charIndex = -1;
				cmd.errors.append(QStringLiteral("Invalid character position specified"));
			}
			else
				--cmd.charIndex;
		}
		else if (param == QStringLiteral("table-delimiter"))
			cmd.tableDelimiter = characterOf(value());
		else if (param == QStringLiteral("table-quote"))
			cmd.tableQuote = characterOf(value());
		else if (param == QStringLiteral("table-allownewlinesinquotes"))
		{
			const QString allow = value().toLower();
			const QChar c = allow.isEmpty() ? QChar() : allow.at(0);
			cmd.tableAllowNewlinesInQuotes = c.isNull() || c == QLatin1Char('y')
				|| c == QLatin1Char('t') || c == QLatin1Char('1');
		}
		else if (param == QStringLiteral("x"))
			cmd.exitIfNoDiff = ExitNoDiff::Exit;
		else if (param == QStringLiteral("xq"))
			cmd.exitIfNoDiff = ExitNoDiff::ExitQuiet;
		else if (param == QStringLiteral("fileext"))
		{
			cmd.fileExt = value();
			if (!cmd.fileExt.isEmpty() && !cmd.fileExt.startsWith(QLatin1Char('.')))
				cmd.fileExt.prepend(QLatin1Char('.'));
		}
		else if (param == QStringLiteral("ignorews"))
			setOption(OPT_CMP_IGNORE_WHITESPACE);
		else if (param == QStringLiteral("ignoreblanklines"))
			setOption(OPT_CMP_IGNORE_BLANKLINES);
		else if (param == QStringLiteral("ignorecase"))
			setOption(OPT_CMP_IGNORE_CASE);
		else if (param == QStringLiteral("ignoreeol"))
			setOption(OPT_CMP_IGNORE_EOL);
		else if (param == QStringLiteral("ignorecodepage"))
			setOption(OPT_CMP_IGNORE_CODEPAGE);
		else if (param == QStringLiteral("ignorecomments"))
			setOption(OPT_CMP_FILTER_COMMENTLINES);
		else if (param == QStringLiteral("cfg") || param == QStringLiteral("config"))
		{
			// SetConfig: "Key=Value", after the colon or as the next argument
			QString setting;
			if (hasColonValue)
			{
				setting = colonValue;
				hasColonValue = false;
			}
			else
				setting = value();
			const qsizetype equals = setting.indexOf(QLatin1Char('='));
			if (equals >= 0)
				cmd.options.append({ setting.left(equals), setting.mid(equals + 1) });
		}
		// one instance and its groups, the Explorer's menu and the jump
		// list are Windows' own: a separate LibreMerge opens each time,
		// which is what a caller waiting for it (-sw) gets as well
		else if (param == QStringLiteral("s") || param == QStringLiteral("s-")
			|| param == QStringLiteral("sw") || param == QStringLiteral("show-compare-as-menu"))
			hasColonValue = false;
		else if (param == QStringLiteral("g") || param == QStringLiteral("set-usertasks-to-jumplist"))
			value();
		else if (param == QStringLiteral("o"))
		{
			const QString path = value();
			cmd.outputPath = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
		}
		else if (param == QStringLiteral("inifile"))
		{
			const QString path = value();
			cmd.iniFile = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
		}
		else if (param == QStringLiteral("noninteractive"))
			cmd.nonInteractive = true;
		else if (param == QStringLiteral("enableexitcode"))
			cmd.enableExitCode = true;
		else if (param == QStringLiteral("noprefs"))
			cmd.noPrefs = true;
		// upstream's, which LibreMerge does not act on yet
		else if (param == QStringLiteral("or") || param == QStringLiteral("unpacker")
			|| param == QStringLiteral("prediffer") || param == QStringLiteral("cp"))
		{
			value();
			unsupported(param);
		}
		else if (param == QStringLiteral("r") || param == QStringLiteral("r-"))
		{
			hasColonValue = false;
			unsupported(QStringLiteral("r"));
		}
		else if (param == QStringLiteral("clipboard-compare") || param == QStringLiteral("al")
			|| param == QStringLiteral("am") || param == QStringLiteral("ar"))
			unsupported(param);
		else
			cmd.errors.append(QStringLiteral("Unknown option '-%1'").arg(param));

		// what is left after a colon the switch did not read reads on as
		// upstream's next parameter: a path
		if (hasColonValue)
		{
			hasColonValue = false;
			addPath(QLatin1Char(':') + colonValue);
		}
	}
	if (cmd.windowType == WindowType::WebPage)
		unsupported(QStringLiteral("t webpage"));
	// upstream's usage is no unattended run, and neither is a dialog
	// shown alone, which waits for its answer
	if (cmd.showUsage || cmd.dialogType != DialogType::None)
		cmd.nonInteractive = false;
	return cmd;
}

QString CommandLine::usage()
{
	return QStringLiteral(
		"Usage: libremerge [switches] [left] [middle] [right]\n"
		"\n"
		"WinMerge's command line, with \"-\" in front of each switch:\n"
		"\n"
		"  -dl desc, -dm desc, -dr desc\n"
		"                  the description of the left, middle and right side\n"
		"  -wl, -wm, -wr   open the left, middle or right side read-only\n"
		"  -fl, -fm, -fr   put the focus on the left, middle or right pane\n"
		"  -u, -ul, -um, -ur\n"
		"                  leave the paths out of the recent comparisons\n"
		"  -e              Esc closes LibreMerge when one window is open\n"
		"  -x              close when the files are identical, after saying so\n"
		"  -xq             close when the files are identical, quietly\n"
		"  -t type         compare as automatic, text, table, binary, image\n"
		"                  or folder\n"
		"  -m method       compare folders by full, quick, binary, date,\n"
		"                  sizedate, size or existence\n"
		"  -f filter       the file filter (\"*.h;*.cpp\", a filter's name or\n"
		"                  an expression)\n"
		"  -l line, -c character\n"
		"                  go to that line and character when the files open\n"
		"  -fileext ext    color the text as files of that extension\n"
		"  -table-delimiter c, -table-quote c, -table-allownewlinesinquotes y|n\n"
		"                  how the files read as tables\n"
		"  -ignorews[:0|1|2], -ignoreblanklines[:0|1], -ignorecase[:0|1],\n"
		"  -ignoreeol[:0|1], -ignorecodepage[:0|1], -ignorecomments[:0|1]\n"
		"                  the compare options, for this run\n"
		"  -cfg Name=Value any option by its name, for this run\n"
		"  -noprefs        neither read nor save the options\n"
		"  -inifile file   keep the options in that file\n"
		"  -o file         save a file there in place of where it came from\n"
		"  -noninteractive no message boxes (their text goes to the terminal,\n"
		"                  answered Cancel), and close once compared\n"
		"  -enableexitcode exit with 0 when the files are identical, 1 when\n"
		"                  they differ and 2 on an error\n"
		"  -self-compare   compare the one file given with a copy of itself\n"
		"  -new            open an empty comparison\n"
		"  -minimize, -maximize\n"
		"                  the state of the window\n"
		"  -show-dialog options|about\n"
		"                  show that dialog only\n"
		"  -?, --help      show this\n"
		"  --version       show the version\n"
		"\n"
		"Two or three paths compare files, folders or archives; one path\n"
		"opens the selection screen with it.\n");
}

} // namespace lm
