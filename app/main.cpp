// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge: Qt application entry point.
#include <functional>
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QCheckBox>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QLineEdit>
#include <QSettings>
#include <QStyleHints>
#include <QToolTip>
#include <QTemporaryDir>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QFileSystemModel>
#include <QImage>
#include <QPushButton>
#include <QTableView>
#include <QTabWidget>
#include <QTreeWidget>
#include <cstdio>
#include <cstring>
#include "DiffTextEdit.h"
#include "ImagePane.h"
#include "MessageBoxes.h"
#include <QTextBrowser>
#include <QThread>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QTranslator>
#include "FileCompareView.h"
#include "TableCompareView.h"
#include "ImageCompareView.h"
#include "FileOps.h"
#include "FolderCompareDriver.h"
#include "FolderCompareView.h"
#include <archive.h>
#include <archive_entry.h>
#include <QDir>
#include "AboutDialog.h"
#include "DesktopIntegration.h"
#include "ArchiveCompare.h"
#include "MainWindow.h"
#include "NewComparisonView.h"
#include "EngineOptions.h"
#include "ItemCheckStyle.h"
#include "OptionsDialog.h"
#include "Theme.h"
#ifdef LM_HAVE_PORTAL
#include "PortalAppearance.h"
#include <QDBusVariant>
#endif
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

/** A small zip or tar.gz with text entries, for the archive selftests. */
struct TestArchiveEntry
{
	const char *name;
	const char *content;
};

bool writeTestArchive(const QString &path, bool tarGz,
	std::initializer_list<TestArchiveEntry> entries)
{
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
	for (const TestArchiveEntry &e : entries)
	{
		if (!ok)
			break;
		struct archive_entry *entry = archive_entry_new();
		archive_entry_set_pathname(entry, e.name);
		archive_entry_set_size(entry, static_cast<la_int64_t>(strlen(e.content)));
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		ok = archive_write_header(a, entry) == ARCHIVE_OK
			&& archive_write_data(a, e.content, strlen(e.content)) >= 0;
		archive_entry_free(entry);
	}
	ok = archive_write_close(a) == ARCHIVE_OK && ok;
	archive_write_free(a);
	return ok;
}

} // namespace

