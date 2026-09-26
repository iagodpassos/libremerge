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
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QThread>
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
	QApplication::setApplicationVersion(QStringLiteral("0.9.5"));
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
	QCommandLineOption selftestMarkerOpt(QStringLiteral("selftest-marker"),
		QStringLiteral("Verify insertion markers where one side lacks text (for testing)"));
	parser.addOption(selftestMarkerOpt);
	QCommandLineOption selftestFolderSyncOpt(QStringLiteral("selftest-folder-sync"),
		QStringLiteral("Save a file opened from a folder compare and verify its row updates (for testing)"));
	parser.addOption(selftestFolderSyncOpt);
	QCommandLineOption selftestFolder3OpsOpt(QStringLiteral("selftest-folder3-ops"),
		QStringLiteral("Copy and delete rows in a 3-way folder compare and verify (for testing)"));
	parser.addOption(selftestFolder3OpsOpt);
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
