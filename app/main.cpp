// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge: Qt application entry point.
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>
#include "FileCompareView.h"
#include "TableCompareView.h"
#include "FileOps.h"
#include "FolderCompareDriver.h"
#include "MainWindow.h"
#include "EngineOptions.h"
#ifdef Q_OS_MACOS
#include "MacServices.h"
#endif

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QGuiApplication::setDesktopFileName(QStringLiteral("libremerge"));
	QApplication::setApplicationName(QStringLiteral("LibreMerge"));
	QApplication::setApplicationVersion(QStringLiteral("0.7.0"));
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
	QCommandLineOption newOpt(QStringLiteral("new"),
		QStringLiteral("Open an empty text comparison"));
	parser.addOption(newOpt);
	QCommandLineOption selftestMergeAllOpt(QStringLiteral("selftest-merge-all"),
		QStringLiteral("Copy all differences at once left-to-right and verify (for testing)"));
	parser.addOption(selftestMergeAllOpt);
	QCommandLineOption selftestSaveOpt(QStringLiteral("selftest-save"),
		QStringLiteral("Merge left-to-right, save and verify the backup (for testing)"));
	parser.addOption(selftestSaveOpt);
	QCommandLineOption selftestUndoOpt(QStringLiteral("selftest-undo"),
		QStringLiteral("Copy one difference, undo, redo and verify (for testing)"));
	parser.addOption(selftestUndoOpt);
	QCommandLineOption selftestUndoScrollOpt(QStringLiteral("selftest-undo-scroll"),
		QStringLiteral("Verify the viewport stays put across merge+undo (for testing)"));
	parser.addOption(selftestUndoScrollOpt);
	QCommandLineOption selftestNavOpt(QStringLiteral("selftest-nav"),
		QStringLiteral("Verify next-diff after a copy continues from the cursor (for testing)"));
	parser.addOption(selftestNavOpt);
	QCommandLineOption selftestCopyOpt(QStringLiteral("selftest-copy"),
		QStringLiteral("Verify select-all + copy excludes alignment filler (for testing)"));
	parser.addOption(selftestCopyOpt);
	QCommandLineOption selftestTableOpt(QStringLiteral("selftest-table"),
		QStringLiteral("Table-compare two CSVs, merge all and verify (for testing)"));
	parser.addOption(selftestTableOpt);
	parser.process(app);

	if (parser.isSet(selftestTableOpt))
	{
		const QStringList files = parser.positionalArguments();
		if (files.size() != 2)
			return 2;
		TableCompareView view;
		QString error;
		if (!view.compare(files.at(0), files.at(1), &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		const int initial = view.diffCount();
		view.copyAllFrom(0);
		const int merged = view.diffCount();
		if (!view.saveModified(&error))
		{
			fprintf(stderr, "save failed: %s\n", qPrintable(error));
			return 2;
		}
		QFile leftFile(files.at(0)), rightFile(files.at(1));
		leftFile.open(QIODevice::ReadOnly);
		rightFile.open(QIODevice::ReadOnly);
		const bool equal = leftFile.readAll() == rightFile.readAll();
		printf("initial: %d, merged: %d, files equal: %d\n",
			initial, merged, equal);
		return (initial > 0 && merged == 0 && equal) ? 0 : 1;
	}

	if (parser.isSet(selftestCopyOpt))
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
		view.selectAllAndCopyForTest(0);
		const QString copied = QGuiApplication::clipboard()->text();

		QFile leftFile(files.at(0));
		leftFile.open(QIODevice::ReadOnly);
		QString expected = QString::fromUtf8(leftFile.readAll());
		expected.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		while (expected.endsWith(QChar('\n')))
			expected.chop(1);

		const bool ok = copied == expected;
		printf("copied %lld chars, expected %lld, match: %d\n",
			static_cast<long long>(copied.size()),
			static_cast<long long>(expected.size()), ok);
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestNavOpt))
	{
		const QStringList files = parser.positionalArguments();
		if (files.size() != 2)
			return 2;
		FileCompareView view;
		view.resize(900, 400);
		QString error;
		if (!view.compare(files.at(0), files.at(1), &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		// stand on the middle difference, merge it (which deselects),
		// then Next must continue downward from the cursor
		view.gotoFirstDiff();
		view.gotoNextDiff();
		QCoreApplication::processEvents();
		const int middle = view.firstVisibleViewLine();
		view.copyCurrentDiff(0);
		view.gotoNextDiff();
		QCoreApplication::processEvents();
		const int landed = view.firstVisibleViewLine();
		printf("middle: %d, landed: %d\n", middle, landed);
		return landed > middle ? 0 : 1;
	}

	if (parser.isSet(selftestUndoScrollOpt))
	{
		const QStringList files = parser.positionalArguments();
		if (files.size() != 2)
			return 2;
		FileCompareView view;
		view.resize(900, 400);
		QString error;
		if (!view.compare(files.at(0), files.at(1), &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		view.gotoLastDiff();
		QCoreApplication::processEvents();
		const int scrollBefore = view.firstVisibleViewLine();
		view.copyCurrentDiff(0);
		QCoreApplication::processEvents();
		view.undoActive();
		QCoreApplication::processEvents();
		const int scrollAfter = view.firstVisibleViewLine();
		printf("before: %d, after: %d\n", scrollBefore, scrollAfter);
		return qAbs(scrollAfter - scrollBefore) <= 2 ? 0 : 1;
	}

	if (parser.isSet(selftestUndoOpt))
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
		const int initial = view.diffCount();
		view.copyCurrentDiff(0);
		const int afterCopy = view.diffCount();
		view.undoActive();
		const int afterUndo = view.diffCount();
		view.redoActive();
		const int afterRedo = view.diffCount();
		printf("initial: %d, copy: %d, undo: %d, redo: %d\n",
			initial, afterCopy, afterUndo, afterRedo);
		const bool ok = initial > 0 && afterCopy == initial - 1
			&& afterUndo == initial && afterRedo == initial - 1;
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestSaveOpt))
	{
		const QStringList files = parser.positionalArguments();
		if (files.size() != 2)
			return 2;
		QFile rightBefore(files.at(1));
		rightBefore.open(QIODevice::ReadOnly);
		const QByteArray originalRight = rightBefore.readAll();
		rightBefore.close();

		FileCompareView view;
		QString error;
		if (!view.compare(files.at(0), files.at(1), &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		view.copyAllFrom(0);
		if (!view.saveModified(&error))
		{
			fprintf(stderr, "save failed: %s\n", qPrintable(error));
			return 2;
		}
		QFile leftFile(files.at(0)), rightFile(files.at(1));
		QFile backupFile(files.at(1) + QStringLiteral(".bak"));
		leftFile.open(QIODevice::ReadOnly);
		rightFile.open(QIODevice::ReadOnly);
		const bool merged = leftFile.readAll() == rightFile.readAll();
		bool backupOk = backupFile.open(QIODevice::ReadOnly)
			&& backupFile.readAll() == originalRight;
		printf("merged: %d, backup: %d\n", merged, backupOk);
		return (merged && backupOk) ? 0 : 1;
	}

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
#ifdef Q_OS_MACOS
	lm::installMacServices(&window);
#endif
	const QStringList args = parser.positionalArguments();
	if (parser.isSet(newOpt))
	{
		window.openBlankComparison();
	}
	else if (args.size() == 2)
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
	else if (!args.isEmpty())
	{
		window.openSelector(args);
	}
	else
	{
		// like WinMerge, start on a fresh (blank) comparison; the
		// selector stays one Cmd+O away
		window.openBlankComparison();
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
