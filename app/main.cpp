// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge: Qt application entry point.
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>
#include "FileCompareView.h"
#include "FileOps.h"
#include "FolderCompareDriver.h"
#include "MainWindow.h"
#include "EngineOptions.h"

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("LibreMerge"));
	QApplication::setApplicationVersion(QStringLiteral("0.3.0"));
	QApplication::setOrganizationName(QStringLiteral("LibreMerge"));

	// translations follow the system language (LIBREMERGE_LANGUAGE
	// overrides it, e.g. pt_BR); Qt's own strings load too when the
	// qtbase catalog is available
	const QByteArray forcedLanguage = qgetenv("LIBREMERGE_LANGUAGE");
	const QLocale locale = forcedLanguage.isEmpty()
		? QLocale() : QLocale(QString::fromUtf8(forcedLanguage));
	static QTranslator qtTranslator;
	if (qtTranslator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
			QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
		app.installTranslator(&qtTranslator);
	static QTranslator appTranslator;
	if (appTranslator.load(locale, QStringLiteral("libremerge"),
			QStringLiteral("_"), QStringLiteral(":/i18n")))
		app.installTranslator(&appTranslator);

	lm::installEngineOptions();

	QCommandLineParser parser;
	parser.setApplicationDescription(
		QStringLiteral("A free differencing and merging tool for macOS and Linux"));
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument(QStringLiteral("left"), QStringLiteral("Left file"), QStringLiteral("[left]"));
	parser.addPositionalArgument(QStringLiteral("middle"), QStringLiteral("Middle file (3-way)"), QStringLiteral("[middle]"));
	parser.addPositionalArgument(QStringLiteral("right"), QStringLiteral("Right file"), QStringLiteral("[right]"));
	QCommandLineOption screenshotOpt(QStringLiteral("screenshot"),
		QStringLiteral("Render the comparison to <file> and exit (for testing)"),
		QStringLiteral("file"));
	parser.addOption(screenshotOpt);
	QCommandLineOption selftestMergeOpt(QStringLiteral("selftest-merge"),
		QStringLiteral("Copy all differences left-to-right in memory and verify (for testing)"));
	parser.addOption(selftestMergeOpt);
	QCommandLineOption selftestCountOpt(QStringLiteral("selftest-count"),
		QStringLiteral("Print the number of differences and exit (for testing)"));
	parser.addOption(selftestCountOpt);
	QCommandLineOption selftestFileOpsOpt(QStringLiteral("selftest-fileops"),
		QStringLiteral("Copy <left> recursively onto <right> and verify (for testing)"));
	parser.addOption(selftestFileOpsOpt);
	QCommandLineOption gotoFirstOpt(QStringLiteral("goto-first-diff"),
		QStringLiteral("Select the first difference after opening (for testing)"));
	parser.addOption(gotoFirstOpt);
	QCommandLineOption selftestMergeAllOpt(QStringLiteral("selftest-merge-all"),
		QStringLiteral("Copy all differences at once left-to-right and verify (for testing)"));
	parser.addOption(selftestMergeAllOpt);
	parser.process(app);

	if (parser.isSet(selftestMergeAllOpt))
	{
		const QStringList files = parser.positionalArguments();
		if (files.size() != 2)
			return 2;
		FileCompareView view;
		QString error;
		if (!view.compare(files.at(0), files.at(1), &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		view.copyAllFrom(0);
		view.recompare();
		printf("remaining diffs: %d\n", view.diffCount());
		return view.diffCount() == 0 ? 0 : 1;
	}

	if (parser.isSet(selftestFileOpsOpt))
	{
		const QStringList dirs = parser.positionalArguments();
		if (dirs.size() != 2)
			return 2;
		if (!lm::copyRecursively(dirs.at(0), dirs.at(1)))
		{
			fprintf(stderr, "copy failed\n");
			return 1;
		}
		const lm::FolderCompareResult result =
			lm::compareFolders(dirs.at(0), dirs.at(1), true);
		printf("after copy: %d different, %d unique, %d identical\n",
			result.different, result.unique, result.identical);
		return (result.different == 0 && result.unique == 0) ? 0 : 1;
	}

	if (parser.isSet(selftestCountOpt))
	{
		const QStringList files = parser.positionalArguments();
		if (files.size() != 2)
			return 2;
		FileCompareView view;
		QString error;
		if (!view.compare(files.at(0), files.at(1), &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		printf("diffs: %d\n", view.diffCount());
		return 0;
	}

	if (parser.isSet(selftestMergeOpt))
	{
		const QStringList files = parser.positionalArguments();
		if (files.size() != 2)
			return 2;
		FileCompareView view;
		QString error;
		if (!view.compare(files.at(0), files.at(1), &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		int guard = 0;
		while (view.diffCount() > 0 && guard++ < 1000)
			view.copyCurrentDiff(0);
		view.recompare();
		printf("remaining diffs: %d\n", view.diffCount());
		return view.diffCount() == 0 ? 0 : 1;
	}

	MainWindow window;
	const QStringList args = parser.positionalArguments();
	if (args.size() == 2)
	{
		const QFileInfo leftInfo(args.at(0)), rightInfo(args.at(1));
		if (leftInfo.isDir() && rightInfo.isDir())
			window.openFolderComparison(args.at(0), args.at(1));
		else
			window.openFileComparison(args.at(0), args.at(1));
	}
	else if (args.size() == 3)
	{
		window.openFileComparison(args);
	}
	else
	{
		window.openSelector(args);
	}

	if (parser.isSet(gotoFirstOpt))
		window.gotoFirstDifference();

	if (parser.isSet(screenshotOpt))
	{
		const QString target = parser.value(screenshotOpt);
		// give async comparisons a moment to finish before capturing
		QTimer::singleShot(1500, &window, [&window, target]() {
			window.resize(1100, 700);
			window.grab().save(target);
			QApplication::quit();
		});
		window.show();
		return app.exec();
	}

	window.show();
	return app.exec();
}