int main(int argc, char *argv[])
{
	// menu integration runs headless (package-manager hooks): handle it
	// before a GUI application needs a display
	const int integration = lm::runDesktopIntegrationCommand(argc, argv);
	if (integration >= 0)
		return integration;

	QApplication app(argc, argv);
	QGuiApplication::setDesktopFileName(QStringLiteral("libremerge"));
	QApplication::setApplicationName(QStringLiteral("LibreMerge"));
	QApplication::setApplicationVersion(QStringLiteral("0.9.7"));
	QApplication::setOrganizationName(QStringLiteral("LibreMerge"));
	// selftests run on settings of their own, emptied first: they neither
	// depend on the user's options nor change them
	for (int i = 1; i < argc; ++i)
	{
		if (std::strncmp(argv[i], "--selftest", 10) == 0)
		{
			QApplication::setOrganizationName(QStringLiteral("LibreMerge-Selftest"));
			QSettings().clear();
			// informational boxes (identical files...) would wait forever
			// for a click in a headless run
			lm::setMessageSinkForTest([](const QString &) {});
			// and so would a question: none is expected, the answer is No
			lm::setQuestionSinkForTest([](const QString &, bool *) { return false; });
			break;
		}
	}
	// the folder scan runs the engine on QtConcurrent's pool; the engine
	// expects Windows' 1 MiB thread stacks and macOS gives 512 KiB, so use
	// glibc's 8 MiB default like DirScan's compare pool
	QThreadPool::globalInstance()->setStackSize(8 * 1024 * 1024);
	// the whole application follows the chosen theme from the first window
	lm::Theme::instance()->applyToApplication();

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
	QCommandLineOption selftestMarkerOpt(QStringLiteral("selftest-marker"),
		QStringLiteral("Verify insertion markers where one side lacks text (for testing)"));
	parser.addOption(selftestMarkerOpt);
	QCommandLineOption selftestFolderSyncOpt(QStringLiteral("selftest-folder-sync"),
		QStringLiteral("Save a file opened from a folder compare and verify its row updates (for testing)"));
	parser.addOption(selftestFolderSyncOpt);
	QCommandLineOption selftestFolder3OpsOpt(QStringLiteral("selftest-folder3-ops"),
		QStringLiteral("Copy and delete rows in a 3-way folder compare and verify (for testing)"));
	parser.addOption(selftestFolder3OpsOpt);
	QCommandLineOption selftestFolderContentOpt(QStringLiteral("selftest-folder-content"),
		QStringLiteral("Verify the folder compare's Full Contents method: text diff, ignore options, file types (for testing)"));
	parser.addOption(selftestFolderContentOpt);
	QCommandLineOption selftestCompareMethodsOpt(QStringLiteral("selftest-compare-methods"),
		QStringLiteral("Run a folder compare with each of WinMerge's seven compare methods and verify (for testing)"));
	parser.addOption(selftestCompareMethodsOpt);
	QCommandLineOption selftestShowFiltersOpt(QStringLiteral("selftest-show-filters"),
		QStringLiteral("Toggle the folder view's View menu filters, 2- and 3-way, and verify (for testing)"));
	parser.addOption(selftestShowFiltersOpt);
	parser.addOption(QCommandLineOption(QStringLiteral("install-desktop-integration"),
		QStringLiteral("Linux AppImage: add LibreMerge to the applications menu")));
	parser.addOption(QCommandLineOption(QStringLiteral("remove-desktop-integration"),
		QStringLiteral("Linux AppImage: remove it from the applications menu")));
	parser.addOption(QCommandLineOption(QStringLiteral("data-home"),
		QStringLiteral("With the two options above: the data directory to use instead of $XDG_DATA_HOME or ~/.local/share"),
		QStringLiteral("dir")));
	QCommandLineOption selftestDesktopOpt(QStringLiteral("selftest-desktop-integration"),
		QStringLiteral("Install and remove the menu integration in a scratch data home (for testing)"));
	parser.addOption(selftestDesktopOpt);
	QCommandLineOption screenshotAboutOpt(QStringLiteral("screenshot-about"),
		QStringLiteral("Render the About box (and its contributors list) to <file> and exit (for testing)"),
		QStringLiteral("file"));
	parser.addOption(screenshotAboutOpt);
	QCommandLineOption selftestAboutOpt(QStringLiteral("selftest-about"),
		QStringLiteral("Verify the About box contents (for testing)"));
	parser.addOption(selftestAboutOpt);
	QCommandLineOption screenshotOptionsOpt(QStringLiteral("screenshot-options"),
		QStringLiteral("Render every Options page to <file>-<n>.png and exit (for testing)"),
		QStringLiteral("file"));
	parser.addOption(screenshotOptionsOpt);
	QCommandLineOption selftestOptionsOpt(QStringLiteral("selftest-options"),
		QStringLiteral("Verify the Options dialog's page tree, titles and per-page defaults (for testing)"));
	parser.addOption(selftestOptionsOpt);
	QCommandLineOption selftestGeneralOptionsOpt(QStringLiteral("selftest-general-options"),
		QStringLiteral("Exercise Close with Esc, Preserve file time, Close the selection on Compare and auto completion (for testing)"));
	parser.addOption(selftestGeneralOptionsOpt);
	QCommandLineOption selftestIdenticalOpt(QStringLiteral("selftest-identical"),
		QStringLiteral("Verify the identical files message, the self-compare and the Message Boxes page (for testing)"));
	parser.addOption(selftestIdenticalOpt);
	QCommandLineOption selftestReloadOpt(QStringLiteral("selftest-reload"),
		QStringLiteral("Change compared files behind the comparison and verify the reload question, File > Reload and the auto-reload modes (for testing)"));
	parser.addOption(selftestReloadOpt);
	QCommandLineOption selftestThemeOpt(QStringLiteral("selftest-theme"),
		QStringLiteral("Switch the theme from the Options dialog and verify the whole application follows (for testing)"));
	parser.addOption(selftestThemeOpt);
	QCommandLineOption selftestPortalOpt(QStringLiteral("selftest-portal"),
		QStringLiteral("Linux: follow the desktop portal's color scheme from dark to light; needs packaging/fake_portal.py (for testing)"));
	parser.addOption(selftestPortalOpt);
	QCommandLineOption selftestArchiveMixedOpt(QStringLiteral("selftest-archive-mixed"),
		QStringLiteral("Compare a folder against an archive, 2- and 3-way (for testing)"));
	parser.addOption(selftestArchiveMixedOpt);
	QCommandLineOption selftestQtI18nOpt(QStringLiteral("selftest-qt-i18n"),
		QStringLiteral("Verify Qt's own strings are translated for the UI language (for testing)"));
	parser.addOption(selftestQtI18nOpt);
	QCommandLineOption selftestMenuRolesOpt(QStringLiteral("selftest-menu-roles"),
		QStringLiteral("Verify every menu-bar action has an explicit macOS menu role (for testing)"));
	parser.addOption(selftestMenuRolesOpt);
	QCommandLineOption selftestAppMenuOpt(QStringLiteral("selftest-app-menu"),
		QStringLiteral("Verify which actions macOS merged into the application menu (for testing)"));
	parser.addOption(selftestAppMenuOpt);
	parser.process(app);

	if (parser.isSet(selftestArchiveMixedOpt))
	{
		// the open screen part expects Compare to close it
		QSettings().setValue(QStringLiteral("General/CloseSelectorOnCompare"), true);
		// WinMerge's DecompressArchive handles each side on its own: an
		// archive compares against a plain folder. Enters through the
		// drop/Finder route (handleIncomingPaths), the one users hit most
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const QString folder = dir.filePath(QStringLiteral("pasta"));
		const QString other = dir.filePath(QStringLiteral("outra"));
		for (const QString &root : { folder, other })
		{
			QDir().mkpath(root);
			const auto put = [&root](const char *name, const char *content) {
				QFile f(root + QLatin1Char('/') + QLatin1String(name));
				f.open(QIODevice::WriteOnly);
				f.write(content);
			};
			put("same.txt", "same\n");
			put("changed.txt", "folder side\n");
			put("folder-only.txt", "x\n");
		}
		const QString zipPath = dir.filePath(QStringLiteral("pacote.zip"));
		if (!writeTestArchive(zipPath, false,
				{ { "same.txt", "same\n" }, { "changed.txt", "archive side\n" },
				  { "archive-only.txt", "y\n" } }))
			return 2;
		const auto waitFor = [](FolderCompareView *view) {
			for (int i = 0; i < 400 && view->isComparingForTest(); ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
		};
		using Item = lm::FolderCompareItem;
		bool ok = MainWindow::allFolderLike({ folder, zipPath })
			&& MainWindow::allFolderLike({ folder, zipPath, other })
			&& !MainWindow::allFolderLike({ folder, dir.filePath(QStringLiteral("x.txt")) });

		MainWindow window;
		window.handleIncomingPaths({ folder, zipPath });
		auto *view = window.findChild<FolderCompareView *>();
		if (view == nullptr)
		{
			fprintf(stderr, "no folder comparison opened\n");
			return 1;
		}
		waitFor(view);
		const int same = view->rowCategoryForTest(QStringLiteral("same.txt"));
		const int changed = view->rowCategoryForTest(QStringLiteral("changed.txt"));
		const int folderOnly = view->rowCategoryForTest(QStringLiteral("folder-only.txt"));
		const int archiveOnly = view->rowCategoryForTest(QStringLiteral("archive-only.txt"));
		ok = ok && same == Item::Identical && changed == Item::Different
			&& folderOnly == Item::LeftOnly && archiveOnly == Item::RightOnly;
		printf("2-way: same %d, changed %d, folder-only %d, archive-only %d\n",
			same, changed, folderOnly, archiveOnly);

		// the file opened from the archive side shows its path inside the
		// archive; the folder side keeps its real path
		view->activateRowForTest(QStringLiteral("changed.txt"));
		QCoreApplication::processEvents();
		FileCompareView *file = nullptr;
		for (FileCompareView *candidate : window.findChildren<FileCompareView *>())
			if (!candidate->paths().at(0).isEmpty())
				file = candidate;
		const QString rightCaption = file != nullptr ? file->sideCaption(1) : QString();
		const QString leftCaption = file != nullptr ? file->sideCaption(0) : QString();
		printf("captions: left '%s', right '%s'\n", qPrintable(leftCaption),
			qPrintable(rightCaption));
		ok = ok && leftCaption.isEmpty()
			&& rightCaption == QStringLiteral("pacote.zip/changed.txt");

		// 3-way: folder x archive x folder
		MainWindow window3;
		window3.handleIncomingPaths({ folder, zipPath, other });
		auto *view3 = window3.findChild<FolderCompareView *>();
		if (view3 == nullptr)
			return 1;
		waitFor(view3);
		const int changed3 = view3->rowCategoryForTest(QStringLiteral("changed.txt"));
		const int archiveOnly3 = view3->rowCategoryForTest(QStringLiteral("archive-only.txt"));
		printf("3-way: changed %d, archive-only %d\n", changed3, archiveOnly3);
		ok = ok && changed3 == Item::Different && archiveOnly3 == Item::MiddleOnly;

		// the open screen used to reject a folder with an archive as
		// "mixing files and folders"
		MainWindow windowSel;
		windowSel.openSelector({ folder, zipPath });
		auto *selector = windowSel.findChild<NewComparisonView *>();
		auto *pathEdit = selector != nullptr
			? selector->findChild<QLineEdit *>() : nullptr;
		if (pathEdit == nullptr)
			return 2;
		QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		QCoreApplication::sendEvent(pathEdit, &press);
		QCoreApplication::processEvents();
		QCoreApplication::processEvents();
		const qsizetype opened = windowSel.findChildren<FolderCompareView *>().size();
		const bool selectorGone = windowSel.findChild<NewComparisonView *>() == nullptr;
		printf("open screen: folder comparisons opened %lld, selector closed %d\n",
			static_cast<long long>(opened), selectorGone);
		ok = ok && opened == 1 && selectorGone;
		printf("ok: %d\n", ok);
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestQtI18nOpt))
	{
		// Qt's own strings (dialog buttons, text-field context menus, the
		// macOS application menu) come from qtbase_<lang>.qm, which the
		// packages must ship next to the app: the 0.9.4 dmg and AppImages
		// did not, so pt-BR users saw "Cancel" and "Paste" in English
		const QString language = locale.name();
		if (language.startsWith(QStringLiteral("en")))
		{
			printf("skipped: English UI\n");
			return 0;
		}
		// held in variables so lupdate does not copy these into LibreMerge's
		// own catalog: the answer must come from Qt's qtbase_<lang>.qm, or
		// this test would pass without it
		const char *themeContext = "QPlatformTheme";
		const char *cancelText = "Cancel";
		const char *textControlContext = "QWidgetTextControl";
		const char *pasteText = "&Paste";
		const QString cancel = QCoreApplication::translate(themeContext, cancelText);
		const QString paste = QCoreApplication::translate(textControlContext, pasteText);
		printf("%s: Cancel -> %s, &Paste -> %s (translations: %s)\n",
			qPrintable(language), qPrintable(cancel), qPrintable(paste),
			qPrintable(QLibraryInfo::path(QLibraryInfo::TranslationsPath)));
		return (cancel != QStringLiteral("Cancel")
			&& paste != QStringLiteral("&Paste")) ? 0 : 1;
	}

	if (parser.isSet(selftestMenuRolesOpt))
	{
		// no menu-bar action may be left to Qt's text heuristic (it once
		// promoted pt-BR "Sobreposicao" to the About item on macOS), and
		// the application-menu roles each belong to exactly one action
		MainWindow window;
		const std::function<void(QWidget *)> expand = [&expand](QWidget *w) {
			for (QAction *action : w->actions())
				if (QMenu *submenu = action->menu())
				{
					emit submenu->aboutToShow(); // builds dynamic menus
					expand(submenu);
				}
		};
		expand(window.menuBar());
		int heuristic = 0, about = 0, prefs = 0, quit = 0;
		const std::function<void(QWidget *)> walk = [&](QWidget *w) {
			for (QAction *action : w->actions())
			{
				switch (action->menuRole())
				{
				case QAction::TextHeuristicRole:
					++heuristic;
					fprintf(stderr, "heuristic role: %s\n",
						qPrintable(action->text()));
					break;
				case QAction::AboutRole: ++about; break;
				case QAction::PreferencesRole: ++prefs; break;
				case QAction::QuitRole: ++quit; break;
				default: break;
				}
				if (QMenu *submenu = action->menu())
					walk(submenu);
			}
		};
		walk(window.menuBar());
		printf("heuristic: %d, about: %d, preferences: %d, quit: %d\n",
			heuristic, about, prefs, quit);
		return (heuristic == 0 && about == 1 && prefs == 1 && quit == 1)
			? 0 : 1;
	}

	if (parser.isSet(selftestDesktopOpt))
	{
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const QString home = dir.path();
		// a launcher path with a space and a reserved character exercises
		// the Exec quoting of the Desktop Entry spec
		const QString launcher = home + QStringLiteral("/My Apps/LibreMerge$1.AppImage");
		QStringList written;
		QString error;
		bool ok = lm::installDesktopIntegration(home, launcher, &written, &error);
		const QString desktopFile = home + QStringLiteral("/applications/libremerge.desktop");
		QFile file(desktopFile);
		file.open(QIODevice::ReadOnly);
		const QString entry = QString::fromUtf8(file.readAll());
		file.close();
		const QString expectedExec = QStringLiteral(
			"Exec=\"") + home + QStringLiteral("/My Apps/LibreMerge\\\\$1.AppImage\" %F");
		ok = ok && written.size() == 7 && entry.contains(expectedExec)
			&& entry.contains(QStringLiteral("TryExec=") + launcher)
			&& entry.contains(QStringLiteral("Name=LibreMerge"))
			&& entry.contains(QStringLiteral("Icon=libremerge"))
			&& entry.contains(QStringLiteral("X-LibreMerge-Launcher="))
			&& QFileInfo::exists(home + QStringLiteral("/icons/hicolor/512x512/apps/libremerge.png"));
		printf("installed: %lld files, exec ok %d\n",
			static_cast<long long>(written.size()), entry.contains(expectedExec));
		if (!ok)
			fprintf(stderr, "%s\n%s\n", qPrintable(error), qPrintable(entry));

		QStringList removed;
		ok = lm::removeDesktopIntegration(home, &removed, &error) && ok
			&& removed.size() == 7 && !QFileInfo::exists(desktopFile)
			&& !QFileInfo::exists(home + QStringLiteral("/icons/hicolor/16x16/apps/libremerge.png"));
		printf("removed: %lld files\n", static_cast<long long>(removed.size()));

		// an entry LibreMerge did not write is left alone
		QDir().mkpath(home + QStringLiteral("/applications"));
		QFile foreign(desktopFile);
		foreign.open(QIODevice::WriteOnly);
		foreign.write("[Desktop Entry]\nName=Mine\nExec=libremerge\n");
		foreign.close();
		const bool refused = !lm::removeDesktopIntegration(home, nullptr, &error)
			&& QFileInfo::exists(desktopFile);
		printf("foreign entry kept: %d\n", refused);
		ok = ok && refused;
		printf("ok: %d\n", ok);
		return ok ? 0 : 1;
	}

	if (parser.isSet(screenshotAboutOpt))
	{
		AboutDialog dialog;
		dialog.show();
		QCoreApplication::processEvents();
		const QString path = parser.value(screenshotAboutOpt);
		if (!dialog.grab().save(path))
			return 2;
		// the contributors list, rendered the way the button shows it
		QTextBrowser browser;
		browser.setMarkdown(AboutDialog::contributorsMarkdown());
		browser.resize(640, 1400);
		browser.show();
		QCoreApplication::processEvents();
		QString contributorsPath = path;
		contributorsPath.insert(contributorsPath.lastIndexOf(QLatin1Char('.')),
			QStringLiteral("-contributors"));
		return browser.grab().save(contributorsPath) ? 0 : 2;
	}

	if (parser.isSet(screenshotOptionsOpt))
	{
		// every tree item in display order, one image each
		OptionsDialog dialog;
		dialog.show();
		const QString path = parser.value(screenshotOptionsOpt);
		const int dot = path.lastIndexOf(QLatin1Char('.'));
		for (int i = 0; i < dialog.categoriesForTest().size(); ++i)
		{
			dialog.selectCategoryForTest(i);
			QCoreApplication::processEvents();
			QString target = path;
			target.insert(dot < 0 ? target.size() : dot,
				QStringLiteral("-%1").arg(i));
			if (!dialog.grab().save(target))
				return 2;
		}
		return 0;
	}

	if (parser.isSet(selftestOptionsOpt))
	{
		// WinMerge's Preferences layout: the page tree, a category opening
		// its first page with the path in the title, the backup switch on
		// Backup Files, and Defaults resetting the current page only.
		// Nothing is saved: the dialog is never accepted.
		OptionsDialog dialog;
		const QList<OptionsDialog::CategoryInfo> tree = dialog.categoriesForTest();
		const int depths[] = { 0, 0, 1, 1, 0, 0 };
		const int pages[] = { OptionsDialog::GeneralPage, -1,
			OptionsDialog::ComparePage, OptionsDialog::FolderPage,
			OptionsDialog::MessageBoxesPage, OptionsDialog::BackupPage };
		bool ok = tree.size() == 6;
		for (int i = 0; ok && i < tree.size(); ++i)
			ok = tree.at(i).depth == depths[i] && tree.at(i).page == pages[i];
		printf("tree: %s\n", ok ? "ok" : "wrong");

		// the Compare category shows Compare > General
		const int expectedPage[] = { OptionsDialog::GeneralPage,
			OptionsDialog::ComparePage, OptionsDialog::ComparePage,
			OptionsDialog::FolderPage, OptionsDialog::MessageBoxesPage,
			OptionsDialog::BackupPage };
		for (int i = 0; ok && i < 6; ++i)
		{
			dialog.selectCategoryForTest(i);
			const bool nested = dialog.windowTitle().contains(QStringLiteral(" > "));
			printf("item %d: page %d, title \"%s\"\n", i,
				dialog.currentPageForTest(), qPrintable(dialog.windowTitle()));
			ok = dialog.currentPageForTest() == expectedPage[i]
				&& nested == (i >= 1 && i <= 3);
		}

		const auto boxes = [&dialog](OptionsDialog::Page page) {
			return dialog.pageForTest(page)->findChildren<QCheckBox *>();
		};
		ok = ok && boxes(OptionsDialog::GeneralPage).size() == 7
			&& boxes(OptionsDialog::BackupPage).size() == 1;

		// Defaults on another page leaves General alone
		for (QCheckBox *box : boxes(OptionsDialog::GeneralPage))
			box->setChecked(true);
		boxes(OptionsDialog::BackupPage).first()->setChecked(false);
		dialog.selectCategoryForTest(2); // Compare > General
		dialog.restoreDefaultsForTest();
		for (QCheckBox *box : boxes(OptionsDialog::GeneralPage))
			ok = ok && box->isChecked();
		ok = ok && !boxes(OptionsDialog::BackupPage).first()->isChecked();
		dialog.selectCategoryForTest(0); // General
		dialog.restoreDefaultsForTest();
		// WinMerge's defaults: all off but "Automatically verify paths"
		const QString verifyPaths = OptionsDialog::tr(
			"Automatically verify paths in the \"Select Files or Folders\" screen");
		for (QCheckBox *box : boxes(OptionsDialog::GeneralPage))
			ok = ok && box->isChecked() == (box->text() == verifyPaths);
		dialog.selectCategoryForTest(5); // Backup Files
		dialog.restoreDefaultsForTest();
		ok = ok && boxes(OptionsDialog::BackupPage).first()->isChecked();

		// every row of the Message Boxes list shows its check box (the
		// macOS 27 style left all but the first one out)
		{
			dialog.selectCategoryForTest(4); // Message Boxes
			dialog.grab(); // lays the pages out
			auto *list = dialog.pageForTest(OptionsDialog::MessageBoxesPage)
				->findChild<QTreeWidget *>();
			const QImage image = list != nullptr
				? list->viewport()->grab().toImage() : QImage();
			const qreal ratio = image.devicePixelRatio();
			int rows = 0, drawn = 0;
			for (int i = 0; list != nullptr && i < list->topLevelItemCount(); ++i)
			{
				const QRect row = list->visualItemRect(list->topLevelItem(i));
				const QRect box(qRound((row.left() + 2) * ratio), qRound((row.top() + 2) * ratio),
					qRound(26 * ratio), qRound((row.height() - 4) * ratio));
				const QRgb background = image.pixel(box.left(), box.top());
				bool inked = false;
				for (int y = box.top(); y <= box.bottom() && !inked; ++y)
					for (int x = box.left(); x <= box.right() && !inked; ++x)
						inked = image.rect().contains(x, y) && image.pixel(x, y) != background;
				++rows;
				drawn += inked ? 1 : 0;
			}
			printf("item check boxes: %d of %d drawn (%s)\n", drawn, rows,
				lm::itemCheckBoxesNeedHelp() ? "painted here" : "platform style");
			ok = ok && rows > 0 && drawn == rows;
		}
		printf("options dialog: %s\n", ok ? "ok" : "FAILED");
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestThemeOpt))
	{
		// the theme lives in Options > General (not in the View menu) and
		// drives the whole application: on Linux the palette, on macOS the
		// appearance asked of the system
		bool ok = true;
		const auto check = [&ok](const char *what, bool passed) {
			printf("%s: %s\n", what, passed ? "ok" : "FAILED");
			ok = ok && passed;
		};
		const auto isDark = [](const QPalette &palette) {
			return palette.color(QPalette::Window).lightness()
				< palette.color(QPalette::WindowText).lightness();
		};
		lm::Theme *theme = lm::Theme::instance();
		const QColor systemWindow = qApp->palette().color(QPalette::Window);
		check("starts following the system", theme->mode() == lm::ThemeMode::System);
		int changes = 0;
		QObject::connect(theme, &lm::Theme::changed, [&changes]() { ++changes; });

		{
			MainWindow window;
			bool themeMenu = false;
			const std::function<void(QWidget *)> walk = [&](QWidget *w) {
				for (QAction *action : w->actions())
					if (QMenu *submenu = action->menu())
					{
						const QString title = submenu->title().remove(QLatin1Char('&'));
						themeMenu = themeMenu || title == QStringLiteral("Theme")
							|| title == QStringLiteral("Tema");
						walk(submenu);
					}
			};
			walk(window.menuBar());
			check("no Theme menu", !themeMenu);
		}

		OptionsDialog dialog;
		auto *combo = dialog.pageForTest(OptionsDialog::GeneralPage)
			->findChild<QComboBox *>(QStringLiteral("theme"));
		check("theme choice on the General page", combo != nullptr && combo->count() == 3
			&& combo->currentIndex() == 0);
		if (combo == nullptr)
			return 1;

		combo->setCurrentIndex(2); // Dark
		dialog.saveForTest();
		QCoreApplication::processEvents();
		check("dark saved", theme->mode() == lm::ThemeMode::Dark && theme->dark()
			&& QSettings().value(QStringLiteral("Appearance/Theme")).toString()
				== QStringLiteral("dark")
			&& changes == 1);
#ifdef Q_OS_MACOS
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
		// only the real platform answers the request (dmg validation)
		if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
			check("dark appearance requested",
				qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
#endif
#else
		check("dark application palette", isDark(qApp->palette())
			&& isDark(QToolTip::palette()));
#endif

		combo->setCurrentIndex(1); // Light
		dialog.saveForTest();
		QCoreApplication::processEvents();
		check("light saved", theme->mode() == lm::ThemeMode::Light && !theme->dark()
			&& changes == 2);
#ifdef Q_OS_MACOS
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
		if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
			check("light appearance requested",
				qApp->styleHints()->colorScheme() == Qt::ColorScheme::Light);
#endif
#else
		check("light application palette", !isDark(qApp->palette())
			&& !isDark(QToolTip::palette()));
#endif

		// Defaults: back to the system, and to the platform's own palette
		dialog.restoreDefaultsForTest();
		check("defaults follow the system", combo->currentIndex() == 0);
		dialog.saveForTest();
		QCoreApplication::processEvents();
		check("system restored", theme->mode() == lm::ThemeMode::System
			&& QSettings().value(QStringLiteral("Appearance/Theme")).toString()
				== QStringLiteral("system"));
#ifndef Q_OS_MACOS
		check("platform palette back",
			qApp->palette().color(QPalette::Window) == systemWindow
			&& isDark(qApp->palette()) == theme->dark());
#else
		Q_UNUSED(systemWindow);
#endif

		// saving the same choice again changes nothing
		const int before = changes;
		dialog.saveForTest();
		check("unchanged choice is quiet", changes == before);

#ifdef LM_HAVE_PORTAL
		// the portal's color-scheme values, bare (ReadOne, SettingChanged)
		// or wrapped twice (the deprecated Read)
		using Portal = lm::PortalAppearance;
		const QVariant twice = QVariant::fromValue(QDBusVariant(
			QVariant::fromValue(QDBusVariant(QVariant(2u)))));
		check("portal values",
			Portal::schemeFromValue(QVariant(1u)) == Portal::PreferDark
			&& Portal::schemeFromValue(QVariant(0u)) == Portal::NoPreference
			&& Portal::schemeFromValue(twice) == Portal::PreferLight
			&& Portal::schemeFromValue(QVariant(7u)) == Portal::Unavailable
			&& Portal::schemeFromValue(QVariant(QStringLiteral("dark"))) == Portal::Unavailable);
#endif
		printf("theme: %s\n", ok ? "ok" : "FAILED");
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestPortalOpt))
	{
#ifdef LM_HAVE_PORTAL
		// the fake portal says "prefer dark", then switches to "prefer
		// light": System mode follows it, the whole palette included
		const auto isDark = [](const QPalette &palette) {
			return palette.color(QPalette::Window).lightness()
				< palette.color(QPalette::WindowText).lightness();
		};
		lm::Theme *theme = lm::Theme::instance();
		const bool startedDark = theme->dark() && isDark(qApp->palette());
		printf("started dark: %d\n", startedDark);
		QEventLoop loop;
		QObject::connect(theme, &lm::Theme::changed, &loop, &QEventLoop::quit);
		QTimer::singleShot(15000, &loop, &QEventLoop::quit);
		loop.exec();
		const bool nowLight = !theme->dark() && !isDark(qApp->palette());
		printf("switched to light: %d\n", nowLight);
		return startedDark && nowLight ? 0 : 1;
#else
		printf("no desktop portal on this platform\n");
		return 0;
#endif
	}

	if (parser.isSet(selftestReloadOpt))
	{
		// WinMerge's file change checks (CheckFileChanged, OnFileReload and
		// DoSave's overwrite question) under the three auto-reload modes;
		// the test answers the questions and the save prompt
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		bool ok = true;
		const auto check = [&ok](bool condition, const char *what)
		{
			printf("%s: %s\n", what, condition ? "ok" : "FAILED");
			ok = ok && condition;
		};
		const auto settle = []()
		{
			for (int i = 0; i < 4; ++i)
				QCoreApplication::processEvents();
		};
		// another application's write: new content with a later time, so
		// the change shows whatever the sizes and the clock's resolution
		const auto write = [](const QString &path, const QByteArray &bytes)
		{
			const QDateTime before = QFileInfo(path).lastModified();
			QFile f(path);
			f.open(QIODevice::WriteOnly | QIODevice::Truncate);
			f.write(bytes);
			if (before.isValid())
				f.setFileTime(before.addSecs(3), QFileDevice::FileModificationTime);
		};
		const auto read = [](const QString &path)
		{
			QFile f(path);
			f.open(QIODevice::ReadOnly);
			return f.readAll();
		};
		const auto setMode = [](OptionsDialog::AutoReload mode)
		{
			QSettings().setValue(QStringLiteral("General/AutoReloadModifiedFiles"),
				static_cast<int>(mode));
		};
		const auto reloadQuestion = [](const QString &path) {
			return QCoreApplication::translate("MessageBoxes",
				"Another application updated\n%1\nsince last scan.\n\nReload?").arg(path);
		};
		const auto overwriteQuestion = [](const QString &path) {
			return QCoreApplication::translate("MessageBoxes",
				"Another application updated\n%1\nsince LibreMerge loaded it.\n\nOverwrite?")
				.arg(path);
		};
		QStringList asked;
		bool answerYes = true;
		bool dontAskAgain = false;
		lm::setQuestionSinkForTest([&](const QString &text, bool *dontAsk) {
			asked.append(text);
			*dontAsk = dontAskAgain;
			return answerYes;
		});
		QStringList shown;
		lm::setMessageSinkForTest([&shown](const QString &text) { shown.append(text); });
		int savePrompts = 0;
		int saveChoice = 2; // 0 cancels, 1 saves, 2 discards
		MainWindow::setSavePromptForTest([&]() { ++savePrompts; return saveChoice; });
		lm::setCompareOptionsForTest(0);
		const QString a = dir.filePath(QStringLiteral("a.txt"));
		const QString b = dir.filePath(QStringLiteral("b.txt"));
		const QStringList three = { QStringLiteral("one"), QStringLiteral("two"),
			QStringLiteral("three") };
		write(a, "one\ntwo\nthree\n");
		write(b, "one\ntwo\nthree\n");

		// IsFileChangedOnDisk
		{
			const lm::FileStamp stamp = lm::fileStamp(b);
			bool fine = lm::fileChangedOnDisk(b, stamp) == lm::FileChange::NoChange;
			{
				QFile f(b);
				f.open(QIODevice::Append);
				f.setFileTime(stamp.modified.addSecs(-90), QFileDevice::FileModificationTime);
			}
			fine = fine && lm::fileChangedOnDisk(b, stamp) == lm::FileChange::Changed;
			{
				QFile f(b);
				f.open(QIODevice::Append);
				f.write("four\n");
				f.setFileTime(stamp.modified, QFileDevice::FileModificationTime);
			}
			fine = fine && lm::fileChangedOnDisk(b, stamp) == lm::FileChange::Changed;
			QFile::remove(b);
			fine = fine && lm::fileChangedOnDisk(b, stamp) == lm::FileChange::Removed
				&& lm::fileChangedOnDisk(QString(), lm::FileStamp()) == lm::FileChange::Removed;
			check(fine, "file stamps: another time, another size, removal");
			write(b, "one\ntwo\nthree\n");
		}

		// the option: WinMerge's default, on the General page
		{
			bool fine = OptionsDialog::autoReloadModifiedFiles()
				== OptionsDialog::AutoReloadOnWindowActivated;
			OptionsDialog dialog;
			auto *combo = dialog.pageForTest(OptionsDialog::GeneralPage)
				->findChild<QComboBox *>(QStringLiteral("autoReload"));
			fine = fine && combo != nullptr && combo->count() == 3
				&& combo->currentIndex() == 1;
			if (combo != nullptr)
			{
				combo->setCurrentIndex(2);
				dialog.saveForTest();
				fine = fine && OptionsDialog::autoReloadModifiedFiles()
					== OptionsDialog::AutoReloadImmediately;
				dialog.selectCategoryForTest(0);
				dialog.restoreDefaultsForTest();
				fine = fine && combo->currentIndex() == 1;
				dialog.saveForTest();
			}
			check(fine && OptionsDialog::autoReloadModifiedFiles()
				== OptionsDialog::AutoReloadOnWindowActivated,
				"Options > General: auto-reload, on window activated by default");
		}

		// "Only on window activated"
		{
			MainWindow window;
			window.openFileComparison({ a, b });
			settle();
			auto *view = window.findChild<FileCompareView *>();
			if (view == nullptr)
				return 2;
			check(asked.isEmpty() && view->diffCount() == 0, "opening asks nothing");
			write(b, "one\n2\nthree\nfour\n");
			window.applicationActivated();
			settle();
			check(asked == QStringList{ reloadQuestion(b) }
				&& view->realLinesForTest(1).size() == 4 && view->diffCount() > 0
				&& !view->isModified(), "window activated: asked, and reloaded on Yes");
			asked.clear();
			window.applicationActivated();
			settle();
			check(asked.isEmpty(), "nothing left to ask after the reload");

			// No keeps the comparison and asks again at the next trigger
			answerYes = false;
			write(b, "one\n2\n");
			window.applicationActivated();
			settle();
			window.applicationActivated();
			settle();
			check(asked.size() >= 2 && view->realLinesForTest(1).size() == 4,
				"No: nothing reloaded, asked again the next time");
			answerYes = true;

			// a file that went away is no change to ask about
			QFile::remove(b);
			asked.clear();
			window.applicationActivated();
			settle();
			check(asked.isEmpty(), "a removed file asks nothing");
			write(b, "one\ntwo\nthree\n");
		}

		// "Disabled" only silences the application coming to the front: a
		// tab becoming current and Recompare still check (OnMDIActivate,
		// Rescan)
		setMode(OptionsDialog::AutoReloadDisabled);
		{
			MainWindow window;
			window.openFileComparison({ a, b });
			window.openFileComparison({ b, a });
			settle();
			auto *tabs = window.findChild<QTabWidget *>();
			auto *view = qobject_cast<FileCompareView *>(tabs->widget(0));
			if (view == nullptr)
				return 2;
			asked.clear();
			answerYes = false;
			write(a, "one\ntwo\n");
			tabs->setCurrentIndex(1);
			settle();
			asked.clear(); // the second tab compares the same files
			window.applicationActivated();
			settle();
			const bool silent = asked.isEmpty();
			tabs->setCurrentIndex(0);
			settle();
			const bool onTab = asked == QStringList{ reloadQuestion(a) };
			asked.clear();
			view->refreshByUser();
			settle();
			check(silent && onTab && asked == QStringList{ reloadQuestion(a) },
				"disabled: silent on activation, asks on tab switch and Recompare");
			answerYes = true;
			write(a, "one\ntwo\nthree\n");
		}

		// "Immediately": the files are watched
		setMode(OptionsDialog::AutoReloadImmediately);
		{
			MainWindow window;
			window.openFileComparison({ a, b });
			settle();
			auto *view = window.findChild<FileCompareView *>();
			auto *tabs = window.findChild<QTabWidget *>();
			if (view == nullptr)
				return 2;
			const QStringList watched = window.watchedPathsForTest();
			check(watched.contains(a) && watched.contains(b)
				&& watched.contains(dir.path()), "immediately: files and folder watched");
			const auto waitForQuestion = [&asked]()
			{
				QElapsedTimer timer;
				timer.start();
				while (asked.isEmpty() && timer.elapsed() < 15000)
					QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
			};
			asked.clear();
			write(b, "one\ntwo\nthree\nwatched\n");
			waitForQuestion();
			settle();
			check(asked == QStringList{ reloadQuestion(b) }
				&& view->realLinesForTest(1).size() == 4, "immediately: a change asks by itself");

			// an editor that writes a new file over the old one
			asked.clear();
			const QString fresh = dir.filePath(QStringLiteral("b.new"));
			write(fresh, "replaced\n");
			std::rename(QFile::encodeName(fresh).constData(), QFile::encodeName(b).constData());
			waitForQuestion();
			settle();
			check(asked == QStringList{ reloadQuestion(b) }
				&& view->realLinesForTest(1) == QStringList{ QStringLiteral("replaced") }
				&& window.watchedPathsForTest().contains(b),
				"immediately: a replaced file asks too, and stays watched");

			// a neighbor in the same folder is none of the comparison's
			// business, even while a declined change is still pending
			answerYes = false;
			asked.clear();
			write(b, "declined\n");
			waitForQuestion();
			settle();
			const bool declined = asked.size() == 1;
			asked.clear();
			write(dir.filePath(QStringLiteral("neighbor.txt")), "noise\n");
			QElapsedTimer quiet;
			quiet.start();
			while (quiet.elapsed() < 2000)
				QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
			check(declined && asked.isEmpty(),
				"immediately: a neighbor in the folder asks nothing");
			answerYes = true;

			emit tabs->tabCloseRequested(0);
			settle();
			check(tabs->count() == 0 && window.watchedPathsForTest().isEmpty(),
				"closing the comparison drops its watches");
			write(b, "one\ntwo\nthree\n");
		}
		setMode(OptionsDialog::AutoReloadOnWindowActivated);
		{
			MainWindow window;
			window.openFileComparison({ a, b });
			settle();
			check(window.watchedPathsForTest().isEmpty(), "the other modes watch nothing");
		}

		// unsaved changes are offered for saving before a reload
		// (PromptAndSaveIfNeeded)
		write(b, "one\n2\nthree\n");
		{
			MainWindow window;
			window.openFileComparison({ a, b });
			settle();
			auto *view = window.findChild<FileCompareView *>();
			if (view == nullptr)
				return 2;
			view->typeAtForTest(0, 0, QStringLiteral("edited "));
			write(b, "one\n2\nthree\nfour\n");
			asked.clear();
			savePrompts = 0;
			saveChoice = 0;
			window.applicationActivated();
			settle();
			check(asked.size() == 1 && savePrompts == 1 && view->isModified()
				&& view->realLinesForTest(1).size() == 3,
				"unsaved changes: Cancel reloads nothing");
			saveChoice = 1;
			window.applicationActivated();
			settle();
			check(savePrompts == 2 && !view->isModified()
				&& read(a).startsWith("edited one")
				&& view->realLinesForTest(0).value(0) == QStringLiteral("edited one")
				&& view->realLinesForTest(1).size() == 4,
				"unsaved changes: Save writes them, then reloads");
			view->typeAtForTest(0, 0, QStringLiteral("dropped "));
			write(b, "one\n2\n");
			saveChoice = 2;
			window.applicationActivated();
			settle();
			check(!view->isModified()
				&& view->realLinesForTest(0).value(0) == QStringLiteral("edited one")
				&& view->realLinesForTest(1).size() == 2,
				"unsaved changes: Discard drops them and reloads");
			write(a, "one\ntwo\nthree\n");
		}

		// DoSave: writing over a file changed elsewhere asks first
		write(b, "one\n2\nthree\n");
		{
			MainWindow window;
			window.openFileComparison({ a, b });
			settle();
			auto *view = window.findChild<FileCompareView *>();
			if (view == nullptr)
				return 2;
			view->typeAtForTest(1, 0, QStringLiteral("mine "));
			write(b, "theirs\n");
			asked.clear();
			answerYes = false;
			QString error;
			const bool carriedOn = view->saveModified(&error);
			check(carriedOn && asked == QStringList{ overwriteQuestion(b) }
				&& read(b) == "theirs\n" && view->isModified(),
				"saving over a changed file: No leaves the file and the edits");
			answerYes = true;
			view->saveModified(&error);
			check(asked.size() == 2 && read(b).startsWith("mine one") && !view->isModified(),
				"saving over a changed file: Yes overwrites");
			asked.clear();
			view->typeAtForTest(1, 0, QStringLiteral("again "));
			view->saveModified(&error);
			settle();
			check(asked.isEmpty() && read(b).startsWith("again mine one"),
				"the next save asks nothing");
		}

		// "Don't ask this question again" keeps the answer
		write(b, "one\ntwo\nthree\n");
		{
			MainWindow window;
			window.openFileComparison({ a, b });
			settle();
			auto *view = window.findChild<FileCompareView *>();
			if (view == nullptr)
				return 2;
			const QString key = QStringLiteral("FileChangedRescan");
			asked.clear();
			dontAskAgain = true;
			write(b, "one\n");
			window.applicationActivated();
			settle();
			dontAskAgain = false;
			const bool kept = lm::rememberedAnswer(key) == lm::AnswerYes;
			write(b, "one\ntwo\n");
			window.applicationActivated();
			settle();
			check(kept && asked.size() == 1 && view->realLinesForTest(1).size() == 2,
				"don't ask again: Yes reloads silently from then on");

			// Options > Message Boxes lists the question with its answer,
			// which the drop-down turns into No
			OptionsDialog dialog;
			const auto rows = dialog.messageBoxesForTest();
			bool fine = rows.size() == 3 && rows.at(2).second
				&& dialog.messageBoxAnswerForTest(2) == lm::answerText(lm::AnswerYes)
				&& dialog.messageBoxAnswerForTest(0).isEmpty();
			dialog.setMessageBoxAnswerForTest(2, 1);
			dialog.setMessageBoxHiddenForTest(0, true);
			fine = fine && dialog.messageBoxAnswerForTest(0) == lm::answerText(lm::AnswerOk);
			dialog.setMessageBoxHiddenForTest(0, false);
			dialog.saveForTest();
			fine = fine && lm::rememberedAnswer(key) == lm::AnswerNo;
			write(b, "one\ntwo\nthree\nfour\n");
			window.applicationActivated();
			settle();
			check(fine && asked.size() == 1 && view->realLinesForTest(1).size() == 2,
				"Message Boxes: the kept answer, switched to No");
			dialog.resetMessageBoxesForTest();
			window.applicationActivated();
			settle();
			check(lm::rememberedAnswer(key) == lm::NoAnswer && asked.size() == 2
				&& dialog.messageBoxAnswerForTest(2).isEmpty()
				&& view->realLinesForTest(1).size() == 4, "Reset asks again");
		}

		// File > Reload (OnFileReload)
		write(a, "l1\nl2\nl3\nl4\nl5\nl6\n");
		write(b, "l1\nl2\nl3\nl4\nl5\nL6\n");
		{
			MainWindow window;
			window.openFileComparison({ a, b }, { true, false });
			settle();
			auto *view = window.findChild<FileCompareView *>();
			if (view == nullptr)
				return 2;
			view->typeAtForTest(1, 0, QStringLiteral("typed "));
			view->setCursorViewLineForTest(1, 4);
			write(b, "l1\nl2\nl3\nl4\nl5\nL6\nl7\n");
			asked.clear();
			savePrompts = 0;
			saveChoice = 2;
			window.reloadCurrentComparison();
			settle();
			check(asked.isEmpty() && savePrompts == 1 && !view->isModified()
				&& view->realLinesForTest(1).size() == 7
				&& view->realLinesForTest(1).value(0) == QStringLiteral("l1")
				&& view->cursorViewLineForTest(1) == 4 && view->isSideReadOnly(0)
				&& !view->isSideReadOnly(1),
				"Reload: save prompt, fresh content, same line, read-only kept");
			view->undoActive();
			check(view->realLinesForTest(1).size() == 7 && !view->isModified(),
				"nothing to undo after a reload");

			// a file that cannot be read: nothing is touched
			QFile::remove(b);
			QString error;
			const bool reloaded = view->reload(&error);
			check(!reloaded && error.contains(b) && view->realLinesForTest(1).size() == 7,
				"a missing file refuses the reload and keeps the panes");
			write(b, "one\ntwo\nthree\n");
		}
		{
			MainWindow window;
			window.openBlankComparison();
			settle();
			auto *view = window.findChild<FileCompareView *>();
			if (view == nullptr)
				return 2;
			view->typeAtForTest(0, 0, QStringLiteral("scratch"));
			saveChoice = 2;
			window.reloadCurrentComparison();
			settle();
			check(!view->isModified()
				&& view->realLinesForTest(0) == QStringList{ QString() },
				"Reload empties an untitled pane");
		}

		// like opening, a reload reports identical files; Recompare's check
		// leaves the report to the Recompare
		write(a, "same\n");
		write(b, "other\n");
		{
			const QString binaryMatch = QCoreApplication::translate("MessageBoxes",
				"Selected files are identical (binary match).");
			MainWindow window;
			window.openFileComparison({ a, b });
			settle();
			auto *view = window.findChild<FileCompareView *>();
			if (view == nullptr)
				return 2;
			shown.clear();
			asked.clear();
			write(b, "same\n");
			window.applicationActivated();
			settle();
			settle();
			check(asked.size() == 1 && shown == QStringList{ binaryMatch },
				"a reload that leaves the files identical says so");
			write(b, "other again\n");
			window.applicationActivated();
			settle();
			shown.clear();
			write(b, "same\n");
			view->refreshByUser();
			settle();
			settle();
			check(shown == QStringList{ binaryMatch } && view->diffCount() == 0,
				"Recompare: reloaded on Yes, one report");
		}

		// tables: the same checks, and the save prompt on closing
		const QString t1 = dir.filePath(QStringLiteral("t1.csv"));
		const QString t2 = dir.filePath(QStringLiteral("t2.csv"));
		write(t1, "k,v\n1,a\n2,b\n");
		write(t2, "k,v\n1,a\n2,b\n");
		{
			MainWindow window;
			window.openFileComparison({ t1, t2 });
			settle();
			auto *table = window.findChild<TableCompareView *>();
			auto *tabs = window.findChild<QTabWidget *>();
			if (table == nullptr)
				return 2;
			asked.clear();
			write(t2, "k,v\n1,a\n2,c\n3,d\n");
			window.applicationActivated();
			settle();
			check(asked == QStringList{ reloadQuestion(t2) } && table->diffCount() > 0,
				"table: asked and reloaded");
			asked.clear();
			answerYes = false;
			write(t2, "k,v\n1,a\n");
			table->refreshByUser();
			settle();
			const bool recompareAsks = asked == QStringList{ reloadQuestion(t2) };
			// the right side now holds what the file no longer has
			table->gotoFirstDiff();
			table->copyCurrentDiff(0);
			asked.clear();
			QString error;
			table->saveModified(&error);
			check(recompareAsks && asked == QStringList{ overwriteQuestion(t2) }
				&& read(t2) == "k,v\n1,a\n" && table->isModified(),
				"table: Recompare checks, saving over a changed file asks");
			savePrompts = 0;
			saveChoice = 0;
			emit tabs->tabCloseRequested(0);
			settle();
			const bool keptOpen = tabs->count() == 1 && savePrompts == 1;
			saveChoice = 2;
			window.reloadCurrentComparison();
			settle();
			check(keptOpen && savePrompts == 2 && !table->isModified()
				&& table->paths() == QStringList({ t1, t2 }),
				"table: closing and reloading offer to save first");
			answerYes = true;
		}

		// images: checked on activation, not by Recompare (OnRefresh)
		const QString p1 = dir.filePath(QStringLiteral("l.png"));
		const QString p2 = dir.filePath(QStringLiteral("r.png"));
		const QString p3 = dir.filePath(QStringLiteral("m.png"));
		QImage picture(8, 8, QImage::Format_RGB32);
		picture.fill(Qt::blue);
		picture.save(p1);
		picture.save(p2);
		picture.save(p3);
		const auto repaint = [&](const QString &path, Qt::GlobalColor color)
		{
			const QDateTime before = QFileInfo(path).lastModified();
			picture.fill(color);
			picture.save(path);
			QFile f(path);
			f.open(QIODevice::Append);
			f.setFileTime(before.addSecs(3), QFileDevice::FileModificationTime);
		};
		{
			MainWindow window;
			window.openFileComparison({ p1, p2 });
			settle();
			auto *image = window.findChild<ImageCompareView *>();
			if (image == nullptr)
				return 2;
			const bool identical = image->diffCount() == 0;
			repaint(p2, Qt::red);
			asked.clear();
			image->refreshByUser();
			settle();
			const bool refreshSilent = asked.isEmpty() && image->diffCount() == 0;
			window.applicationActivated();
			settle();
			check(identical && refreshSilent && asked == QStringList{ reloadQuestion(p2) }
				&& image->diffCount() > 0, "image: Recompare keeps the loaded images, activation reloads");
			image->copyAllToRight();
			savePrompts = 0;
			saveChoice = 2;
			window.reloadCurrentComparison();
			settle();
			check(savePrompts == 1 && !image->isModified() && image->diffCount() > 0,
				"image: Reload offers to save, then reads the files again");
		}
		{
			// Recompare used to reopen the first two files only
			MainWindow window;
			window.openFileComparison({ p1, p3, p2 });
			settle();
			auto *image = window.findChild<ImageCompareView *>();
			if (image == nullptr)
				return 2;
			image->refreshByUser();
			window.reloadCurrentComparison();
			settle();
			check(image->paneCount() == 3 && image->paths().size() == 3
				&& image->diffCount() > 0, "image: three panes stay three");
		}

		MainWindow::setSavePromptForTest({});
		lm::setQuestionSinkForTest([](const QString &, bool *) { return false; });
		lm::setMessageSinkForTest([](const QString &) {});
		printf("reload: %s\n", ok ? "ok" : "FAILED");
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestIdenticalOpt))
	{
		// WinMerge's ShowIdenticalMessage, DoSelfCompare and Message
		// Boxes page; the messages are collected instead of shown
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		QStringList shown;
		lm::setMessageSinkForTest([&shown](const QString &text) { shown.append(text); });
		const auto write = [](const QString &path, const QByteArray &bytes)
		{
			QFile f(path);
			f.open(QIODevice::WriteOnly);
			f.write(bytes);
		};
		const auto message = [](const char *text) {
			return QCoreApplication::translate("MessageBoxes", text);
		};
		const QString binaryMatch = message("Selected files are identical (binary match).");
		const QString binaryDiffer = message("Selected files are identical (with current settings).\n"
			"But differ at the binary level.");
		const QString sameFile = message("Same file is opened in both panes.");
		const QString a = dir.filePath(QStringLiteral("a.txt"));
		const QString b = dir.filePath(QStringLiteral("b.txt"));
		const QString spaced = dir.filePath(QStringLiteral("spaced.txt"));
		const QString other = dir.filePath(QStringLiteral("other.txt"));
		write(a, "one two\n");
		write(b, "one two\n");
		write(spaced, "one   two\n");
		write(other, "something else\n");
		lm::setCompareOptionsForTest(0);
		bool ok = true;
		const auto check = [&ok](bool condition, const char *what)
		{
			printf("%s: %s\n", what, condition ? "ok" : "FAILED");
			ok = ok && condition;
		};
		const auto settle = []()
		{
			for (int i = 0; i < 3; ++i)
				QCoreApplication::processEvents();
		};
		const auto opened = [&](const QStringList &paths) {
			shown.clear();
			MainWindow window;
			window.openFileComparison(paths);
			settle();
			return shown;
		};

		check(opened({ a, b }) == QStringList{ binaryMatch },
			"identical files: binary match on opening");
		check(opened({ a, other }).isEmpty(), "different files: no message");
		check(opened({ a, a }) == QStringList{ sameFile },
			"same file in both panes");
		lm::setCompareOptionsForTest(2); // ignore all whitespace
		check(opened({ a, spaced }) == QStringList{ binaryDiffer },
			"identical with the options, not in bytes");
		lm::setCompareOptionsForTest(0);
		{
			shown.clear();
			MainWindow window;
			window.openBlankComparison();
			settle();
			check(shown.isEmpty(), "File > New: no message");
		}
		{
			MainWindow window;
			window.openFileComparison({ a, b });
			settle();
			shown.clear();
			window.findChild<FileCompareView *>()->refreshByUser();
			settle();
			check(shown == QStringList{ binaryMatch }, "Recompare reports again");
		}
		{
			// saving that leaves nothing different reports it too
			write(other, "one two!\n");
			MainWindow window;
			window.openFileComparison({ a, other });
			settle();
			auto *view = window.findChild<FileCompareView *>();
			view->copyCurrentDiff(0, 1, false);
			shown.clear();
			QString error;
			view->saveModified(&error);
			settle();
			check(shown == QStringList{ binaryMatch }, "saving to identical reports it");
			write(other, "something else\n");
		}
		{
			const QString leftPng = dir.filePath(QStringLiteral("l.png"));
			const QString rightPng = dir.filePath(QStringLiteral("r.png"));
			QImage image(4, 4, QImage::Format_RGB32);
			image.fill(Qt::blue);
			image.save(leftPng);
			image.save(rightPng);
			check(opened({ leftPng, rightPng }) == QStringList{ binaryMatch },
				"identical images: binary match");
		}
		lm::setMessageHidden(QStringLiteral("FilesSame"), true);
		check(opened({ a, b }).isEmpty(), "hidden: no message");
		lm::resetHiddenMessages();

		// self-compare: a snapshot on the left, read-only, "Original File"
		{
			shown.clear();
			MainWindow window;
			window.openSelfComparison(a);
			settle();
			auto *view = window.findChild<FileCompareView *>();
			const QStringList paths = view != nullptr ? view->paths() : QStringList{};
			const QStringList recent = QSettings()
				.value(QStringLiteral("RecentComparisons/List")).toStringList();
			check(view != nullptr && paths.size() == 2 && paths.at(1) == a
				&& paths.at(0) != a && QFile::exists(paths.at(0))
				&& view->isSideReadOnly(0) && !view->isSideReadOnly(1)
				&& view->sideCaption(0) == MainWindow::tr("Original File")
				&& view->tabTitle().startsWith(MainWindow::tr("Original File"))
				&& recent.value(0) == a && shown == QStringList{ binaryMatch },
				"self-compare of a text file");
		}
		{
			const QString csv = dir.filePath(QStringLiteral("t.csv"));
			write(csv, "k,v\n1,a\n");
			MainWindow window;
			window.openSelfComparison(csv);
			settle();
			auto *table = window.findChild<TableCompareView *>();
			check(table != nullptr && table->tabTitle().startsWith(
				MainWindow::tr("Original File")), "self-compare of a table");
		}
		{
			// Compare with the first path alone; the selection stays
			MainWindow window;
			window.openSelector({ b });
			auto *selector = window.findChild<NewComparisonView *>();
			auto *pathEdit = selector != nullptr ? selector->findChild<QLineEdit *>() : nullptr;
			if (pathEdit == nullptr)
				return 2;
			QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
			QCoreApplication::sendEvent(pathEdit, &press);
			settle();
			auto *view = window.findChild<FileCompareView *>();
			check(view != nullptr && view->paths().value(1) == b
				&& window.findChild<NewComparisonView *>() != nullptr,
				"Compare with one file self-compares");
		}

		// Options > Message Boxes
		{
			OptionsDialog dialog;
			const auto rows = dialog.messageBoxesForTest();
			dialog.setMessageBoxHiddenForTest(0, true);
			dialog.saveForTest();
			const bool saved = lm::messageHidden(QStringLiteral("FilesSame"));
			shown.clear();
			dialog.resetMessageBoxesForTest();
			check(rows.size() == 3 && !rows.at(0).second && saved
				&& !lm::messageHidden(QStringLiteral("FilesSame"))
				&& !dialog.messageBoxesForTest().at(0).second,
				"Message Boxes page: hide, save and reset");
		}
		lm::setMessageSinkForTest([](const QString &) {});
		printf("identical: %s\n", ok ? "ok" : "FAILED");
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestGeneralOptionsOpt))
	{
		// WinMerge's General page options, on this run's own settings
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const auto write = [](const QString &path, const QByteArray &bytes)
		{
			QFile f(path);
			f.open(QIODevice::WriteOnly);
			f.write(bytes);
		};
		const QString left = dir.filePath(QStringLiteral("left.txt"));
		const QString right = dir.filePath(QStringLiteral("right.txt"));
		write(left, "a\n");
		write(right, "b\n");
		const QString leftCsv = dir.filePath(QStringLiteral("left.csv"));
		const QString rightCsv = dir.filePath(QStringLiteral("right.csv"));
		write(leftCsv, "k,v\n1,a\n");
		write(rightCsv, "k,v\n1,b\n");
		const QString leftPng = dir.filePath(QStringLiteral("left.png"));
		const QString rightPng = dir.filePath(QStringLiteral("right.png"));
		QImage image(8, 8, QImage::Format_RGB32);
		image.fill(Qt::white);
		image.save(leftPng);
		image.fill(Qt::red);
		image.save(rightPng);
		const QString leftDir = dir.filePath(QStringLiteral("L"));
		const QString rightDir = dir.filePath(QStringLiteral("R"));
		QDir().mkpath(leftDir);
		QDir().mkpath(rightDir);
		bool ok = true;
		const auto check = [&ok](bool condition, const char *what)
		{
			printf("%s: %s\n", what, condition ? "ok" : "FAILED");
			ok = ok && condition;
		};
		const auto pressEsc = [](QWidget *target)
		{
			QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
			QCoreApplication::sendEvent(target, &press);
			QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
			QCoreApplication::sendEvent(target, &release);
			QCoreApplication::processEvents();
			QCoreApplication::processEvents();
		};
		const auto setEsc = [](int mode)
		{
			QSettings().setValue(QStringLiteral("General/CloseWithEsc"), mode);
		};
		const auto tabCount = [](MainWindow &window)
		{
			return window.findChild<QTabWidget *>()->count();
		};

		// Esc climbs from each kind of page to the window and closes it
		setEsc(OptionsDialog::EscTabOrMainWindow);
		const auto closesPage = [&](const char *what, auto open, auto focusOf)
		{
			MainWindow window;
			window.show();
			open(window);
			QCoreApplication::processEvents();
			const int before = tabCount(window);
			QWidget *target = focusOf(window);
			if (target == nullptr)
			{
				check(false, what);
				return;
			}
			pressEsc(target);
			check(tabCount(window) == before - 1, what);
		};
		closesPage("Esc closes a file comparison",
			[&](MainWindow &w) { w.openFileComparison({ left, right }); },
			[](MainWindow &w) -> QWidget * { return w.findChild<DiffTextEdit *>(); });
		closesPage("Esc closes a table comparison",
			[&](MainWindow &w) { w.openFileComparison({ leftCsv, rightCsv }); },
			[](MainWindow &w) -> QWidget * {
				auto *table = w.findChild<TableCompareView *>();
				return table != nullptr ? table->findChild<QTableView *>() : nullptr;
			});
		closesPage("Esc closes an image comparison",
			[&](MainWindow &w) { w.openFileComparison({ leftPng, rightPng }); },
			[](MainWindow &w) -> QWidget * { return w.findChild<ImagePane *>(); });
		closesPage("Esc closes a finished folder comparison",
			[&](MainWindow &w) {
				w.openFolderComparison({ leftDir, rightDir });
				auto *folder = w.findChild<FolderCompareView *>();
				for (int i = 0; i < 400 && folder != nullptr
					&& folder->isComparingForTest(); ++i)
				{
					QThread::msleep(25);
					QCoreApplication::processEvents();
				}
			},
			[](MainWindow &w) -> QWidget * {
				auto *folder = w.findChild<FolderCompareView *>();
				return folder != nullptr ? folder->findChild<QTreeWidget *>() : nullptr;
			});

		// the find bar keeps its own Esc: it closes, the tab stays
		{
			MainWindow window;
			window.show();
			window.openFileComparison({ left, right });
			auto *view = window.findChild<FileCompareView *>();
			view->showFindBar();
			QCoreApplication::processEvents();
			QLineEdit *findEdit = nullptr;
			for (QLineEdit *edit : view->findChildren<QLineEdit *>())
				if (edit->isVisible())
					findEdit = edit;
			const int before = tabCount(window);
			pressEsc(findEdit);
			check(findEdit != nullptr && !findEdit->isVisible()
				&& tabCount(window) == before, "Esc closes the find bar only");
		}

		// a running folder comparison: Esc stops it and the tab stays
		{
			MainWindow window;
			window.show();
			window.openFolderComparison({ leftDir, rightDir });
			auto *folder = window.findChild<FolderCompareView *>();
			const int before = tabCount(window);
			const bool running = folder != nullptr && folder->isComparingForTest();
			pressEsc(folder != nullptr ? folder->findChild<QTreeWidget *>() : nullptr);
			check(running && tabCount(window) == before,
				"Esc stops a running folder comparison first");
		}

		// the four modes, on file comparisons
		{
			setEsc(OptionsDialog::EscDisabled);
			MainWindow window;
			window.show();
			window.openFileComparison({ left, right });
			const int before = tabCount(window);
			pressEsc(window.findChild<DiffTextEdit *>());
			check(tabCount(window) == before, "Disabled keeps the tab");
		}
		{
			setEsc(OptionsDialog::EscTabOrMainWindow);
			MainWindow window;
			window.show();
			window.openFileComparison({ left, right });
			window.openFileComparison({ right, left });
			while (tabCount(window) > 0)
				pressEsc(window.findChild<QTabWidget *>()->currentWidget()
					->findChild<QWidget *>());
			check(window.isVisible(), "Tab or main window: tabs go first");
			pressEsc(&window);
			check(!window.isVisible(), "Tab or main window: no tab, the window");
		}
		{
			setEsc(OptionsDialog::EscTabOnly);
			MainWindow window;
			window.show();
			window.openFileComparison({ left, right });
			while (tabCount(window) > 0)
				pressEsc(window.findChild<QTabWidget *>()->currentWidget()
					->findChild<QWidget *>());
			pressEsc(&window);
			check(window.isVisible(), "Tab only never closes the window");
		}
		{
			setEsc(OptionsDialog::EscMainWindowIfOneTab);
			MainWindow window;
			window.show();
			window.openFileComparison({ left, right });
			window.openFileComparison({ right, left });
			while (tabCount(window) > 1)
				pressEsc(window.findChild<QTabWidget *>()->currentWidget()
					->findChild<DiffTextEdit *>());
			check(window.isVisible(), "One tab left: still open");
			pressEsc(window.findChild<DiffTextEdit *>());
			check(!window.isVisible(), "Main window if only one tab");
		}
		{
			// the selection screen: Esc is its Cancel button, any mode
			setEsc(OptionsDialog::EscDisabled);
			MainWindow window;
			window.show();
			window.openFileComparison({ left, right });
			window.openSelector({ left, right });
			auto *selector = window.findChild<NewComparisonView *>();
			pressEsc(selector != nullptr ? selector->findChild<QLineEdit *>() : nullptr);
			check(window.findChild<NewComparisonView *>() == nullptr,
				"Esc cancels the selection screen");
		}
		{
			// the last tab too, and before the window weighs Esc: in
			// "main window if only one tab" it closes the screen only
			setEsc(OptionsDialog::EscMainWindowIfOneTab);
			MainWindow window;
			window.show();
			window.openSelector({});
			auto *selector = window.findChild<NewComparisonView *>();
			pressEsc(selector != nullptr ? selector->findChild<QLineEdit *>() : nullptr);
			check(window.findChild<NewComparisonView *>() == nullptr
				&& tabCount(window) == 0 && window.isVisible(),
				"Esc on a lone selection screen closes it, not the window");
		}
		{
			// Cancel closes a lone selection screen (COpenView::OnCancel)
			MainWindow window;
			window.show();
			window.openSelector({});
			QPushButton *cancel = nullptr;
			for (QPushButton *button : window.findChild<NewComparisonView *>()
					->findChildren<QPushButton *>())
				if (button->text() == NewComparisonView::tr("Cancel"))
					cancel = button;
			if (cancel == nullptr)
				return 2;
			cancel->click();
			QCoreApplication::processEvents();
			QCoreApplication::processEvents();
			check(window.findChild<NewComparisonView *>() == nullptr
				&& window.isVisible(), "Cancel closes a lone selection screen");
		}

		// Preserve file time: the saved file keeps its date
		const QDateTime older(QDate(2020, 1, 1), QTime(10, 0));
		for (const bool preserve : { true, false })
		{
			QSettings().setValue(QStringLiteral("General/PreserveFileTime"), preserve);
			write(left, "a\n");
			{
				QFile f(left);
				f.open(QIODevice::Append);
				f.setFileTime(older, QFileDevice::FileModificationTime);
			}
			FileCompareView view;
			QString error;
			view.compare(left, right, &error);
			view.typeAtForTest(0, 0, QStringLiteral("x"));
			view.saveModified(&error);
			const qint64 drift = qAbs(QFileInfo(left).lastModified().secsTo(older));
			check(preserve ? drift < 2 : drift > 86400,
				preserve ? "Preserve file time keeps the date"
				         : "without it the date moves");
		}

		// Close the selection on Compare: off (WinMerge's default) keeps it
		for (const bool closeOnCompare : { false, true })
		{
			QSettings().setValue(QStringLiteral("General/CloseSelectorOnCompare"),
				closeOnCompare);
			MainWindow window;
			window.openSelector({ left, right });
			auto *selector = window.findChild<NewComparisonView *>();
			auto *pathEdit = selector != nullptr
				? selector->findChild<QLineEdit *>() : nullptr;
			if (pathEdit == nullptr)
				return 2;
			QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
			QCoreApplication::sendEvent(pathEdit, &press);
			QCoreApplication::processEvents();
			QCoreApplication::processEvents();
			const bool open = window.findChild<NewComparisonView *>() != nullptr;
			check(window.findChildren<FileCompareView *>().size() == 1
				&& open != closeOnCompare,
				closeOnCompare ? "Compare closes the selection"
				               : "Compare keeps the selection open");
		}

		// auto completion of the path fields
		for (int source = 0; source < 3; ++source)
		{
			QSettings().setValue(QStringLiteral("General/AutoCompleteSource"), source);
			NewComparisonView selector;
			auto *combo = selector.findChild<QComboBox *>();
			QCompleter *completer = combo != nullptr ? combo->completer() : nullptr;
			const bool right = source == 0 ? completer == nullptr
				: source == 1 ? completer != nullptr
					&& qobject_cast<QFileSystemModel *>(completer->model()) != nullptr
				: completer != nullptr && completer->model() == combo->model();
			check(right, source == 0 ? "no auto completion"
				: source == 1 ? "completion from the file system"
				              : "completion from the recent list");
		}

		// clearing the recent items empties the recent-list suggestions,
		// in a selection screen that is already open too
		{
			QSettings().setValue(QStringLiteral("General/AutoCompleteSource"), 2);
			QSettings().setValue(QStringLiteral("NewComparison/History"),
				QStringList{ left, right });
			NewComparisonView selector;
			auto *combo = selector.findChild<QComboBox *>();
			const bool had = combo != nullptr && combo->count() == 2
				&& combo->completer() != nullptr
				&& combo->completer()->model()->rowCount() == 2;
			NewComparisonView::clearSavedHistory();
			selector.reloadHistory();
			check(had && combo->count() == 0
				&& combo->completer()->model()->rowCount() == 0
				&& NewComparisonView::savedHistory().isEmpty(),
				"clearing the recent items clears the suggestions");
		}

		// Verify paths (on by default): the status names what is wrong and
		// Compare waits for a path that exists
		{
			const QString missing = dir.filePath(QStringLiteral("nothing-here"));
			const QString zip = dir.filePath(QStringLiteral("pack.zip"));
			write(zip, "PK");
			NewComparisonView selector;
			const auto combos = selector.findChildren<QComboBox *>();
			const auto fill = [&](const QString &a, const QString &b, const QString &c)
			{
				combos.at(0)->setEditText(a);
				combos.at(1)->setEditText(b);
				combos.at(2)->setEditText(c);
				selector.verifyPathsForTest();
			};
			const auto expect = [&](const char *what, const char *hint, bool enabled)
			{
				check(selector.hintForTest() == NewComparisonView::tr(hint)
					&& selector.compareEnabledForTest() == enabled, what);
			};
			fill({}, {}, {});
			expect("empty fields: the prompt, Compare off",
				"Select two (or three) folders/files to compare.", false);
			fill(missing, missing, {});
			expect("both invalid", "Both paths are invalid!", false);
			fill(leftDir, missing, {});
			expect("right invalid", "Right (2nd) path is invalid!", true);
			fill(left, {}, {});
			expect("a lone file is a start",
				"Select two (or three) folders/files to compare.", true);
			fill(left, leftDir, {});
			expect("file and folder", "Cannot compare file and folder!", true);
			fill(leftDir, zip, {});
			expect("folder and archive",
				"Select two (or three) folders/files to compare.", true);
			fill(left, missing, right);
			expect("middle invalid", "Middle (2nd) path is invalid!", true);
			fill(missing, left, missing);
			expect("left and right invalid",
				"Left (1st) and Right (3rd) paths are invalid!", true);
			fill(left, right, leftDir);
			expect("3-way file and folder", "Cannot compare file and folder!", true);

			QSettings().setValue(QStringLiteral("General/VerifyOpenPaths"), false);
			fill(missing, missing, {});
			check(selector.compareEnabledForTest(), "unchecked: Compare always on");
			QSettings().setValue(QStringLiteral("General/VerifyOpenPaths"), true);

			// Enter with Compare off does nothing
			MainWindow window;
			window.openSelector({ missing, missing });
			auto *screen = window.findChild<NewComparisonView *>();
			auto *pathEdit = screen != nullptr ? screen->findChild<QLineEdit *>() : nullptr;
			if (pathEdit == nullptr)
				return 2;
			QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
			QCoreApplication::sendEvent(pathEdit, &press);
			QCoreApplication::processEvents();
			check(window.findChildren<FileCompareView *>().isEmpty()
				&& window.findChildren<FolderCompareView *>().isEmpty(),
				"Enter with Compare off opens nothing");
		}

		// Shift+Delete on an open dropdown forgets the highlighted path,
		// in every field and in the saved history; a typed path stays
		{
			QSettings().setValue(QStringLiteral("NewComparison/History"),
				QStringList{ left, right });
			NewComparisonView selector;
			const auto combos = selector.findChildren<QComboBox *>();
			QComboBox *first = combos.value(0);
			QComboBox *second = combos.value(1);
			if (first == nullptr || second == nullptr)
				return 2;
			second->setEditText(QStringLiteral("typed/path"));
			first->view()->setCurrentIndex(first->model()->index(0, 0));
			QKeyEvent press(QEvent::KeyPress, Qt::Key_Delete, Qt::ShiftModifier);
			QCoreApplication::sendEvent(first->view(), &press);
			check(first->count() == 1 && first->itemText(0) == right
				&& second->count() == 1
				&& second->currentText() == QStringLiteral("typed/path")
				&& NewComparisonView::savedHistory() == QStringList{ right },
				"Shift+Delete forgets one recent path");
		}

		printf("general options: %s\n", ok ? "ok" : "FAILED");
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestAboutOpt))
	{
		const QString version = AboutDialog::versionText();
		const QString contributors = AboutDialog::contributorsMarkdown();
		// the Crystal Edit parser authors ask for an About-box credit
		const QStringList mustCredit = { QStringLiteral("Stcherbatchenko"),
			QStringLiteral("Ferdinand Prantl"), QStringLiteral("YuanShoyan"),
			QStringLiteral("wiera987"), QStringLiteral("devmynote"),
			QStringLiteral("Javier Miguel"), QStringLiteral("H. Saido"),
			QStringLiteral("voidray"), QStringLiteral("Przemys") };
		bool ok = version.contains(QApplication::applicationVersion())
			&& !AboutDialog::platformText().isEmpty()
			&& AboutDialog::homepageUrl().host() == QStringLiteral("github.com")
			&& !contributors.isEmpty();
		for (const QString &name : mustCredit)
			if (!contributors.contains(name))
			{
				fprintf(stderr, "missing credit: %s\n", qPrintable(name));
				ok = false;
			}
		printf("%s | %s | contributors: %lld chars, ok: %d\n",
			qPrintable(version), qPrintable(AboutDialog::platformText()),
			static_cast<long long>(contributors.size()), ok);
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestAppMenuOpt))
	{
		// Qt moves menu actions into the macOS application menu by role;
		// a text heuristic once promoted the pt-BR "Sobreposicao" submenu
		// to "About", hiding the real About. Only the native menu tells.
#ifdef Q_OS_MACOS
		if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
		{
			printf("skipped: needs the cocoa platform\n");
			return 0;
		}
		MainWindow window;
		window.show();
		window.raise();
		window.activateWindow();
		lm::activateAppForTest();
		// macOS installs a window's menu bar only once it is active: wait
		// for the app's own menus to appear next to the application menu
		// (a terminal keeping the focus made this check flaky)
		for (int i = 0; i < 120; ++i)
		{
			QThread::msleep(25);
			QCoreApplication::processEvents();
			if (lm::appMenuItemsForTest(QStringLiteral("*")).size() > 3)
				break;
		}
		const QStringList items = lm::appMenuItemsForTest();
		bool ok = !items.isEmpty()
			&& items.first().endsWith(QLatin1String("\t0"));
		for (const QString &item : items)
		{
			printf("%s\n", qPrintable(item));
			const QString title = item.section(QLatin1Char('\t'), 0, 0);
			const bool submenu = item.endsWith(QLatin1String("\t1"));
			// the Services entry is the only application-menu item that
			// legitimately opens a submenu
			if (submenu && !title.startsWith(QStringLiteral("Servi")))
				ok = false;
		}
		// the overlay submenu belongs to the Image menu; macOS installs the
		// window's menus only while the app is frontmost, which a test run
		// from a terminal cannot force (cooperative activation), so this
		// half is checked only when the menus are there
		const bool installed =
			lm::appMenuItemsForTest(QStringLiteral("*")).size() > 3;
		if (installed)
		{
			QStringList imageItems = lm::appMenuItemsForTest(QStringLiteral("Image"));
			if (imageItems.isEmpty())
				imageItems = lm::appMenuItemsForTest(QStringLiteral("Imagem"));
			bool overlayHome = false;
			for (const QString &item : imageItems)
				if ((item.startsWith(QStringLiteral("Overlay"))
						|| item.startsWith(QStringLiteral("Sobreposi")))
					&& item.endsWith(QLatin1String("\t1")))
					overlayHome = true;
			printf("overlay submenu in the Image menu: %d\n", overlayHome);
			ok = ok && overlayHome;
		}
		else
			printf("window menus not installed (app not frontmost): Image menu check skipped\n");
		printf("ok: %d\n", ok);
		return ok ? 0 : 1;
#else
		printf("skipped: macOS only\n");
		return 0;
#endif
	}

	if (parser.isSet(selftestFolder3OpsOpt))
	{
		// WinMerge's 3-way DirView operations: pairwise copies leave the
		// row "not compared", deletes recompute from existence and drop
		// rows that ran out of sides
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
			put(i, QStringLiteral("a.txt"), QByteArray::number(i) + "\n");
			put(i, QStringLiteral("c.txt"), "c\n");
			put(i, QStringLiteral("e.txt"), "e\n");
		}
		put(0, QStringLiteral("b.txt"), "b\n");
		put(1, QStringLiteral("d.txt"), "d\n");

		FolderCompareView view;
		const auto wait = [&view]() {
			for (int i = 0; i < 400 && view.isComparingForTest(); ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
		};
		view.start(QStringList{ roots[0], roots[1], roots[2] });
		wait();

		using Item = lm::FolderCompareItem;
		bool ok = true;
		const auto expect = [&view, &ok](const QString &name, int category,
			const char *when) {
			const int got = view.rowCategoryForTest(name);
			if (got != category)
			{
				fprintf(stderr, "%s: %s category %d, expected %d\n",
					when, qPrintable(name), got, category);
				ok = false;
			}
		};

		view.copyRowForTest(QStringLiteral("a.txt"), 0, 1);
		expect(QStringLiteral("a.txt"), Item::NotCompared, "after copy");
		{
			QFile fa(roots[1] + QStringLiteral("/a.txt"));
			fa.open(QIODevice::ReadOnly);
			if (fa.readAll() != "0\n")
			{
				fprintf(stderr, "copy did not reach the middle\n");
				ok = false;
			}
		}
		view.copyRowForTest(QStringLiteral("b.txt"), 0, 2);
		expect(QStringLiteral("b.txt"), Item::MissingMiddle, "after copy");
		view.deleteRowForTest(QStringLiteral("c.txt"), { 2 });
		expect(QStringLiteral("c.txt"), Item::MissingRight, "after delete");
		if (QFileInfo::exists(roots[2] + QStringLiteral("/c.txt")))
		{
			fprintf(stderr, "right c.txt still on disk\n");
			ok = false;
		}
		view.deleteRowForTest(QStringLiteral("d.txt"), { 1 });
		expect(QStringLiteral("d.txt"), -1, "after lone-side delete");
		view.deleteRowForTest(QStringLiteral("e.txt"), { 0, 1, 2 });
		expect(QStringLiteral("e.txt"), -1, "after delete all");

		// a recompare resolves every unknown against the disk
		view.recompare();
		wait();
		expect(QStringLiteral("a.txt"), Item::Different, "after recompare");
		expect(QStringLiteral("b.txt"), Item::MissingMiddle, "after recompare");
		expect(QStringLiteral("c.txt"), Item::MissingRight, "after recompare");
		expect(QStringLiteral("d.txt"), -1, "after recompare");
		expect(QStringLiteral("e.txt"), -1, "after recompare");
		printf("ok: %d\n", ok);
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestFolderSyncOpt))
	{
		// WinMerge's UpdateChangedItem: saving a comparison opened from
		// the folder view refreshes that row without a rescan
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const QString leftDir = dir.filePath(QStringLiteral("L"));
		const QString rightDir = dir.filePath(QStringLiteral("R"));
		QDir().mkpath(leftDir);
		QDir().mkpath(rightDir);
		{
			QFile f(leftDir + QStringLiteral("/a.txt"));
			f.open(QIODevice::WriteOnly);
			f.write("x\n");
		}
		{
			QFile f(rightDir + QStringLiteral("/a.txt"));
			f.open(QIODevice::WriteOnly);
			f.write("y\n");
		}
		MainWindow window;
		window.openFolderComparison(QStringList{ leftDir, rightDir });
		auto *folder = window.findChild<FolderCompareView *>();
		if (folder == nullptr)
			return 2;
		for (int i = 0; i < 400 && folder->isComparingForTest(); ++i)
		{
			QThread::msleep(25);
			QCoreApplication::processEvents();
		}
		const int before = folder->rowCategoryForTest(QStringLiteral("a.txt"));
		folder->activateRowForTest(QStringLiteral("a.txt"));
		QCoreApplication::processEvents();
		FileCompareView *file = nullptr;
		for (FileCompareView *candidate : window.findChildren<FileCompareView *>())
			if (!candidate->paths().at(0).isEmpty())
				file = candidate;
		if (file == nullptr)
		{
			fprintf(stderr, "no file comparison opened\n");
			return 2;
		}
		file->copyCurrentDiff(0);
		QString error;
		if (!file->saveModified(&error))
		{
			fprintf(stderr, "save failed: %s\n", qPrintable(error));
			return 2;
		}
		const int after = folder->rowCategoryForTest(QStringLiteral("a.txt"));
		printf("category before: %d, after: %d\n", before, after);
		return (before == static_cast<int>(lm::FolderCompareItem::Different)
			&& after == static_cast<int>(lm::FolderCompareItem::Identical))
			? 0 : 1;
	}

	if (parser.isSet(selftestFolderContentOpt))
	{
		// Full Contents like WinMerge: text files diff as text, so the
		// ignore options apply; Quick Contents above 4 MB; the Result
		// column names the file type
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const QString leftDir = dir.filePath(QStringLiteral("L"));
		const QString rightDir = dir.filePath(QStringLiteral("R"));
		QDir().mkpath(leftDir);
		QDir().mkpath(rightDir);
		const auto write = [](const QString &path, const QByteArray &bytes)
		{
			QFile f(path);
			f.open(QIODevice::WriteOnly);
			f.write(bytes);
		};
		const QByteArray big = QByteArray("line of text\n").repeated(400000);
		write(leftDir + QStringLiteral("/same.txt"), "alpha\n");
		write(rightDir + QStringLiteral("/same.txt"), "alpha\n");
		write(leftDir + QStringLiteral("/spaces.txt"), "a b\n");
		write(rightDir + QStringLiteral("/spaces.txt"), "a   b\n");
		write(leftDir + QStringLiteral("/big.txt"), big);
		write(rightDir + QStringLiteral("/big.txt"), big);
		write(leftDir + QStringLiteral("/data.bin"), QByteArray("\x00\x01\x02", 3));
		write(rightDir + QStringLiteral("/data.bin"), QByteArray("\x00\x01\x03", 3));

		const auto results = [&](int ignoreWhitespace)
		{
			lm::setCompareOptionsForTest(ignoreWhitespace);
			FolderCompareView view;
			view.start(QStringList{ leftDir, rightDir });
			for (int i = 0; i < 400 && view.isComparingForTest(); ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
			QMap<QString, QString> texts;
			for (const QString &name : { QStringLiteral("same.txt"),
					QStringLiteral("spaces.txt"), QStringLiteral("big.txt"),
					QStringLiteral("data.bin") })
			{
				texts[name] = view.rowResultForTest(name);
				printf("ignore whitespace %d: %s -> %s\n", ignoreWhitespace,
					qPrintable(name), qPrintable(texts[name]));
			}
			return texts;
		};
		const QMap<QString, QString> exact = results(0);
		const QMap<QString, QString> ignoring = results(2); // ignore all
		const QString textSame = QObject::tr("Text files are identical");
		const QString textDiff = QObject::tr("Text files are different");
		const QString binDiff = QObject::tr("Binary files are different");
		return (exact[QStringLiteral("same.txt")] == textSame
			&& exact[QStringLiteral("spaces.txt")] == textDiff
			&& ignoring[QStringLiteral("spaces.txt")] == textSame
			&& exact[QStringLiteral("big.txt")] == textSame
			&& exact[QStringLiteral("data.bin")] == binDiff)
			? 0 : 1;
	}

	if (parser.isSet(selftestCompareMethodsOpt))
	{
		// WinMerge's seven folder compare methods against three pairs:
		// same bytes with another date, other bytes of the same size, and
		// another size (the last two share one date)
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const QString leftDir = dir.filePath(QStringLiteral("L"));
		const QString rightDir = dir.filePath(QStringLiteral("R"));
		QDir().mkpath(leftDir);
		QDir().mkpath(rightDir);
		const QDateTime older(QDate(2020, 1, 1), QTime(10, 0));
		const QDateTime newer(QDate(2021, 1, 1), QTime(10, 0));
		const auto write = [](const QString &path, const QByteArray &bytes,
			const QDateTime &mtime)
		{
			QFile f(path);
			f.open(QIODevice::WriteOnly);
			f.write(bytes);
			f.flush(); // or closing would write, and date, again
			f.setFileTime(mtime, QFileDevice::FileModificationTime);
		};
		write(leftDir + QStringLiteral("/touched.txt"), "same\n", older);
		write(rightDir + QStringLiteral("/touched.txt"), "same\n", newer);
		write(leftDir + QStringLiteral("/edited.txt"), "aaaa\n", older);
		write(rightDir + QStringLiteral("/edited.txt"), "bbbb\n", older);
		write(leftDir + QStringLiteral("/grown.txt"), "a\n", older);
		write(rightDir + QStringLiteral("/grown.txt"), "abc\n", older);

		// per method: touched, edited, grown; 0 identical, 1 different
		const int expected[lm::kCompareMethodCount][3] = {
			{ 0, 1, 1 }, // Full Contents
			{ 0, 1, 1 }, // Quick Contents
			{ 0, 1, 1 }, // Binary Contents
			{ 1, 0, 0 }, // Modified Date
			{ 1, 0, 1 }, // Modified Date and Size
			{ 0, 0, 1 }, // Size
			{ 0, 0, 0 }, // Existence
		};
		const QString names[3] = { QStringLiteral("touched.txt"),
			QStringLiteral("edited.txt"), QStringLiteral("grown.txt") };
		lm::setCompareOptionsForTest(0);
		bool ok = true;
		for (int method = 0; method < lm::kCompareMethodCount; ++method)
		{
			lm::setCompareMethodForTest(method);
			FolderCompareView view;
			view.start(QStringList{ leftDir, rightDir });
			for (int i = 0; i < 400 && view.isComparingForTest(); ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
			const bool shown =
				view.compareMethodTextForTest() == lm::compareMethodName(method);
			printf("%-24s", qPrintable(view.compareMethodTextForTest()));
			for (int f = 0; f < 3; ++f)
			{
				const int category = view.rowCategoryForTest(names[f]);
				const int want = expected[method][f] == 0
					? static_cast<int>(lm::FolderCompareItem::Identical)
					: static_cast<int>(lm::FolderCompareItem::Different);
				printf("  %s=%d%s", qPrintable(names[f]), category,
					category == want ? "" : " (wrong)");
				ok = ok && category == want;
			}
			printf("\n");
			ok = ok && shown;
			// no content compare ran: the Result column says "Files are ..."
			if (method == 3) // Modified Date
				ok = ok && view.rowResultForTest(names[0])
					== QObject::tr("Files are different");
		}
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestShowFiltersOpt))
	{
		// WinMerge's View menu filters: switching one off hides exactly
		// the rows it names, 2- and 3-way; nothing is saved
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const auto write = [](const QString &path, const QByteArray &bytes)
		{
			QDir().mkpath(QFileInfo(path).absolutePath());
			QFile f(path);
			f.open(QIODevice::WriteOnly);
			f.write(bytes);
		};
		const QString a = dir.filePath(QStringLiteral("A"));
		const QString b = dir.filePath(QStringLiteral("B"));
		const QString c = dir.filePath(QStringLiteral("C"));
		const QString l2 = dir.filePath(QStringLiteral("L2"));
		const QString r2 = dir.filePath(QStringLiteral("R2"));
		write(l2 + QStringLiteral("/same.txt"), "x\n");
		write(r2 + QStringLiteral("/same.txt"), "x\n");
		write(l2 + QStringLiteral("/diff.txt"), "x\n");
		write(r2 + QStringLiteral("/diff.txt"), "y\n");
		write(l2 + QStringLiteral("/left.txt"), "x\n");
		write(r2 + QStringLiteral("/right.txt"), "x\n");
		write(l2 + QStringLiteral("/data.bin"), QByteArray("\x00\x01", 2));
		write(r2 + QStringLiteral("/data.bin"), QByteArray("\x00\x02", 2));
		// 3-way: which side differs, all three differ, middle only,
		// missing on the left only
		write(a + QStringLiteral("/leftdiff.txt"), "x\n");
		write(b + QStringLiteral("/leftdiff.txt"), "a\n");
		write(c + QStringLiteral("/leftdiff.txt"), "a\n");
		write(a + QStringLiteral("/middiff.txt"), "a\n");
		write(b + QStringLiteral("/middiff.txt"), "x\n");
		write(c + QStringLiteral("/middiff.txt"), "a\n");
		write(a + QStringLiteral("/rightdiff.txt"), "a\n");
		write(b + QStringLiteral("/rightdiff.txt"), "a\n");
		write(c + QStringLiteral("/rightdiff.txt"), "x\n");
		write(a + QStringLiteral("/alldiff.txt"), "a\n");
		write(b + QStringLiteral("/alldiff.txt"), "b\n");
		write(c + QStringLiteral("/alldiff.txt"), "c\n");
		write(b + QStringLiteral("/middleonly.txt"), "x\n");
		write(b + QStringLiteral("/noleft.txt"), "x\n");
		write(c + QStringLiteral("/noleft.txt"), "x\n");

		lm::setCompareMethodForTest(0);
		lm::setCompareOptionsForTest(0);
		using F = FolderCompareView::ShowFilter;
		// one filter off at a time, from the defaults: the rows that
		// must disappear, every other row staying listed
		const auto check = [](FolderCompareView &view, const QStringList &all,
			F filter, const QStringList &gone)
		{
			for (int f = 0; f < FolderCompareView::ShowFilterCount; ++f)
				view.setShowFilterForTest(static_cast<F>(f),
					f != FolderCompareView::ShowSkipped);
			view.setShowFilterForTest(filter, false);
			bool ok = view.hiddenRowsForTest() == gone.size();
			for (const QString &name : all)
			{
				const bool shown = view.rowShownForTest(name);
				ok = ok && shown != gone.contains(name);
				if (shown == gone.contains(name))
					printf("filter %d: %s wrongly %s\n", int(filter),
						qPrintable(name), shown ? "shown" : "hidden");
			}
			return ok;
		};
		const auto run = [](const QStringList &dirs, FolderCompareView &view)
		{
			view.start(dirs);
			for (int i = 0; i < 400 && view.isComparingForTest(); ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
		};

		FolderCompareView two;
		run(QStringList{ l2, r2 }, two);
		const QStringList twoRows{ QStringLiteral("same.txt"),
			QStringLiteral("diff.txt"), QStringLiteral("left.txt"),
			QStringLiteral("right.txt"), QStringLiteral("data.bin") };
		bool ok = check(two, twoRows, F::ShowIdentical, { QStringLiteral("same.txt") })
			&& check(two, twoRows, F::ShowDifferent,
				{ QStringLiteral("diff.txt"), QStringLiteral("data.bin") })
			&& check(two, twoRows, F::ShowUniqueLeft, { QStringLiteral("left.txt") })
			&& check(two, twoRows, F::ShowUniqueRight, { QStringLiteral("right.txt") })
			&& check(two, twoRows, F::ShowBinaries, { QStringLiteral("data.bin") });

		FolderCompareView three;
		run(QStringList{ a, b, c }, three);
		const QStringList threeRows{ QStringLiteral("leftdiff.txt"),
			QStringLiteral("middiff.txt"), QStringLiteral("rightdiff.txt"),
			QStringLiteral("alldiff.txt"), QStringLiteral("middleonly.txt"),
			QStringLiteral("noleft.txt") };
		ok = ok
			&& check(three, threeRows, F::ShowDifferentLeftOnly,
				{ QStringLiteral("leftdiff.txt"), QStringLiteral("noleft.txt") })
			&& check(three, threeRows, F::ShowDifferentMiddleOnly,
				{ QStringLiteral("middiff.txt") })
			&& check(three, threeRows, F::ShowDifferentRightOnly,
				{ QStringLiteral("rightdiff.txt") })
			&& check(three, threeRows, F::ShowDifferent, { QStringLiteral("alldiff.txt") })
			&& check(three, threeRows, F::ShowUniqueMiddle,
				{ QStringLiteral("middleonly.txt") })
			&& check(three, threeRows, F::ShowMissingLeftOnly,
				{ QStringLiteral("noleft.txt") });
		printf("show filters: %s\n", ok ? "ok" : "FAILED");
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestMarkerOpt))
	{
		// issue #6: text present on one side only must leave a
		// zero-length marker span at the other side's insertion point
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		const QString leftPath = dir.filePath(QStringLiteral("l.txt"));
		const QString rightPath = dir.filePath(QStringLiteral("r.txt"));
		{
			QFile f(leftPath);
			f.open(QIODevice::WriteOnly);
			f.write("this is a test\nword only here\n");
		}
		{
			QFile f(rightPath);
			f.open(QIODevice::WriteOnly);
			f.write("this is test\nword here\n");
		}
		FileCompareView view;
		QString error;
		if (!view.compare(leftPath, rightPath, &error))
		{
			fprintf(stderr, "compare failed: %s\n", qPrintable(error));
			return 2;
		}
		const int leftMarkers = view.insertionMarkersForTest(0);
		const int rightMarkers = view.insertionMarkersForTest(1);
		FileCompareView swapped;
		if (!swapped.compare(rightPath, leftPath, &error))
			return 2;
		const int swappedLeft = swapped.insertionMarkersForTest(0);
		const int swappedRight = swapped.insertionMarkersForTest(1);
		printf("markers L/R: %d/%d, swapped L/R: %d/%d\n",
			leftMarkers, rightMarkers, swappedLeft, swappedRight);
		return (leftMarkers == 0 && rightMarkers == 2
			&& swappedLeft == 2 && swappedRight == 0) ? 0 : 1;
	}

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
		const QString zipPath = dir.filePath(QStringLiteral("left.zip"));
		const QString tgzPath = dir.filePath(QStringLiteral("right.tar.gz"));
		if (!writeTestArchive(zipPath, false,
				{ { "a.txt", "hello\n" }, { "sub/same.txt", "same\n" },
				  { "left-only.txt", "extra\n" } })
			|| !writeTestArchive(tgzPath, true,
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
		// the crash below came from closing the screen on Compare
		QSettings().setValue(QStringLiteral("General/CloseSelectorOnCompare"), true);
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
		// exactly one: a single Enter used to reach compare() twice when
		// the window-wide shortcut did not take the key first
		const qsizetype comparisons = window.findChildren<FileCompareView *>().size();
		printf("selector closed: %d, comparisons open: %lld\n",
			selectorClosed, static_cast<long long>(comparisons));
		return (selectorClosed && comparisons == 1) ? 0 : 1;
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
	else if (args.size() == 2 || args.size() == 3)
	{
		// folders and archives, in any mix, compare as folders
		if (MainWindow::allFolderLike(args))
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
