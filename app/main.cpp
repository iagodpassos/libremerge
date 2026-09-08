// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge: Qt application entry point.
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include "FileCompareView.h"
#include "TableCompareView.h"
#include "ImageCompareView.h"
#include "FileOps.h"
#include "FolderCompareDriver.h"
#include <archive.h>
#include <archive_entry.h>
#include <QDir>
#include "ArchiveCompare.h"
#include "MainWindow.h"
#include "NewComparisonView.h"
#include "EngineOptions.h"
#include "OptionsDialog.h"
#ifdef Q_OS_MACOS
#include "MacServices.h"
#endif

// engine (folder-compare image hook)
#include "DiffItem.h"
#include "IAbortable.h"
#include "image_compare_hook.h"
#include "ImgMergeBuffer.hpp"

namespace
{

/** Pixel comparison for the folder compare, mirroring upstream's
    ImageCompare::compare_files over the ported WinIMerge core. */
int compareImageFiles(const String &file1, const String &file2,
	double colorDistanceThreshold, const IAbortable *piAbortable)
{
	CImgMergeBuffer buffer;
	buffer.SetColorDistanceThreshold(colorDistanceThreshold);
	const std::wstring f1 = QString::fromStdString(file1).toStdWString();
	const std::wstring f2 = QString::fromStdString(file2).toStdWString();
	const wchar_t *files[3] = { f1.c_str(), f2.c_str(), nullptr };
	if (!buffer.OpenImages(2, files))
		return DIFFCODE::CMPERR;
	bool aborted = false;
	bool different = false;
	if (buffer.GetPageCount(0) == buffer.GetPageCount(1))
	{
		for (int page = 0; page < buffer.GetPageCount(0); ++page)
		{
			if (piAbortable != nullptr && piAbortable->ShouldAbort())
			{
				aborted = true;
				break;
			}
			buffer.SetCurrentPageAll(page);
			buffer.CompareImages();
			if (buffer.GetDiffCount() > 0)
			{
				different = true;
				break;
			}
		}
	}
	else
		different = true;
	buffer.CloseImages();
	if (aborted)
		return DIFFCODE::CMPABORT;
	return different ? DIFFCODE::DIFF : DIFFCODE::SAME;
}

} // namespace

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QGuiApplication::setDesktopFileName(QStringLiteral("libremerge"));
	QApplication::setApplicationName(QStringLiteral("LibreMerge"));
	QApplication::setApplicationVersion(QStringLiteral("0.9.3"));
	QApplication::setOrganizationName(QStringLiteral("LibreMerge"));

	// translations follow the system language unless overridden by the
	// Options dialog (Appearance/Language) or, for testing, by the
	// LIBREMERGE_LANGUAGE environment variable; Qt's own strings load
	// too when the qtbase catalog is available
	const QByteArray forcedLanguage = qgetenv("LIBREMERGE_LANGUAGE");
	const QString configuredLanguage = QSettings()
		.value(QStringLiteral("Appearance/Language")).toString();
	const QLocale locale = !forcedLanguage.isEmpty()
		? QLocale(QString::fromUtf8(forcedLanguage))
		: (!configuredLanguage.isEmpty()
			? QLocale(configuredLanguage) : QLocale());
	static QTranslator qtTranslator;
	if (qtTranslator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
			QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
		app.installTranslator(&qtTranslator);
	static QTranslator appTranslator;
	if (appTranslator.load(locale, QStringLiteral("libremerge"),
			QStringLiteral("_"), QStringLiteral(":/i18n")))
		app.installTranslator(&appTranslator);

	lm::installEngineOptions();
	lm::SetImageCompareHook(&compareImageFiles);

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
	QCommandLineOption selftestImageOpt(QStringLiteral("selftest-image"),
		QStringLiteral("Image-compare two files, merge all in memory and verify (for testing)"));
	parser.addOption(selftestImageOpt);
	QCommandLineOption selftestMerge3Opt(QStringLiteral("selftest-merge3"),
		QStringLiteral("3-way: merge left into middle, then middle into right, and verify (for testing)"));
	parser.addOption(selftestMerge3Opt);
	QCommandLineOption selftestUndoRescanOpt(QStringLiteral("selftest-undo-rescan"),
		QStringLiteral("Edit, recompare (realigning the edited pane), undo and redo (for testing)"));
	parser.addOption(selftestUndoRescanOpt);
	QCommandLineOption selftestUndoGhostsOpt(QStringLiteral("selftest-undo-ghosts"),
		QStringLiteral("Merge over ghost filler, undo and verify no phantom lines (for testing)"));
	parser.addOption(selftestUndoGhostsOpt);
	QCommandLineOption selftestOpenEnterOpt(QStringLiteral("selftest-open-enter"),
		QStringLiteral("Press Enter in the selector's path field and verify the comparison opens (for testing)"));
	parser.addOption(selftestOpenEnterOpt);
	QCommandLineOption selftestArchiveOpt(QStringLiteral("selftest-archive"),
		QStringLiteral("Build a zip and a tar.gz, extract both and folder-compare them (for testing)"));
	parser.addOption(selftestArchiveOpt);
	QCommandLineOption selftestFolder3Opt(QStringLiteral("selftest-folder3"),
		QStringLiteral("Build three folder trees and verify the 3-way comparison (for testing)"));
	parser.addOption(selftestFolder3Opt);
	QCommandLineOption selftestServiceOpt(QStringLiteral("selftest-service"),
		QStringLiteral("Simulate the Finder service arriving after a cold start (for testing)"));
	parser.addOption(selftestServiceOpt);
	parser.process(app);

	if (parser.isSet(selftestServiceOpt))
	{
		// cold start: the app opens its blank placeholder, then the
		// Finder service delivers a pair; the placeholder must go
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const QString leftPath = dir.filePath(QStringLiteral("a.txt"));
		const QString rightPath = dir.filePath(QStringLiteral("b.txt"));
		{
			QFile f(leftPath);
			f.open(QIODevice::WriteOnly);
			f.write("x\n");
		}
		{
			QFile f(rightPath);
			f.open(QIODevice::WriteOnly);
			f.write("y\n");
		}
		MainWindow window;
		window.openBlankComparison();
		window.handleIncomingPaths({ leftPath, rightPath });
		QCoreApplication::processEvents();
		const auto tabs = window.findChildren<FileCompareView *>();
		bool blankLeft = false;
		for (const FileCompareView *view : tabs)
		{
			bool blank = true;
			for (const QString &path : view->paths())
				blank = blank && path.isEmpty();
			blankLeft = blankLeft || blank;
		}
		printf("comparisons: %lld, placeholder left: %d\n",
			static_cast<long long>(tabs.size()), blankLeft);
		return (tabs.size() == 1 && !blankLeft) ? 0 : 1;
	}

	if (parser.isSet(selftestFolder3Opt))
	{
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const QString roots[3] = { dir.filePath(QStringLiteral("A")),
			dir.filePath(QStringLiteral("B")), dir.filePath(QStringLiteral("C")) };
		const auto put = [&roots](int side, const QString &rel,
			const QByteArray &content) {
			const QString path = roots[side] + QLatin1Char('/') + rel;
			QDir().mkpath(QFileInfo(path).absolutePath());
			QFile f(path);
			f.open(QIODevice::WriteOnly);
			f.write(content);
		};
		for (int i = 0; i < 3; ++i)
		{
			put(i, QStringLiteral("same.txt"), "x" + QByteArray(1, '\n'));
			put(i, QStringLiteral("diff-left.txt"), i == 0 ? "a\n" : "b\n");
			put(i, QStringLiteral("diff-middle.txt"), i == 1 ? "a\n" : "b\n");
			put(i, QStringLiteral("diff-right.txt"), i == 2 ? "a\n" : "b\n");
			put(i, QStringLiteral("diff-all.txt"), QByteArray::number(i) + "\n");
			put(i, QStringLiteral("sub/nested.txt"), i == 2 ? "n2\n" : "n\n");
		}
		put(0, QStringLiteral("left-only.txt"), "u\n");
		put(1, QStringLiteral("middle-only.txt"), "u\n");
		put(2, QStringLiteral("right-only.txt"), "u\n");
		put(1, QStringLiteral("no-left.txt"), "m\n");
		put(2, QStringLiteral("no-left.txt"), "m\n");

		const lm::FolderCompareResult result = lm::compareFolders(
			{ roots[0], roots[1], roots[2] }, true);
		using Item = lm::FolderCompareItem;
		QHash<QString, const Item *> byName;
		for (const Item &item : result.items)
			byName.insert(item.name, &item);
		const auto is = [&byName](const QString &name, Item::Category cat,
			Item::ThreeWayInfo info = Item::NoInfo) {
			const Item *item = byName.value(name);
			const bool ok = item != nullptr && item->category == cat
				&& (info == Item::NoInfo || item->threeWay == info);
			if (!ok)
				fprintf(stderr, "unexpected: %s cat=%d 3way=%d\n",
					qPrintable(name), item ? item->category : -1,
					item ? item->threeWay : -1);
			return ok;
		};
		bool ok = result.sides == 3 && result.items.size() == 11;
		ok = is(QStringLiteral("same.txt"), Item::Identical) && ok;
		ok = is(QStringLiteral("diff-left.txt"), Item::Different,
			Item::MiddleRightIdentical) && ok;
		ok = is(QStringLiteral("diff-middle.txt"), Item::Different,
			Item::LeftRightIdentical) && ok;
		ok = is(QStringLiteral("diff-right.txt"), Item::Different,
			Item::LeftMiddleIdentical) && ok;
		ok = is(QStringLiteral("diff-all.txt"), Item::Different) && ok;
		ok = is(QStringLiteral("left-only.txt"), Item::LeftOnly) && ok;
		ok = is(QStringLiteral("middle-only.txt"), Item::MiddleOnly) && ok;
		ok = is(QStringLiteral("right-only.txt"), Item::RightOnly) && ok;
		ok = is(QStringLiteral("no-left.txt"), Item::MissingLeft) && ok;
		ok = is(QStringLiteral("nested.txt"), Item::Different,
			Item::LeftMiddleIdentical) && ok;
		ok = is(QStringLiteral("sub"), Item::Different) && ok;
		printf("items: %lld, different: %d, unique: %d, identical: %d, ok: %d\n",
			static_cast<long long>(result.items.size()), result.different,
			result.unique, result.identical, ok);
		return (ok && result.different == 7 && result.unique == 3
			&& result.identical == 1) ? 0 : 1;
	}

	if (parser.isSet(selftestArchiveOpt))
	{
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		// two small archives with one differing file, one identical
		// file and one side-only file each; zip on one side, tar.gz on
		// the other so both the format and the filter paths run
		struct Entry { const char *name; const char *content; };
		const auto writeArchive = [](const QString &path, bool tarGz,
			std::initializer_list<Entry> entries) {
			struct archive *a = archive_write_new();
			if (tarGz)
			{
				archive_write_set_format_pax_restricted(a);
				archive_write_add_filter_gzip(a);
			}
			else
				archive_write_set_format_zip(a);
			bool ok = archive_write_open_filename(a,
				QFile::encodeName(path).constData()) == ARCHIVE_OK;
			for (const Entry &e : entries)
			{
				if (!ok)
					break;
				struct archive_entry *entry = archive_entry_new();
				archive_entry_set_pathname(entry, e.name);
				archive_entry_set_size(entry,
					static_cast<la_int64_t>(strlen(e.content)));
				archive_entry_set_filetype(entry, AE_IFREG);
				archive_entry_set_perm(entry, 0644);
				ok = archive_write_header(a, entry) == ARCHIVE_OK
					&& archive_write_data(a, e.content,
						strlen(e.content)) >= 0;
				archive_entry_free(entry);
			}
			ok = archive_write_close(a) == ARCHIVE_OK && ok;
			archive_write_free(a);
			return ok;
		};
		const QString zipPath = dir.filePath(QStringLiteral("left.zip"));
		const QString tgzPath = dir.filePath(QStringLiteral("right.tar.gz"));
		if (!writeArchive(zipPath, false,
				{ { "a.txt", "hello\n" }, { "sub/same.txt", "same\n" },
				  { "left-only.txt", "extra\n" } })
			|| !writeArchive(tgzPath, true,
				{ { "a.txt", "world\n" }, { "sub/same.txt", "same\n" },
				  { "right-only.txt", "extra\n" } }))
		{
			fprintf(stderr, "building the fixtures failed\n");
			return 2;
		}
		if (!lm::isArchivePath(zipPath) || !lm::isArchivePath(tgzPath)
			|| lm::isArchivePath(QStringLiteral("a.txt")))
		{
			fprintf(stderr, "isArchivePath misdetects\n");
			return 1;
		}
		const QString leftDir = dir.filePath(QStringLiteral("L"));
		const QString rightDir = dir.filePath(QStringLiteral("R"));
		QDir().mkpath(leftDir);
		QDir().mkpath(rightDir);
		QString error;
		if (!lm::extractArchive(zipPath, leftDir, &error)
			|| !lm::extractArchive(tgzPath, rightDir, &error))
		{
			fprintf(stderr, "extract failed: %s\n", qPrintable(error));
			return 1;
		}
		const lm::FolderCompareResult result =
			lm::compareFolders(leftDir, rightDir, true);
		printf("different: %d, identical: %d, unique: %d\n",
			result.different, result.identical, result.unique);
		return (result.different == 1 && result.identical == 2
			&& result.unique == 2) ? 0 : 1;
	}

	if (parser.isSet(selftestOpenEnterOpt))
	{
		// regression: Enter in a path field triggers Compare, whose
		// handler used to delete the selector page while its line
		// edit's key handling was still on the stack (crashed)
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const QString leftPath = dir.filePath(QStringLiteral("left.txt"));
		const QString rightPath = dir.filePath(QStringLiteral("right.txt"));
		{
			QFile f(leftPath);
			f.open(QIODevice::WriteOnly);
			f.write("a\nb\n");
		}
		{
			QFile f(rightPath);
			f.open(QIODevice::WriteOnly);
			f.write("a\nc\n");
		}
		MainWindow window;
		window.openSelector({ leftPath, rightPath });
		auto *selector = window.findChild<NewComparisonView *>();
		auto *pathEdit = selector != nullptr
			? selector->findChild<QLineEdit *>() : nullptr;
		if (pathEdit == nullptr)
			return 2;
		QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		QCoreApplication::sendEvent(pathEdit, &press);
		QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
		QCoreApplication::sendEvent(pathEdit, &release);
		QCoreApplication::processEvents();
		QCoreApplication::processEvents();
		const bool selectorClosed =
			window.findChild<NewComparisonView *>() == nullptr;
		const bool comparisonOpen =
			window.findChild<FileCompareView *>() != nullptr;
		printf("selector closed: %d, comparison open: %d\n",
			selectorClosed, comparisonOpen);
		return (selectorClosed && comparisonOpen) ? 0 : 1;
	}

	if (parser.isSet(selftestUndoRescanOpt))
	{
		// the README's old limitation: a recompare that rebuilds the
		// alignment of the edited pane must not clear its undo history
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const QString leftPath = dir.filePath(QStringLiteral("left.txt"));
		const QString rightPath = dir.filePath(QStringLiteral("right.txt"));
		{
			QFile f(leftPath);
			f.open(QIODevice::WriteOnly);
			f.write("a\np\nq\nb\n");
		}
		{
			QFile f(rightPath);
			f.open(QIODevice::WriteOnly);
			f.write("a\nb\n");
		}
		FileCompareView view;
		QString error;
		if (!view.compare({ leftPath, rightPath }, &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		const QStringList original{ QStringLiteral("a"), QStringLiteral("b") };
		const QStringList edited{ QStringLiteral("a"), QStringLiteral("p"),
			QString(), QStringLiteral("b") };
		bool ok = view.diffCount() == 1
			&& view.realLinesForTest(1) == original;
		// type over the first ghost: the pane gains a real line, so the
		// recompare must delete the leftover ghost from this very pane
		view.typeAtForTest(1, 1, QStringLiteral("p\n"));
		view.recompare();
		ok = ok && view.realLinesForTest(1) == edited;
		view.undoActive();
		const bool undone = view.realLinesForTest(1) == original;
		view.recompare();
		ok = ok && undone && view.diffCount() == 1;
		view.redoActive();
		const bool redone = view.realLinesForTest(1) == edited;
		view.undoActive();
		const bool undoneAgain = view.realLinesForTest(1) == original;
		printf("undone: %d, redone: %d, again: %d, ok: %d\n",
			undone, redone, undoneAgain, ok);
		return (ok && redone && undoneAgain) ? 0 : 1;
	}

	if (parser.isSet(selftestUndoGhostsOpt))
	{
		// undoing a merge restores the target's ghost filler as ghosts,
		// not as phantom real empty lines that would reach the file
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const QString leftPath = dir.filePath(QStringLiteral("left.txt"));
		const QString rightPath = dir.filePath(QStringLiteral("right.txt"));
		{
			QFile f(leftPath);
			f.open(QIODevice::WriteOnly);
			f.write("a\nx\ny\nb\n");
		}
		{
			QFile f(rightPath);
			f.open(QIODevice::WriteOnly);
			f.write("a\nb\n");
		}
		FileCompareView view;
		QString error;
		if (!view.compare({ leftPath, rightPath }, &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		const QStringList original{ QStringLiteral("a"), QStringLiteral("b") };
		const QStringList merged{ QStringLiteral("a"), QStringLiteral("x"),
			QStringLiteral("y"), QStringLiteral("b") };
		view.gotoFirstDiff();
		view.copyCurrentDiff(0);
		bool ok = view.diffCount() == 0
			&& view.realLinesForTest(1) == merged;
		view.undoActive();
		const bool undone = view.realLinesForTest(1) == original
			&& view.diffCount() == 1;
		view.redoActive();
		const bool redone = view.realLinesForTest(1) == merged
			&& view.diffCount() == 0;
		printf("merged ok: %d, undone: %d, redone: %d\n", ok, undone, redone);
		return (ok && undone && redone) ? 0 : 1;
	}

	if (parser.isSet(selftestMerge3Opt))
	{
		const QStringList files = parser.positionalArguments();
		if (files.size() != 3)
			return 2;
		FileCompareView view;
		QString error;
		if (!view.compare(files, &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		const int initial = view.diffCount();
		// the user's workflow: with the left pane active, Alt+Right pushes
		// left->middle; recompare, focus the middle pane, Alt+Right again
		// pushes middle->right (WinMerge's pane-relative MenuIDtoXY)
		view.copyAllToRight();          // active pane 0: left -> middle
		view.recompare();
		const int afterFirst = view.diffCount();
		view.focusNextPane();           // active pane 1 (middle)
		view.copyAllToRight();          // middle -> right
		view.recompare();
		const int afterSecond = view.diffCount();
		printf("initial: %d, after left->middle: %d, after middle->right: %d\n",
			initial, afterFirst, afterSecond);
		// after the first merge the block still differs against the right
		// pane; only the second merge zeroes the comparison
		return (initial > 0 && afterFirst > 0 && afterSecond == 0) ? 0 : 1;
	}

	if (parser.isSet(selftestImageOpt))
	{
		const QStringList files = parser.positionalArguments();
		if (files.size() != 2)
			return 2;
		ImageCompareView view;
		QString error;
		if (!view.compare(files.at(0), files.at(1), &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		const int initial = view.diffCount();
		view.copyAllFrom(0);
		const int merged = view.diffCount();
		view.undo();
		const int undone = view.diffCount();
		view.redo();
		const int redone = view.diffCount();
		printf("initial: %d, merged: %d, undone: %d, redone: %d\n",
			initial, merged, undone, redone);
		return (initial > 0 && merged == 0 && undone == initial
			&& redone == 0) ? 0 : 1;
	}

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
		view.undo();
		const int undone = view.diffCount();
		view.redo();
		const int redone = view.diffCount();
		if (!view.saveModified(&error))
		{
			fprintf(stderr, "save failed: %s\n", qPrintable(error));
			return 2;
		}
		QFile leftFile(files.at(0)), rightFile(files.at(1));
		leftFile.open(QIODevice::ReadOnly);
		rightFile.open(QIODevice::ReadOnly);
		const bool equal = leftFile.readAll() == rightFile.readAll();
		printf("initial: %d, merged: %d, undone: %d, redone: %d, "
			"files equal: %d\n", initial, merged, undone, redone, equal);
		return (initial > 0 && merged == 0 && undone == initial
			&& redone == 0 && equal) ? 0 : 1;
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
		const bool allDirs = QFileInfo(args.at(0)).isDir()
			&& QFileInfo(args.at(1)).isDir() && QFileInfo(args.at(2)).isDir();
		if (allDirs)
			window.openFolderComparison(args);
		else
			window.openFileComparison(args);
	}
	else if (!args.isEmpty())
	{
		window.openSelector(args);
	}
	else if (OptionsDialog::showSelectorAtStartup())
	{
		// WinMerge's "show Select Files or Folders at startup" option
		window.openSelector();
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
