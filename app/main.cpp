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
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QRadioButton>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QToolButton>
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
#include <cstdlib>
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
#include "ComparisonResultFilterDialog.h"
#include "DisplayFilterBar.h"
#include "FileFilterCombo.h"
#include "FileFilterMenu.h"
#include "FileFilters.h"
#include "FilterConditionDialog.h"
#include "FiltersDialog.h"
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
// engine (option names and the file filter, for the selftests)
#include "OptionsDef.h"
#include "FileFilterHelper.h"

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
			// likewise the save prompt: unsaved changes are discarded
			MainWindow::setSavePromptForTest([]() { return 2; });
			// and no file dialog or editor opens from the Filters dialog
			FiltersDialog::setFileChooserForTest([](bool, const QString &) { return QString(); });
			FiltersDialog::setEditorForTest([](const QString &) {});
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
	lm::installFileFilters();
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
	QCommandLineOption screenshotFiltersOpt(QStringLiteral("screenshot-filters"),
		QStringLiteral("Render every Filters page to <file>-<n>.png, with a sample row each, and exit (for testing)"),
		QStringLiteral("file"));
	parser.addOption(screenshotFiltersOpt);
	QCommandLineOption screenshotSelectorOpt(QStringLiteral("screenshot-selector"),
		QStringLiteral("Render the \"Select Files or Folders\" screen, over the paths given, to <file> and exit (for testing)"),
		QStringLiteral("file"));
	parser.addOption(screenshotSelectorOpt);
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
	QCommandLineOption selftestCompareOptionsOpt(QStringLiteral("selftest-compare-options"),
		QStringLiteral("Verify the Compare options page and what its options do to file, table and folder comparisons (for testing)"));
	parser.addOption(selftestCompareOptionsOpt);
	QCommandLineOption selftestFiltersOpt(QStringLiteral("selftest-filters"),
		QStringLiteral("Verify the Filters dialog's pages, where the line and substitution filters are kept, what they do to comparisons and what OK rescans (for testing)"));
	parser.addOption(selftestFiltersOpt);
	QCommandLineOption selftestFileFiltersOpt(QStringLiteral("selftest-file-filters"),
		QStringLiteral("Verify the shipped preset filters, the File Filters page, the global file filter in folder comparisons, its status bar pane and the selection screen's filter field (for testing)"));
	parser.addOption(selftestFileFiltersOpt);
	QCommandLineOption selftestFilterMenuOpt(QStringLiteral("selftest-filter-menu"),
		QStringLiteral("Verify the file filter helper menu, its Filter Condition dialog and that folder comparisons go by the conditions it makes (for testing)"));
	parser.addOption(selftestFilterMenuOpt);
	QCommandLineOption selftestDisplayFilterOpt(QStringLiteral("selftest-display-filter"),
		QStringLiteral("Verify the folder window's display filter: its bar, what it hides in the tree and in the flat list, the columns' header menu and the Filter by Comparison Result dialog (for testing)"));
	parser.addOption(selftestDisplayFilterOpt);
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

	if (parser.isSet(screenshotSelectorOpt))
	{
		// the selection screen as the given paths leave it
		MainWindow window;
		window.resize(1100, 700);
		window.show();
		window.openSelector(parser.positionalArguments());
		if (auto *selector = window.findChild<NewComparisonView *>())
			selector->verifyPathsForTest();
		QCoreApplication::processEvents();
		return window.grab().save(parser.value(screenshotSelectorOpt)) ? 0 : 2;
	}

	if (parser.isSet(screenshotFiltersOpt))
	{
		// the three pages over the saved filters, the line and substitution
		// ones with a sample row each, added with the pages' own buttons;
		// nothing is saved
		FiltersDialog dialog;
		dialog.show();
		QCoreApplication::processEvents();
		auto *lines = dialog.findChild<QTreeWidget *>(QStringLiteral("lineFilters"));
		auto *pairs = dialog.findChild<QTreeWidget *>(QStringLiteral("substitutionFilters"));
		auto *newLine = dialog.findChild<QPushButton *>(QStringLiteral("newLineFilter"));
		auto *addPair = dialog.findChild<QPushButton *>(QStringLiteral("addSubstitutionFilter"));
		if (lines == nullptr || pairs == nullptr || newLine == nullptr || addPair == nullptr)
			return 2;
		newLine->click();
		QCoreApplication::processEvents();
		if (auto *editor = lines->viewport()->findChild<QLineEdit *>())
		{
			editor->setText(QStringLiteral("^\\s*Build date:"));
			QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
			QCoreApplication::sendEvent(editor, &enter);
			QCoreApplication::processEvents();
		}
		lines->topLevelItem(lines->topLevelItemCount() - 1)->setCheckState(0, Qt::Checked);
		addPair->click();
		QTreeWidgetItem *pair = pairs->topLevelItem(pairs->topLevelItemCount() - 1);
		pair->setText(1, QStringLiteral("colour"));
		pair->setCheckState(3, Qt::Checked);
		const QString path = parser.value(screenshotFiltersOpt);
		const int dot = path.lastIndexOf(QLatin1Char('.'));
		const int pages[] = { FiltersDialog::FileFiltersPage, FiltersDialog::LineFiltersPage,
			FiltersDialog::SubstitutionFiltersPage };
		for (int page : pages)
		{
			dialog.showPage(page);
			QCoreApplication::processEvents();
			QString target = path;
			target.insert(dot < 0 ? target.size() : dot, QStringLiteral("-%1").arg(page));
			if (!dialog.grab().save(target))
				return 2;
		}
		// the "=" button's menu, with one of its submenus, and the Filter
		// Condition dialog as "Custom Range..." of a file size opens it
		const auto named = [&path, dot](const char *suffix) {
			QString target = path;
			target.insert(dot < 0 ? target.size() : dot, QLatin1String(suffix));
			return target;
		};
		if (auto *menu = dialog.findChild<FileFilterMenu *>())
		{
			menu->ensurePolished();
			menu->adjustSize();
			if (!menu->grab().save(named("-menu")))
				return 2;
			if (QAction *item = menu->actionForTest(FileFilterMenu::SizeFirst))
				if (auto *submenu = qobject_cast<QMenu *>(item->parent()))
				{
					submenu->ensurePolished();
					submenu->adjustSize();
					if (!submenu->grab().save(named("-submenu")))
						return 2;
				}
		}
		FilterConditionDialog condition(false, 0, QStringLiteral("Size"), QString(),
			QStringLiteral("isWithin(%1, %2, %3)"), QStringLiteral("%1"), false);
		condition.show();
		QCoreApplication::processEvents();
		return condition.grab().save(named("-condition")) ? 0 : 2;
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

	if (parser.isSet(selftestCompareOptionsOpt))
	{
		// WinMerge's Options > Compare > General: the page as PropCompare
		// lays it out, and what the options do to a file, a table and a
		// folder comparison. The messages are collected instead of shown
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		QStringList shown;
		lm::setMessageSinkForTest([&shown](const QString &text) { shown.append(text); });
		bool ok = true;
		const auto check = [&ok](bool condition, const char *what)
		{
			printf("%s: %s\n", what, condition ? "ok" : "FAILED");
			ok = ok && condition;
		};
		const auto write = [](const QString &path, const QByteArray &bytes)
		{
			QDir().mkpath(QFileInfo(path).absolutePath());
			QFile f(path);
			f.open(QIODevice::WriteOnly);
			f.write(bytes);
		};
		const auto settle = []()
		{
			for (int i = 0; i < 3; ++i)
				QCoreApplication::processEvents();
		};
		const auto utf16 = [](const QString &text)
		{
			QByteArray bytes("\xFF\xFE", 2); // little endian, with its BOM
			for (const QChar c : text)
			{
				bytes.append(static_cast<char>(c.unicode() & 0xFF));
				bytes.append(static_cast<char>(c.unicode() >> 8));
			}
			return bytes;
		};
		const auto message = [](const char *text) {
			return QCoreApplication::translate("MessageBoxes", text);
		};
		const QString binaryMatch = message("Selected files are identical (binary match).");
		const QString binaryDiffer = message("Selected files are identical (with current settings).\n"
			"But differ at the binary level.");

		// --- the page ---
		{
			OptionsDialog dialog;
			dialog.selectCategoryForTest(2); // Compare > General
			dialog.grab(); // lays the page out
			QWidget *page = dialog.pageForTest(OptionsDialog::ComparePage);
			const auto top = [page](const QWidget *widget) {
				return widget->mapTo(page, QPoint(0, 0)).y();
			};
			// PropCompare's controls, top to bottom, with its texts
			const struct { const char *name; const char *text; } rows[] = {
				{ "ignoreBlankLines", QT_TRANSLATE_NOOP("OptionsDialog", "Ignore blank lines") },
				{ "ignoreCase", QT_TRANSLATE_NOOP("OptionsDialog", "Ignore case") },
				{ "ignoreEol", QT_TRANSLATE_NOOP("OptionsDialog",
					"Ignore EOL differences (Windows/Unix/Mac)") },
				{ "ignoreNumbers", QT_TRANSLATE_NOOP("OptionsDialog", "Ignore numbers") },
				{ "ignoreCodepage", QT_TRANSLATE_NOOP("OptionsDialog",
					"Ignore codepage differences") },
				{ "filterComments", QT_TRANSLATE_NOOP("OptionsDialog",
					"Ignore comment differences") },
				{ "ignoreMissingTrailingEol", QT_TRANSLATE_NOOP("OptionsDialog",
					"Ignore missing trailing EOL") },
				{ "ignoreLineBreaks", QT_TRANSLATE_NOOP("OptionsDialog",
					"Ignore line breaks (treat as spaces)") },
				{ "movedBlocks", QT_TRANSLATE_NOOP("OptionsDialog",
					"Enable moved block detection") },
				{ "diffAlgorithm", nullptr },
				{ "indentHeuristic", QT_TRANSLATE_NOOP("OptionsDialog",
					"Enable indent heuristic") },
				{ "blankOutIgnored", QT_TRANSLATE_NOOP("OptionsDialog",
					"Completely unhighlight the ignored differences") },
			};
			const QList<QRadioButton *> radios = page->findChildren<QRadioButton *>();
			bool layout = radios.size() == 3
				&& radios.at(0)->text() == OptionsDialog::tr("Compare")
				&& radios.at(1)->text() == OptionsDialog::tr("Ignore change")
				&& radios.at(2)->text() == OptionsDialog::tr("Ignore all")
				&& top(radios.at(0)) < top(radios.at(1))
				&& top(radios.at(1)) < top(radios.at(2));
			int above = layout ? top(radios.at(2)) : -1;
			for (const auto &row : rows)
			{
				auto *widget = page->findChild<QWidget *>(QLatin1String(row.name));
				auto *box = qobject_cast<QCheckBox *>(widget);
				const bool fits = widget != nullptr && top(widget) > above
					&& (row.text == nullptr
						|| (box != nullptr && box->text() == OptionsDialog::tr(row.text)));
				if (!fits)
					printf("  out of place: %s\n", row.name);
				layout = layout && fits;
				if (widget != nullptr)
					above = top(widget);
			}
			layout = layout && page->findChildren<QCheckBox *>().size() == 11;
			check(layout, "page: WinMerge's controls, order and texts");

			const auto box = [page](const char *name) {
				return page->findChild<QCheckBox *>(QLatin1String(name));
			};
			auto *algorithm = page->findChild<QComboBox *>(QStringLiteral("diffAlgorithm"));
			QCheckBox *heuristic = box("indentHeuristic");
			if (!layout || algorithm == nullptr || heuristic == nullptr)
				return 1;
			const QStringList algorithms = { OptionsDialog::tr("default"),
				OptionsDialog::tr("minimal"), OptionsDialog::tr("patience"),
				OptionsDialog::tr("histogram"), OptionsDialog::tr("none") };
			QStringList offered;
			for (int i = 0; i < algorithm->count(); ++i)
				offered.append(algorithm->itemText(i));
			check(offered == algorithms, "page: the five diff algorithms");

			// WinMerge's defaults: all off but the indent heuristic
			const auto atDefaults = [&]() {
				bool defaults = radios.at(0)->isChecked() && algorithm->currentIndex() == 0;
				for (QCheckBox *each : page->findChildren<QCheckBox *>())
					defaults = defaults && each->isChecked() == (each == heuristic);
				return defaults;
			};
			check(atDefaults(), "page: defaults");

			// only the xdiff algorithms take the indent heuristic
			bool rule = true;
			for (int i = 0; i < algorithm->count(); ++i)
			{
				algorithm->setCurrentIndex(i);
				rule = rule && heuristic->isEnabled() == (i != 0 && i != 4);
			}
			check(rule, "page: indent heuristic follows the algorithm");

			const char *const added[] = { "ignoreCodepage", "filterComments",
				"ignoreMissingTrailingEol", "ignoreLineBreaks", "blankOutIgnored" };
			for (const char *name : added)
				box(name)->setChecked(true);
			heuristic->setChecked(false);
			algorithm->setCurrentIndex(3); // histogram
			dialog.saveForTest();
			const DIFFOPTIONS saved = lm::currentDiffOptions();
			const QSettings settings;
			check(saved.bFilterCommentsLines && saved.bIgnoreMissingTrailingEol
				&& saved.bIgnoreLineBreaks && saved.bCompletelyBlankOutIgnoredChanges
				&& !saved.bIndentHeuristic && saved.nDiffAlgorithm == 3
				&& lm::ignoreCodepageDifferences()
				&& settings.value(QStringLiteral("Settings/IgnoreCodepage")).toBool()
				&& settings.value(QStringLiteral("Settings/FilterCommentsLines")).toBool()
				&& settings.value(QStringLiteral("Settings/IgnoreMissingTrailingEol")).toBool()
				&& settings.value(QStringLiteral("Settings/IgnoreLineBreaks")).toBool()
				&& settings.value(QStringLiteral(
					"Settings/CompletelyBlankOutIgnoredChanges")).toBool()
				&& !settings.value(QStringLiteral("Settings/IndentHeuristic")).toBool(),
				"page: saved to the options");

			// another dialog shows what was saved; Defaults puts it back
			OptionsDialog again;
			QWidget *pageAgain = again.pageForTest(OptionsDialog::ComparePage);
			bool loaded = !pageAgain->findChild<QCheckBox *>(
				QStringLiteral("indentHeuristic"))->isChecked()
				&& pageAgain->findChild<QComboBox *>(
					QStringLiteral("diffAlgorithm"))->currentIndex() == 3;
			for (const char *name : added)
				loaded = loaded
					&& pageAgain->findChild<QCheckBox *>(QLatin1String(name))->isChecked();
			check(loaded, "page: saved options shown again");
			dialog.restoreDefaultsForTest();
			check(atDefaults(), "page: Defaults");
			dialog.saveForTest();
			const DIFFOPTIONS reset = lm::currentDiffOptions();
			check(!reset.bFilterCommentsLines && !reset.bIgnoreMissingTrailingEol
				&& !reset.bIgnoreLineBreaks && !reset.bCompletelyBlankOutIgnoredChanges
				&& reset.bIndentHeuristic && reset.nDiffAlgorithm == 0
				&& !lm::ignoreCodepageDifferences(), "page: Defaults saved");
		}

		// --- file comparisons ---
		const QString leftC = dir.filePath(QStringLiteral("left.c"));
		const QString rightC = dir.filePath(QStringLiteral("right.c"));
		const QByteArray leftCode = "int total; // the sum\nint count;\n";
		const QByteArray rightCode = "int total; // sum of everything\nint count;\n";
		write(leftC, leftCode);
		write(rightC, rightCode);
		// the same two texts where no language tells what a comment is
		const QString leftPlain = dir.filePath(QStringLiteral("left.txt"));
		const QString rightPlain = dir.filePath(QStringLiteral("right.txt"));
		write(leftPlain, leftCode);
		write(rightPlain, rightCode);
		// a line break moved inside the same words; then with another
		// number of lines; then with a real change next to it
		const QString leftWrap = dir.filePath(QStringLiteral("wrap-left.txt"));
		const QString rightWrap = dir.filePath(QStringLiteral("wrap-right.txt"));
		const QByteArray leftWrapped = "alpha\none two\nthree\nomega\n";
		const QByteArray rightWrapped = "alpha\none\ntwo three\nomega\n";
		write(leftWrap, leftWrapped);
		write(rightWrap, rightWrapped);
		const QString leftTall = dir.filePath(QStringLiteral("tall-left.txt"));
		const QString rightTall = dir.filePath(QStringLiteral("tall-right.txt"));
		write(leftTall, "alpha\none two three\nomega\n");
		write(rightTall, "alpha\none\ntwo\nthree\nomega\n");
		const QString leftEdit = dir.filePath(QStringLiteral("edit-left.txt"));
		const QString rightEdit = dir.filePath(QStringLiteral("edit-right.txt"));
		write(leftEdit, "alpha\none two\nthree LEFT\nomega\n");
		write(rightEdit, "alpha\none\ntwo three RIGHT\nomega\n");
		// one text in two encodings, and with and without a BOM
		const QString text = QStringLiteral("héllo\nworld\n");
		const QString utf8File = dir.filePath(QStringLiteral("utf8.txt"));
		const QString utf8Copy = dir.filePath(QStringLiteral("utf8-copy.txt"));
		const QString utf8Bom = dir.filePath(QStringLiteral("utf8-bom.txt"));
		const QString utf16File = dir.filePath(QStringLiteral("utf16.txt"));
		write(utf8File, text.toUtf8());
		write(utf8Copy, text.toUtf8());
		write(utf8Bom, QByteArray("\xEF\xBB\xBF") + text.toUtf8());
		write(utf16File, utf16(text));

		struct Counts
		{
			int diffs = -1;
			int ignored = -1;
			int wordSpans = -1;
			QStringList messages;
		};
		const auto opened = [&](const QStringList &paths) {
			shown.clear();
			MainWindow window;
			window.openFileComparison(paths);
			settle();
			Counts counts;
			if (auto *view = window.findChild<FileCompareView *>())
			{
				counts.diffs = view->diffCount();
				counts.ignored = view->ignoredDiffCount();
				counts.wordSpans = view->wordSpanCountForTest(0);
			}
			counts.messages = shown;
			return counts;
		};
		const auto is = [](const Counts &counts, int diffs, int ignored,
			const QStringList &messages = {}) {
			if (counts.diffs != diffs || counts.ignored != ignored
				|| counts.messages != messages)
				printf("  got %d difference(s), %d ignored, %d message(s)\n", counts.diffs,
					counts.ignored, static_cast<int>(counts.messages.size()));
			return counts.diffs == diffs && counts.ignored == ignored
				&& counts.messages == messages;
		};

		lm::setCompareOptionsForTest(0);
		check(is(opened({ leftC, rightC }), 1, 0), "comments: a difference by default");
		lm::setCompareFlagForTest(OPT_CMP_FILTER_COMMENTLINES, true);
		check(is(opened({ leftC, rightC }), 0, 1, { binaryDiffer }),
			"comments: ignored, the files identical with the settings");
		check(is(opened({ leftPlain, rightPlain }), 1, 0),
			"comments: only where the language has them");
		lm::setCompareFlagForTest(OPT_CMP_COMPLETELY_BLANK_OUT_IGNORED_CHANGES, true);
		check(is(opened({ leftC, rightC }), 0, 0, { binaryDiffer }),
			"comments: unhighlighted completely");

		lm::setCompareOptionsForTest(0);
		check(is(opened({ leftWrap, rightWrap }), 1, 0), "line breaks: a difference by default");
		const Counts strictEdit = opened({ leftEdit, rightEdit });
		lm::setCompareFlagForTest(OPT_CMP_IGNORE_LINE_BREAKS, true);
		check(is(opened({ leftWrap, rightWrap }), 0, 1, { binaryDiffer }),
			"line breaks: ignored when the words are the same");
		check(is(opened({ leftTall, rightTall }), 0, 1, { binaryDiffer }),
			"line breaks: ignored across another number of lines");
		// inside a real difference the moved break is not highlighted
		const Counts looseEdit = opened({ leftEdit, rightEdit });
		printf("  word highlights on the left: %d strict, %d as spaces\n",
			strictEdit.wordSpans, looseEdit.wordSpans);
		check(is(strictEdit, 1, 0) && is(looseEdit, 1, 0)
			&& looseEdit.wordSpans == 1 && strictEdit.wordSpans > 1,
			"line breaks: a real change stays, its moved break unmarked");
		lm::setCompareFlagForTest(OPT_CMP_COMPLETELY_BLANK_OUT_IGNORED_CHANGES, true);
		check(is(opened({ leftWrap, rightWrap }), 0, 0, { binaryDiffer }),
			"line breaks: unhighlighted completely");
		check(is(opened({ leftTall, rightTall }), 0, 1, { binaryDiffer }),
			"line breaks: the extra lines stay marked");

		// the same text in another encoding: no difference to show, yet
		// not identical unless the option says so
		lm::setCompareOptionsForTest(0);
		check(is(opened({ utf8File, utf8Copy }), 0, 0, { binaryMatch }),
			"codepage: same encoding, identical");
		check(is(opened({ utf8File, utf16File }), 0, 0),
			"codepage: another encoding is not identical");
		check(is(opened({ utf8File, utf8Bom }), 0, 0),
			"codepage: a BOM alone counts too");
		check(is(opened({ utf8File, utf8Copy, utf16File }), 0, 0),
			"codepage: 3-way, one pane in another encoding");
		lm::setCompareFlagForTest(OPT_CMP_IGNORE_CODEPAGE, true);
		check(is(opened({ utf8File, utf16File }), 0, 0, { binaryDiffer }),
			"codepage: ignored, identical with the settings");
		check(is(opened({ utf8File, utf8Copy, utf16File }), 0, 0, { binaryDiffer }),
			"codepage: ignored, 3-way");

		// --- tables ---
		const QString utf8Table = dir.filePath(QStringLiteral("utf8.csv"));
		const QString utf16Table = dir.filePath(QStringLiteral("utf16.csv"));
		const QString otherTable = dir.filePath(QStringLiteral("other.csv"));
		const QString table = QStringLiteral("id,name\n1,café\n2,tea\n");
		write(utf8Table, table.toUtf8());
		write(utf16Table, utf16(table));
		write(otherTable, "id,name\n1,coffee\n2,tea\n");
		const auto openedTable = [&](const QString &left, const QString &right) {
			shown.clear();
			MainWindow window;
			window.openTableComparison(left, right);
			settle();
			auto *view = window.findChild<TableCompareView *>();
			return view != nullptr && view->diffCount() == 0 ? shown
				: QStringList{ QStringLiteral("?") };
		};
		lm::setCompareOptionsForTest(0);
		check(openedTable(utf8Table, utf16Table).isEmpty(),
			"table: another encoding is not identical");
		lm::setCompareFlagForTest(OPT_CMP_IGNORE_CODEPAGE, true);
		check(openedTable(utf8Table, utf16Table) == QStringList{ binaryDiffer },
			"table: codepage ignored, identical with the settings");

		// --- folders ---
		const QString leftDir = dir.filePath(QStringLiteral("L"));
		const QString rightDir = dir.filePath(QStringLiteral("R"));
		write(leftDir + QStringLiteral("/code.c"), leftCode);
		write(rightDir + QStringLiteral("/code.c"), rightCode);
		write(leftDir + QStringLiteral("/wrap.txt"), leftWrapped);
		write(rightDir + QStringLiteral("/wrap.txt"), rightWrapped);
		write(leftDir + QStringLiteral("/codepage.txt"), text.toUtf8());
		write(rightDir + QStringLiteral("/codepage.txt"), utf16(text));
		write(leftDir + QStringLiteral("/eol.txt"), "first\nlast\n");
		write(rightDir + QStringLiteral("/eol.txt"), "first\nlast");
		const QStringList names = { QStringLiteral("code.c"), QStringLiteral("wrap.txt"),
			QStringLiteral("codepage.txt"), QStringLiteral("eol.txt") };
		const QString textSame = QObject::tr("Text files are identical");
		const QString textDiff = QObject::tr("Text files are different");
		const auto waitFor = [](const FolderCompareView *view)
		{
			for (int i = 0; view != nullptr && i < 400 && view->isComparingForTest(); ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
		};
		// which of the four files a folder comparison finds identical
		const auto identicalIn = [&](const char *what) {
			FolderCompareView view;
			view.start(QStringList{ leftDir, rightDir });
			waitFor(&view);
			QStringList same;
			for (const QString &name : names)
			{
				const QString result = view.rowResultForTest(name);
				if (result == textSame)
					same.append(name);
				else if (result != textDiff)
					same.append(name + QStringLiteral(" (") + result + QLatin1Char(')'));
			}
			printf("  folder, %s: identical: %s\n", what,
				same.isEmpty() ? "none" : qPrintable(same.join(QStringLiteral(", "))));
			return same;
		};
		lm::setCompareOptionsForTest(0);
		check(identicalIn("defaults").isEmpty(), "folder: four differences by default");
		const struct { const String &option; const char *name; const char *what; } flags[] = {
			{ OPT_CMP_FILTER_COMMENTLINES, "code.c", "folder: comment differences ignored" },
			{ OPT_CMP_IGNORE_LINE_BREAKS, "wrap.txt", "folder: line breaks ignored" },
			{ OPT_CMP_IGNORE_CODEPAGE, "codepage.txt", "folder: codepage differences ignored" },
			{ OPT_CMP_IGNORE_MISSING_TRAILING_EOL, "eol.txt",
				"folder: missing trailing EOL ignored" },
		};
		for (const auto &flag : flags)
		{
			lm::setCompareOptionsForTest(0);
			lm::setCompareFlagForTest(flag.option, true);
			check(identicalIn(flag.name) == QStringList{ QLatin1String(flag.name) }, flag.what);
		}

		// --- OK in the Options dialog recompares what is open ---
		{
			lm::setCompareOptionsForTest(0);
			MainWindow window;
			window.openFileComparison({ leftC, rightC });
			window.openTableComparison(utf8Table, otherTable);
			window.openFolderComparison(QStringList{ leftDir, rightDir });
			settle();
			auto *file = window.findChild<FileCompareView *>();
			auto *tableView = window.findChild<TableCompareView *>();
			auto *folder = window.findChild<FolderCompareView *>();
			if (file == nullptr || tableView == nullptr || folder == nullptr)
				return 2;
			waitFor(folder);
			int fileScans = 0, tableScans = 0;
			QObject::connect(file, &FileCompareView::rescanned, [&fileScans]() { ++fileScans; });
			QObject::connect(tableView, &TableCompareView::rescanned,
				[&tableScans]() { ++tableScans; });
			// the dialog is modal: tick the box and press a button from
			// inside its event loop
			const auto answerDialog = [&window](bool tick, QDialogButtonBox::StandardButton button) {
				QTimer::singleShot(0, &window, [&window, tick, button]() {
					auto *dialog = window.findChild<OptionsDialog *>();
					auto *buttons = dialog != nullptr
						? dialog->findChild<QDialogButtonBox *>() : nullptr;
					if (buttons == nullptr)
					{
						printf("no Options dialog to answer\n");
						std::exit(3);
					}
					dialog->pageForTest(OptionsDialog::ComparePage)->findChild<QCheckBox *>(
						QStringLiteral("filterComments"))->setChecked(tick);
					buttons->button(button)->click();
				});
				QMetaObject::invokeMethod(&window, "showOptions");
			};

			shown.clear();
			answerDialog(true, QDialogButtonBox::Cancel);
			settle();
			check(fileScans == 0 && tableScans == 0 && file->diffCount() == 1
				&& !lm::currentDiffOptions().bFilterCommentsLines,
				"Options, Cancel: nothing recompared");

			answerDialog(true, QDialogButtonBox::Ok);
			settle();
			check(fileScans == 1 && file->diffCount() == 0 && file->ignoredDiffCount() == 1
				&& shown == QStringList{ binaryDiffer },
				"Options, OK: the open file comparison takes the new options");
			check(tableScans == 1 && tableView->diffCount() == 1,
				"Options, OK: the open table is recompared too");
			check(!folder->isComparingForTest()
				&& folder->rowResultForTest(QStringLiteral("code.c")) == textDiff,
				"Options, OK: a folder comparison keeps its results");

			// every OK rescans, whatever was changed
			answerDialog(true, QDialogButtonBox::Ok);
			settle();
			check(fileScans == 2 && tableScans == 2, "Options, OK again: recompared again");
		}

		lm::setCompareOptionsForTest(0);
		lm::setMessageSinkForTest([](const QString &) {});
		printf("compare options: %s\n", ok ? "ok" : "FAILED");
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestFiltersOpt))
	{
		// WinMerge's Tools > Filters: the Line Filters and Substitution
		// Filters pages as LineFiltersDlg and SubstitutionFiltersDlg have
		// them, where the filters are kept, what they do to comparisons
		// and what OK rescans (CMainFrame::OnToolsFilters)
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		QStringList shown;
		lm::setMessageSinkForTest([&shown](const QString &text) { shown.append(text); });
		QStringList asked;
		bool answer = false;
		lm::setQuestionSinkForTest([&asked, &answer](const QString &text, bool *) {
			asked.append(text);
			return answer;
		});
		bool ok = true;
		const auto check = [&ok](bool condition, const char *what)
		{
			printf("%s: %s\n", what, condition ? "ok" : "FAILED");
			ok = ok && condition;
		};
		const auto write = [](const QString &path, const QByteArray &bytes)
		{
			QDir().mkpath(QFileInfo(path).absolutePath());
			QFile f(path);
			f.open(QIODevice::WriteOnly);
			f.write(bytes);
		};
		const auto settle = []()
		{
			for (int i = 0; i < 3; ++i)
				QCoreApplication::processEvents();
		};
		const auto waitFor = [](const FolderCompareView *view)
		{
			for (int i = 0; view != nullptr && i < 400 && view->isComparingForTest(); ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
		};
		// type into the row editor a list has open, then Enter
		const auto typeInEditor = [](QTreeWidget *list, const QString &text)
		{
			QCoreApplication::processEvents();
			auto *editor = list->viewport()->findChild<QLineEdit *>();
			if (editor == nullptr)
				return false;
			editor->setText(text);
			QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
			QCoreApplication::sendEvent(editor, &enter);
			QCoreApplication::processEvents(); // the commit is queued
			return true;
		};
		const auto button = [](QWidget *scope, const char *name) {
			return scope->findChild<QPushButton *>(QLatin1String(name));
		};
		const auto click = [&button](QWidget *scope, const char *name)
		{
			if (QPushButton *found = button(scope, name))
				found->click();
			else
				printf("  no button %s\n", name);
		};
		const auto usable = [&button](QWidget *scope, const char *name) {
			const QPushButton *found = button(scope, name);
			return found != nullptr && found->isEnabled();
		};
		const QString stampExpression = QStringLiteral("^stamp:");

		// --- where the filters are kept ---
		{
			LineFiltersList lines;
			lm::copyLineFilters(&lines);
			SubstitutionFiltersList pairs;
			lm::copySubstitutionFilters(&pairs);
			check(!lm::lineFiltersEnabled() && lines.GetCount() == 0 && pairs.GetCount() == 0
				&& !pairs.GetEnabled() && lm::currentLineFilters() == nullptr
				&& lm::currentSubstitutionFilters() == nullptr,
				"storage: no filters, both kinds switched off, by default");

			// the one list LibreMerge kept up to 0.9.7
			QSettings().setValue(QStringLiteral("LineFilters/List"),
				QStringList{ QStringLiteral("1\t^one"), QStringLiteral("0\ttwo") });
			lm::reloadFiltersForTest();
			lm::copyLineFilters(&lines);
			const QSettings settings;
			check(lm::lineFiltersEnabled() && lines.GetCount() == 2
				&& lines.GetAt(0).filterStr == "^one" && lines.GetAt(0).enabled
				&& lines.GetAt(1).filterStr == "two" && !lines.GetAt(1).enabled
				&& !settings.contains(QStringLiteral("LineFilters/List"))
				&& settings.value(QStringLiteral("LineFilters/Values")).toInt() == 2
				&& settings.value(QStringLiteral("LineFilters/Filter00")).toString()
					== QStringLiteral("^one")
				&& settings.value(QStringLiteral("LineFilters/Enabled00")).toInt() == 1
				&& settings.value(QStringLiteral("LineFilters/Filter01")).toString()
					== QStringLiteral("two")
				&& settings.value(QStringLiteral("LineFilters/Enabled01")).toInt() == 0
				&& settings.value(QStringLiteral("Settings/IgnoreRegExp")).toBool()
				&& lm::currentLineFilters() != nullptr,
				"storage: the old list moves to WinMerge's layout, switched on");

			// a shorter list takes the leftovers away
			LineFiltersList one;
			one.AddFilter("^one", true);
			lm::saveLineFilters(false, one);
			check(settings.value(QStringLiteral("LineFilters/Values")).toInt() == 1
				&& !settings.contains(QStringLiteral("LineFilters/Filter01"))
				&& !settings.contains(QStringLiteral("LineFilters/Enabled01"))
				&& !settings.value(QStringLiteral("Settings/IgnoreRegExp")).toBool()
				&& lm::currentLineFilters() == nullptr,
				"storage: a shorter list, switched off");
			lm::saveLineFilters(false, LineFiltersList());
		}

		// --- the Line Filters page ---
		{
			FiltersDialog dialog;
			auto *page = dialog.findChild<QWidget *>(QStringLiteral("lineFiltersPage"));
			auto *enable = dialog.findChild<QCheckBox *>(QStringLiteral("enableLineFilters"));
			auto *list = dialog.findChild<QTreeWidget *>(QStringLiteral("lineFilters"));
			if (page == nullptr || enable == nullptr || list == nullptr)
				return 1;
			QStringList labels;
			for (const QLabel *label : page->findChildren<QLabel *>())
				labels.append(label->text());
			const auto text = [&](const char *name) {
				const QPushButton *found = button(&dialog, name);
				return found != nullptr ? found->text() : QString();
			};
			check(dialog.windowTitle() == FiltersDialog::tr("Filters")
				&& enable->text() == FiltersDialog::tr("Enable Line Filters")
				&& !enable->isChecked()
				&& labels == QStringList{
					FiltersDialog::tr("Regular Expressions (one per line):") }
				&& list->columnCount() == 1
				&& list->headerItem()->text(0) == FiltersDialog::tr("Regular expression")
				&& text("newLineFilter") == FiltersDialog::tr("New")
				&& text("editLineFilter") == FiltersDialog::tr("Edit")
				&& text("removeLineFilter") == FiltersDialog::tr("Remove"),
				"line filters page: WinMerge's controls and texts");
			check(usable(&dialog, "newLineFilter") && !usable(&dialog, "editLineFilter")
				&& !usable(&dialog, "removeLineFilter"),
				"line filters page: nothing selected, only New");

			click(&dialog, "newLineFilter");
			const bool typed = typeInEditor(list, stampExpression);
			QTreeWidgetItem *row = list->topLevelItem(0);
			check(typed && list->topLevelItemCount() == 1 && row->text(0) == stampExpression
				&& row->checkState(0) == Qt::Unchecked && row->isSelected()
				&& usable(&dialog, "editLineFilter") && usable(&dialog, "removeLineFilter"),
				"line filters page: New adds an unticked row and edits it");
			click(&dialog, "editLineFilter");
			check(typeInEditor(list, QStringLiteral("first"))
				&& list->topLevelItem(0)->text(0) == QStringLiteral("first"),
				"line filters page: Edit edits the selected row");

			click(&dialog, "newLineFilter");
			typeInEditor(list, QStringLiteral("second"));
			list->setCurrentItem(list->topLevelItem(0));
			click(&dialog, "removeLineFilter");
			check(list->topLevelItemCount() == 1
				&& list->topLevelItem(0)->text(0) == QStringLiteral("second")
				&& list->topLevelItem(0)->isSelected(),
				"line filters page: Remove selects the row that took its place");
			click(&dialog, "removeLineFilter");
			check(list->topLevelItemCount() == 0 && !usable(&dialog, "editLineFilter")
				&& !usable(&dialog, "removeLineFilter"),
				"line filters page: the last row removed");

			// a ticked expression that does not compile keeps the dialog open
			click(&dialog, "newLineFilter");
			typeInEditor(list, QStringLiteral("("));
			list->topLevelItem(0)->setCheckState(0, Qt::Checked);
			shown.clear();
			dialog.accept();
			printf("  message: %s\n", qPrintable(shown.value(0)));
			check(dialog.result() != QDialog::Accepted && shown.size() == 1
				&& shown.first().startsWith(QStringLiteral("#1: "))
				&& shown.first().size() > 6,
				"line filters page: a bad expression is refused, by its number");
			list->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
			enable->setChecked(true);
			dialog.accept();
			check(dialog.result() == QDialog::Accepted && dialog.lineFiltersEnabled()
				&& dialog.lineFilters().GetCount() == 1
				&& dialog.lineFilters().GetAt(0).filterStr == "("
				&& !dialog.lineFilters().GetAt(0).enabled,
				"line filters page: unticked, it is kept as typed");

			// the first row is selected when the dialog opens
			LineFiltersList two;
			two.AddFilter("one", false);
			two.AddFilter("two", true);
			lm::saveLineFilters(true, two);
			FiltersDialog again;
			auto *listAgain = again.findChild<QTreeWidget *>(QStringLiteral("lineFilters"));
			check(again.findChild<QCheckBox *>(QStringLiteral("enableLineFilters"))->isChecked()
				&& listAgain->topLevelItemCount() == 2
				&& listAgain->topLevelItem(0)->isSelected()
				&& listAgain->topLevelItem(0)->checkState(0) == Qt::Unchecked
				&& listAgain->topLevelItem(1)->checkState(0) == Qt::Checked
				&& usable(&again, "editLineFilter"),
				"line filters page: shows the saved filters, the first selected");
			lm::saveLineFilters(false, LineFiltersList());
		}

		// --- the Substitution Filters page ---
		{
			FiltersDialog dialog;
			dialog.showPage(FiltersDialog::SubstitutionFiltersPage);
			auto *page = dialog.findChild<QWidget *>(QStringLiteral("substitutionFiltersPage"));
			auto *enable = dialog.findChild<QCheckBox *>(
				QStringLiteral("enableSubstitutionFilters"));
			auto *list = dialog.findChild<QTreeWidget *>(QStringLiteral("substitutionFilters"));
			if (page == nullptr || enable == nullptr || list == nullptr)
				return 1;
			QStringList labels;
			for (const QLabel *label : page->findChildren<QLabel *>())
				labels.append(label->text());
			QStringList headers;
			for (int column = 0; column < list->columnCount(); ++column)
				headers.append(list->headerItem()->text(column));
			const auto text = [&](const char *name) {
				const QPushButton *found = button(&dialog, name);
				return found != nullptr ? found->text() : QString();
			};
			check(dialog.currentPage() == FiltersDialog::SubstitutionFiltersPage
				&& labels == QStringList{ FiltersDialog::tr(
					"Changes to the listed pairs below will be ignored or marked as "
					"insignificant. Patches are unaffected.") }
				&& enable->text() == FiltersDialog::tr("Enable") && !enable->isChecked()
				&& headers == QStringList{ FiltersDialog::tr("Find what"),
					FiltersDialog::tr("Replace with"), FiltersDialog::tr("Regular expression"),
					FiltersDialog::tr("Match case"),
					FiltersDialog::tr("Match whole word only") }
				&& text("addSubstitutionFilter") == FiltersDialog::tr("Add")
				&& text("removeSubstitutionFilter") == FiltersDialog::tr("Remove")
				&& text("clearSubstitutionFilters") == FiltersDialog::tr("Clear"),
				"substitution page: WinMerge's controls and texts");
			check(usable(&dialog, "addSubstitutionFilter")
				&& !usable(&dialog, "removeSubstitutionFilter")
				&& !usable(&dialog, "clearSubstitutionFilters"),
				"substitution page: nothing listed, only Add");

			click(&dialog, "addSubstitutionFilter");
			QTreeWidgetItem *row = list->topLevelItem(0);
			const QString editHere = FiltersDialog::tr("<Edit here>");
			check(list->topLevelItemCount() == 1 && row->text(0) == editHere
				&& row->text(1) == editHere && row->checkState(0) == Qt::Checked
				&& row->checkState(2) == Qt::Unchecked && row->checkState(3) == Qt::Unchecked
				&& row->checkState(4) == Qt::Unchecked && row->isSelected()
				&& usable(&dialog, "removeSubstitutionFilter")
				&& usable(&dialog, "clearSubstitutionFilters"),
				"substitution page: Add lists a ticked pair to edit");

			// the two texts edit in place; the other columns only tick
			list->editItem(row, 0);
			const bool typedPattern = typeInEditor(list, QStringLiteral("foo"));
			list->editItem(row, 1);
			const bool typedReplacement = typeInEditor(list, QStringLiteral("bar"));
			list->editItem(row, 2);
			QCoreApplication::processEvents();
			const bool noEditor = list->viewport()->findChild<QLineEdit *>() == nullptr;
			check(typedPattern && typedReplacement && row->text(0) == QStringLiteral("foo")
				&& row->text(1) == QStringLiteral("bar") && noEditor,
				"substitution page: the texts edit in place, the other columns do not");

			// with a regular expression "whole word" does not count
			row->setCheckState(2, Qt::Checked);
			row->setCheckState(4, Qt::Checked);
			enable->setChecked(true);
			dialog.accept();
			const SubstitutionFiltersList &accepted = dialog.substitutionFilters();
			check(dialog.result() == QDialog::Accepted && accepted.GetEnabled()
				&& accepted.GetCount() == 1 && accepted.GetAt(0).pattern == "foo"
				&& accepted.GetAt(0).replacement == "bar" && accepted.GetAt(0).enabled
				&& accepted.GetAt(0).useRegExp && !accepted.GetAt(0).caseSensitive
				&& !accepted.GetAt(0).matchWholeWordOnly,
				"substitution page: the pair as accepted");

			// a ticked regular expression that does not compile
			FiltersDialog bad;
			auto *badList = bad.findChild<QTreeWidget *>(QStringLiteral("substitutionFilters"));
			click(&bad, "addSubstitutionFilter");
			click(&bad, "addSubstitutionFilter");
			badList->topLevelItem(1)->setText(0, QStringLiteral("("));
			badList->topLevelItem(1)->setCheckState(2, Qt::Checked);
			shown.clear();
			bad.accept();
			printf("  message: %s\n", qPrintable(shown.value(0)));
			check(bad.result() != QDialog::Accepted && shown.size() == 1
				&& shown.first().startsWith(QStringLiteral("#2: "))
				&& bad.currentPage() == FiltersDialog::SubstitutionFiltersPage,
				"substitution page: a bad expression is refused, on its page");
			badList->setCurrentItem(badList->topLevelItem(0));
			click(&bad, "removeSubstitutionFilter");
			const bool removed = badList->topLevelItemCount() == 1
				&& badList->topLevelItem(0)->text(0) == QStringLiteral("(")
				&& badList->topLevelItem(0)->isSelected();
			click(&bad, "clearSubstitutionFilters");
			check(removed && badList->topLevelItemCount() == 0
				&& !usable(&bad, "removeSubstitutionFilter")
				&& !usable(&bad, "clearSubstitutionFilters"),
				"substitution page: Remove and Clear");
		}

		// --- what the filters do to comparisons ---
		const QByteArray leftStamp = "alpha\nstamp: 2026-01-01\nomega\n";
		const QByteArray rightStamp = "alpha\nstamp: 2026-02-02\nomega\n";
		const QString leftText = dir.filePath(QStringLiteral("left.txt"));
		const QString rightText = dir.filePath(QStringLiteral("right.txt"));
		write(leftText, leftStamp);
		write(rightText, rightStamp);
		const QString leftTable = dir.filePath(QStringLiteral("left.csv"));
		const QString rightTable = dir.filePath(QStringLiteral("right.csv"));
		write(leftTable, "key,value\nstamp:,2026-01-01\nname,x\n");
		write(rightTable, "key,value\nstamp:,2026-02-02\nname,x\n");
		const QString stamp = QStringLiteral("stamp.txt");
		const QString other = QStringLiteral("other.txt");
		QStringList folderPairs[2];
		for (int i = 0; i < 2; ++i)
		{
			const QString number = QString::number(i + 1);
			folderPairs[i] = { dir.filePath(QStringLiteral("L") + number),
				dir.filePath(QStringLiteral("R") + number) };
			write(folderPairs[i].at(0) + QLatin1Char('/') + stamp, leftStamp);
			write(folderPairs[i].at(1) + QLatin1Char('/') + stamp, rightStamp);
			write(folderPairs[i].at(0) + QLatin1Char('/') + other, "one\n");
			write(folderPairs[i].at(1) + QLatin1Char('/') + other, "two\n");
		}
		const QString textSame = QObject::tr("Text files are identical");
		const QString textDiff = QObject::tr("Text files are different");
		const QString binaryDiffer = QCoreApplication::translate("MessageBoxes",
			"Selected files are identical (with current settings).\n"
			"But differ at the binary level.");
		const QString question = QCoreApplication::translate("MessageBoxes",
			"Filters updated. Refresh all open folder compares?\n\n"
			"Select 'No' to refresh later.");
		lm::setCompareOptionsForTest(0);
		{
			// text, table and folder: significant differences, ignored ones
			const auto compared = [&]() {
				QString result;
				{
					MainWindow window;
					window.openFileComparison({ leftText, rightText });
					window.openTableComparison(leftTable, rightTable);
					settle();
					auto *file = window.findChild<FileCompareView *>();
					auto *table = window.findChild<TableCompareView *>();
					result = QStringLiteral("text %1+%2, table %3, ")
						.arg(file != nullptr ? file->diffCount() : -1)
						.arg(file != nullptr ? file->ignoredDiffCount() : -1)
						.arg(table != nullptr ? table->diffCount() : -1);
				}
				FolderCompareView view;
				view.start(folderPairs[0]);
				waitFor(&view);
				result += view.rowResultForTest(stamp) == textSame ? QStringLiteral("folder same")
					: view.rowResultForTest(stamp) == textDiff ? QStringLiteral("folder different")
					: view.rowResultForTest(stamp);
				printf("  %s\n", qPrintable(result));
				return result;
			};
			const QString different = QStringLiteral("text 1+0, table 1, folder different");
			const QString ignored = QStringLiteral("text 0+1, table 0, folder same");
			LineFiltersList lines;
			lines.AddFilter(stampExpression.toStdString(), true);
			lm::saveLineFilters(false, lines);
			check(compared() == different, "line filters: listed but switched off");
			lm::saveLineFilters(true, lines);
			check(compared() == ignored, "line filters: switched on, the line is ignored");
			lm::saveLineFilters(false, LineFiltersList());

			SubstitutionFiltersList pairs;
			pairs.Add("2026-01-01", "2026-02-02", false, true, false, true);
			pairs.SetEnabled(false);
			lm::saveSubstitutionFilters(pairs);
			check(compared() == different, "substitution filters: listed but switched off");
			pairs.SetEnabled(true);
			lm::saveSubstitutionFilters(pairs);
			check(compared() == ignored, "substitution filters: the listed pair is ignored");
			lm::saveSubstitutionFilters(SubstitutionFiltersList());
		}

		// --- OK in the dialog: what is rescanned ---
		MainWindow window;
		window.openFileComparison({ leftText, rightText });
		window.openTableComparison(leftTable, rightTable);
		window.openFolderComparison(folderPairs[0]);
		window.openFolderComparison(folderPairs[1]);
		window.openSelector();
		settle();
		auto *tabs = window.findChild<QTabWidget *>();
		auto *file = window.findChild<FileCompareView *>();
		auto *table = window.findChild<TableCompareView *>();
		const QList<FolderCompareView *> folders = window.findChildren<FolderCompareView *>();
		auto *selector = window.findChild<NewComparisonView *>();
		if (tabs == nullptr || file == nullptr || table == nullptr || folders.size() != 2
			|| selector == nullptr)
			return 2;
		for (const FolderCompareView *folder : folders)
			waitFor(folder);
		int fileScans = 0, tableScans = 0;
		QObject::connect(file, &FileCompareView::rescanned, [&fileScans]() { ++fileScans; });
		QObject::connect(table, &TableCompareView::rescanned, [&tableScans]() { ++tableScans; });
		const auto foldersComparing = [&folders]() {
			int comparing = 0;
			for (const FolderCompareView *folder : folders)
				comparing += folder->isComparingForTest() ? 1 : 0;
			return comparing;
		};
		const auto folderResults = [&folders](const QString &name) {
			QStringList results;
			for (const FolderCompareView *folder : folders)
				results.append(folder->rowResultForTest(name));
			return results;
		};
		const auto savedLines = []() {
			LineFiltersList lines;
			lm::copyLineFilters(&lines);
			QStringList saved{ lm::lineFiltersEnabled() ? QStringLiteral("on")
				: QStringLiteral("off") };
			for (size_t i = 0; i < lines.GetCount(); ++i)
				saved.append((lines.GetAt(i).enabled ? QStringLiteral("1 ") : QStringLiteral("0 "))
					+ QString::fromStdString(lines.GetAt(i).filterStr));
			return saved;
		};
		const auto savedPairs = []() {
			SubstitutionFiltersList pairs;
			lm::copySubstitutionFilters(&pairs);
			QStringList saved{ pairs.GetEnabled() ? QStringLiteral("on") : QStringLiteral("off") };
			for (size_t i = 0; i < pairs.GetCount(); ++i)
				saved.append(QString::fromStdString(pairs.GetAt(i).pattern) + QStringLiteral(" > ")
					+ QString::fromStdString(pairs.GetAt(i).replacement));
			return saved;
		};
		check(file->diffCount() == 1 && table->diffCount() == 1
			&& folderResults(stamp) == QStringList{ textDiff, textDiff }
			&& savedLines() == QStringList{ QStringLiteral("off") }
			&& savedPairs() == QStringList{ QStringLiteral("off") },
			"no filters: the line is a difference everywhere");

		// the dialog is window-modal: edit it and press a button from
		// inside its event loop
		using Edit = std::function<void(FiltersDialog *)>;
		const auto withDialog = [&window](const Edit &edit,
			QDialogButtonBox::StandardButton pressed)
		{
			QTimer::singleShot(0, &window, [&window, edit, pressed]() {
				auto *dialog = window.findChild<FiltersDialog *>();
				auto *buttons = dialog != nullptr
					? dialog->findChild<QDialogButtonBox *>() : nullptr;
				if (buttons == nullptr)
				{
					printf("no Filters dialog to answer\n");
					std::exit(3);
				}
				if (edit)
					edit(dialog);
				buttons->button(pressed)->click();
			});
			QMetaObject::invokeMethod(&window, "showFilters");
		};
		const auto lineList = [](FiltersDialog *dialog) {
			return dialog->findChild<QTreeWidget *>(QStringLiteral("lineFilters"));
		};
		const auto lineSwitch = [](FiltersDialog *dialog) {
			return dialog->findChild<QCheckBox *>(QStringLiteral("enableLineFilters"));
		};
		// New, type the expression, tick it
		const auto addingLine = [&](const QString &expression) -> Edit {
			return [&, expression](FiltersDialog *dialog) {
				click(dialog, "newLineFilter");
				if (!typeInEditor(lineList(dialog), expression))
					printf("  no editor on the new row\n");
				QTreeWidget *list = lineList(dialog);
				list->topLevelItem(list->topLevelItemCount() - 1)->setCheckState(0, Qt::Checked);
				if (!dialog->isVisible())
					printf("  Enter in the editor closed the dialog\n");
			};
		};
		const auto switchingLines = [&](bool on) -> Edit {
			return [&, on](FiltersDialog *dialog) { lineSwitch(dialog)->setChecked(on); };
		};
		const auto switchingPairs = [](bool on) -> Edit {
			return [on](FiltersDialog *dialog) {
				dialog->findChild<QCheckBox *>(
					QStringLiteral("enableSubstitutionFilters"))->setChecked(on);
			};
		};
		const QString on = QStringLiteral("on");
		const QString off = QStringLiteral("off");
		const QString stampLine = QStringLiteral("1 ") + stampExpression;

		// a text comparison in front
		tabs->setCurrentWidget(file);
		settle();
		shown.clear();
		withDialog([&](FiltersDialog *dialog) {
			addingLine(stampExpression)(dialog);
			lineSwitch(dialog)->setChecked(true);
		}, QDialogButtonBox::Ok);
		settle();
		check(savedLines() == QStringList{ on, stampLine }, "a new line filter is saved");
		check(fileScans == 1 && file->diffCount() == 0 && file->ignoredDiffCount() == 1,
			"text in front: the text comparison is rescanned at once");
		check(tableScans == 1 && table->diffCount() == 0, "text in front: the table too");
		check(shown == QStringList{ binaryDiffer, binaryDiffer },
			"text in front: identical files are reported");
		check(asked.isEmpty() && foldersComparing() == 0
			&& folderResults(stamp) == QStringList{ textDiff, textDiff },
			"text in front: folder comparisons are left alone");

		withDialog({}, QDialogButtonBox::Ok);
		settle();
		check(fileScans == 1 && tableScans == 1, "unchanged filters: nothing is rescanned");

		withDialog(switchingLines(false), QDialogButtonBox::Cancel);
		settle();
		check(fileScans == 1 && tableScans == 1
			&& savedLines() == QStringList{ on, stampLine },
			"Cancel: nothing is saved or rescanned");

		// a table is the same kind of document; the switch alone is a change
		tabs->setCurrentWidget(table);
		settle();
		withDialog(switchingLines(false), QDialogButtonBox::Ok);
		settle();
		check(savedLines() == QStringList{ off, stampLine } && fileScans == 2
			&& tableScans == 2 && file->diffCount() == 1 && file->ignoredDiffCount() == 0
			&& table->diffCount() == 1,
			"table in front: line filters switched off, both rescanned");

		// substitution filters rescan them as well
		tabs->setCurrentWidget(file);
		settle();
		withDialog([&](FiltersDialog *dialog) {
			dialog->showPage(FiltersDialog::SubstitutionFiltersPage);
			click(dialog, "addSubstitutionFilter");
			auto *list = dialog->findChild<QTreeWidget *>(QStringLiteral("substitutionFilters"));
			list->topLevelItem(0)->setText(0, QStringLiteral("2026-01-01"));
			list->topLevelItem(0)->setText(1, QStringLiteral("2026-02-02"));
			switchingPairs(true)(dialog);
		}, QDialogButtonBox::Ok);
		settle();
		check(savedPairs() == QStringList{ on, QStringLiteral("2026-01-01 > 2026-02-02") }
			&& fileScans == 3 && tableScans == 3 && file->diffCount() == 0
			&& file->ignoredDiffCount() == 1 && table->diffCount() == 0,
			"text in front: a new substitution filter rescans both");
		{
			FiltersDialog reopened;
			check(reopened.currentPage() == FiltersDialog::SubstitutionFiltersPage,
				"the dialog opens on the page it was accepted on");
		}

		// a folder comparison in front: substitution filters alone change
		// nothing there
		tabs->setCurrentWidget(folders.at(0));
		settle();
		asked.clear();
		withDialog(switchingPairs(false), QDialogButtonBox::Ok);
		const int startedOnPairs = foldersComparing();
		settle();
		check(savedPairs().value(0) == off && asked.isEmpty() && startedOnPairs == 0
			&& fileScans == 3 && tableScans == 3,
			"folder in front: changed substitution filters are only saved");

		// changed line filters ask first
		answer = false;
		withDialog(switchingLines(true), QDialogButtonBox::Ok);
		const int startedOnNo = foldersComparing();
		settle();
		check(asked == QStringList{ question }, "folder in front: asks before refreshing");
		check(startedOnNo == 0 && savedLines() == QStringList{ on, stampLine }
			&& folderResults(stamp) == QStringList{ textDiff, textDiff },
			"folder in front, No: saved, refreshed later");
		check(fileScans == 3 && tableScans == 3 && file->diffCount() == 0,
			"folder in front: text and table comparisons are left alone");

		asked.clear();
		answer = true;
		const QString second = QStringLiteral("^never there$");
		withDialog([&](FiltersDialog *dialog) {
			dialog->showPage(FiltersDialog::LineFiltersPage);
			addingLine(second)(dialog);
		}, QDialogButtonBox::Ok);
		const int startedOnYes = foldersComparing();
		for (const FolderCompareView *folder : folders)
			waitFor(folder);
		settle();
		check(asked == QStringList{ question } && startedOnYes == 2,
			"folder in front, Yes: every folder comparison is refreshed");
		check(folderResults(stamp) == QStringList{ textSame, textSame }
			&& folderResults(other) == QStringList{ textDiff, textDiff },
			"the refreshed folder comparisons take the filters");
		check(fileScans == 3 && tableScans == 3, "folder in front, Yes: no text rescans");

		// nothing to rescan in front: the filters are only saved
		tabs->setCurrentWidget(selector);
		settle();
		asked.clear();
		withDialog([&](FiltersDialog *dialog) {
			lineList(dialog)->setCurrentItem(lineList(dialog)->topLevelItem(0));
			click(dialog, "removeLineFilter");
		}, QDialogButtonBox::Ok);
		const int startedElsewhere = foldersComparing();
		settle();
		check(savedLines() == QStringList{ on, QStringLiteral("1 ") + second }
			&& asked.isEmpty() && startedElsewhere == 0 && fileScans == 3 && tableScans == 3,
			"another tab in front: saved, nothing rescanned");

		lm::setMessageSinkForTest([](const QString &) {});
		lm::setQuestionSinkForTest([](const QString &, bool *) { return false; });
		printf("filters: %s\n", ok ? "ok" : "FAILED");
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestFileFiltersOpt))
	{
		// WinMerge's file filter: the preset filters shipped with the
		// application, the File Filters page (FileFiltersDlg), the one
		// global filter folder comparisons go by, its status bar pane and
		// the "Folder: Filter" field of the selection screen
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		QStringList shown;
		lm::setMessageSinkForTest([&shown](const QString &text) { shown.append(text); });
		QStringList asked;
		bool answer = false;
		lm::setQuestionSinkForTest([&asked, &answer](const QString &text, bool *) {
			asked.append(text);
			return answer;
		});
		QStringList edited;
		FiltersDialog::setEditorForTest([&edited](const QString &path) { edited.append(path); });
		QString chosen; // what the file dialogs answer
		QStringList chooserCalls;
		FiltersDialog::setFileChooserForTest([&](bool save, const QString &folder) {
			chooserCalls.append((save ? QStringLiteral("save ") : QStringLiteral("open ")) + folder);
			return chosen;
		});
		bool ok = true;
		const auto check = [&ok](bool condition, const char *what)
		{
			printf("%s: %s\n", what, condition ? "ok" : "FAILED");
			ok = ok && condition;
		};
		const auto write = [](const QString &path, const QByteArray &bytes)
		{
			QDir().mkpath(QFileInfo(path).absolutePath());
			QFile f(path);
			f.open(QIODevice::WriteOnly | QIODevice::Truncate);
			f.write(bytes);
		};
		const auto settle = []()
		{
			for (int i = 0; i < 3; ++i)
				QCoreApplication::processEvents();
		};
		const auto waitFor = [](const FolderCompareView *view)
		{
			for (int i = 0; view != nullptr && i < 400 && view->isComparingForTest(); ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
		};
		const auto button = [](QWidget *scope, const char *name) {
			return scope->findChild<QPushButton *>(QLatin1String(name));
		};
		const auto click = [&button](QWidget *scope, const char *name)
		{
			if (QPushButton *found = button(scope, name))
				found->click();
			else
				printf("  no button %s\n", name);
		};
		const auto usable = [&button](QWidget *scope, const char *name) {
			const QPushButton *found = button(scope, name);
			return found != nullptr && found->isEnabled();
		};
		const auto leaveField = [](FileFilterCombo *combo)
		{
			QFocusEvent out(QEvent::FocusOut);
			QCoreApplication::sendEvent(combo->lineEdit(), &out);
		};
		const auto type = [&leaveField](FileFilterCombo *combo, const QString &text)
		{
			combo->setEditText(text);
			leaveField(combo);
		};
		const QString sourceControl = QStringLiteral("Exclude Source Control");
		const QString settingsKey = QStringLiteral("Settings/FileFilterCurrent");

		// --- what ships with the application ---
		{
			const QDir shipped(lm::sharedFiltersDir());
			const std::vector<FileFilterInfo> presets = lm::globalFileFilter()->GetFileFilters();
			QStringList names;
			for (const FileFilterInfo &preset : presets)
				names.append(QString::fromStdString(preset.name));
			printf("  shipped filters in %s: %d\n", qPrintable(shipped.path()),
				static_cast<int>(presets.size()));
			check(shipped.exists() && presets.size() == 12 && names.contains(sourceControl)
				&& names.contains(QStringLiteral("Visual C++ loose"))
				&& shipped.exists(QStringLiteral("FileFilter.tmpl")),
				"shipped: WinMerge's twelve preset filters and the template");
			check(lm::fileFilterMask() == QStringLiteral("*.*")
				&& !lm::userFiltersDir().isEmpty()
				&& lm::userFiltersDir().endsWith(QStringLiteral("/Filters")),
				"the filter is *.* by default, the user's folder apart");
		}

		// from here on: filter folders of the test's own. Two presets and
		// the template stand for the shipped ones
		const QString sharedDir = dir.filePath(QStringLiteral("shared"));
		const QString userDir = dir.filePath(QStringLiteral("user"));
		QDir().mkpath(sharedDir);
		{
			const QDir shipped(lm::sharedFiltersDir());
			for (const char *name : { "SourceControl.flt", "Merge_GnuC_loose.flt",
					"FileFilter.tmpl" })
				QFile::copy(shipped.filePath(QLatin1String(name)),
					QDir(sharedDir).filePath(QLatin1String(name)));
		}
		lm::setFiltersDirsForTest(sharedDir, userDir);

		// --- the mask the folder comparison's own field kept until 0.9.7 ---
		{
			QSettings().setValue(QStringLiteral("FolderCompare/Filter"),
				QStringLiteral("*.cpp;*.h"));
			lm::installFileFilters();
			const QSettings settings;
			check(lm::fileFilterMask() == QStringLiteral("*.cpp;*.h")
				&& !settings.contains(QStringLiteral("FolderCompare/Filter"))
				&& settings.value(settingsKey).toString() == QStringLiteral("*.cpp;*.h"),
				"the old field's mask becomes the file filter");
			lm::setFileFilterMask(QString());
			check(lm::fileFilterMask() == QStringLiteral("*.*")
				&& settings.value(settingsKey).toString() == QStringLiteral("*.*"),
				"no mask is *.*");
		}

		// --- the File Filters page ---
		{
			QSettings().remove(QStringLiteral("Settings/FilterStartPage"));
			FiltersDialog dialog;
			auto *page = dialog.findChild<QWidget *>(QStringLiteral("fileFiltersPage"));
			auto *mask = dialog.findChild<FileFilterCombo *>(QStringLiteral("fileFilterMask"));
			auto *list = dialog.findChild<QTreeWidget *>(QStringLiteral("presetFilters"));
			if (page == nullptr || mask == nullptr || list == nullptr)
				return 1;
			QStringList labels;
			for (const QLabel *label : page->findChildren<QLabel *>())
				labels.append(label->text());
			QStringList headers;
			for (int column = 0; column < list->columnCount(); ++column)
				headers.append(list->headerItem()->text(column));
			const auto text = [&](const char *name) {
				const QPushButton *found = button(&dialog, name);
				return found != nullptr ? found->text() : QString();
			};
			check(dialog.currentPage() == FiltersDialog::FileFiltersPage
				&& labels == QStringList{ FiltersDialog::tr("Mask / Filter Expression"),
					FiltersDialog::tr("Preset Filters") }
				&& mask->lineEdit()->placeholderText() == FiltersDialog::tr("e.g. %1")
					.arg(QStringLiteral("*.txt|fe:Size > 100KB"))
				&& headers == QStringList{ FiltersDialog::tr("Name"),
					FiltersDialog::tr("Description"), FiltersDialog::tr("Location") }
				&& text("testFileFilter") == FiltersDialog::tr("Test...")
				&& text("installFileFilter") == FiltersDialog::tr("Install...")
				&& text("newFileFilter") == FiltersDialog::tr("New...")
				&& text("editFileFilter") == FiltersDialog::tr("Edit...")
				&& text("deleteFileFilter") == FiltersDialog::tr("Delete..."),
				"page: the first one, with WinMerge's controls and texts");

			const auto row = [list](const QString &name) -> QTreeWidgetItem * {
				for (int i = 0; i < list->topLevelItemCount(); ++i)
					if (list->topLevelItem(i)->text(0) == name)
						return list->topLevelItem(i);
				return nullptr;
			};
			const auto ticked = [list]() {
				QStringList names;
				for (int i = 0; i < list->topLevelItemCount(); ++i)
					if (list->topLevelItem(i)->checkState(0) == Qt::Checked)
						names.append(list->topLevelItem(i)->text(0));
				return names;
			};
			QTreeWidgetItem *preset = row(sourceControl);
			check(list->topLevelItemCount() == 2 && preset != nullptr
				&& preset->text(1) == QStringLiteral("Exclude Source Control files and directories")
				&& preset->text(2) == QDir(sharedDir).filePath(QStringLiteral("SourceControl.flt"))
				&& ticked().isEmpty() && mask->mask() == QStringLiteral("*.*")
				&& usable(&dialog, "testFileFilter") && usable(&dialog, "installFileFilter")
				&& usable(&dialog, "newFileFilter") && !usable(&dialog, "editFileFilter")
				&& !usable(&dialog, "deleteFileFilter"),
				"page: the preset filters listed, the filter in use in the field");
			if (preset == nullptr)
				return 1;

			// a tick writes the preset into the mask, and back
			preset->setCheckState(0, Qt::Checked);
			const QString withPreset = mask->mask();
			preset->setCheckState(0, Qt::Unchecked);
			check(withPreset == QStringLiteral("*.*;pf:") + sourceControl
				&& mask->mask() == QStringLiteral("*.*"),
				"page: ticking a preset names it in the mask");
			// a mask typed with a preset ticks it once the field is left
			type(mask, QStringLiteral("*.c;pf:") + sourceControl);
			const QStringList tickedByMask = ticked();
			type(mask, QStringLiteral("*.c"));
			check(tickedByMask == QStringList{ sourceControl } && ticked().isEmpty(),
				"page: a preset named in the mask is ticked");

			// what does not parse turns the field red and says why
			type(mask, QStringLiteral("*.*|fe:Size >"));
			const QStringList syntaxErrors = mask->errors();
			const bool red = !mask->lineEdit()->styleSheet().isEmpty()
				&& !mask->lineEdit()->toolTip().isEmpty();
			type(mask, QStringLiteral("pf:No such filter"));
			const QStringList nameErrors = mask->errors();
			type(mask, QStringLiteral("*.*|fe:Size > 100KB"));
			printf("  errors: %s | %s\n", qPrintable(syntaxErrors.join(QStringLiteral(" / "))),
				qPrintable(nameErrors.join(QStringLiteral(" / "))));
			check(syntaxErrors.size() == 1 && red && nameErrors.size() == 1
				&& nameErrors.first().startsWith(QCoreApplication::translate("FileFilters",
					"Filter name not found"))
				&& nameErrors.first().endsWith(QStringLiteral(": No such filter"))
				&& mask->errors().isEmpty() && mask->lineEdit()->styleSheet().isEmpty(),
				"page: a mask that does not parse is marked, with the reason");

			// Test...: names against the mask as it stands
			const auto tested = [&](const QString &maskText, const QString &name, bool folder) {
				type(mask, maskText);
				QString result;
				QTimer::singleShot(0, &dialog, [&]() {
					auto *test = dialog.findChild<QDialog *>(QStringLiteral("testFilterDialog"));
					if (test == nullptr)
					{
						printf("no Test Filter dialog\n");
						std::exit(3);
					}
					test->findChild<QLineEdit *>(QStringLiteral("testFilterText"))->setText(name);
					test->findChild<QCheckBox *>(QStringLiteral("testFilterIsFolder"))
						->setChecked(folder);
					click(test, "testFilterRun");
					result = test->findChild<QLabel *>(QStringLiteral("testFilterName"))->text()
						+ QStringLiteral(" -> ") + test->findChild<QPlainTextEdit *>(
							QStringLiteral("testFilterResults"))->toPlainText();
					test->reject();
				});
				click(&dialog, "testFileFilter");
				return result;
			};
			check(tested(QStringLiteral("*.cpp"), QStringLiteral("a.cpp"), false)
					== QStringLiteral("*.cpp -> a.cpp: passed")
				&& tested(QStringLiteral("*.cpp"), QStringLiteral("a.txt"), false)
					== QStringLiteral("*.cpp -> a.txt: failed")
				&& tested(QStringLiteral("*.*;!build/"), QStringLiteral("src/build"), true)
					== QStringLiteral("*.*;!build/ -> src/build: failed")
				&& tested(QStringLiteral("*.*;!build/"), QStringLiteral("src/lib"), true)
					== QStringLiteral("*.*;!build/ -> src/lib: passed"),
				"Test: names pass or fail the mask");
			preset->setCheckState(0, Qt::Checked);
			const QString presetTest = tested(mask->mask(), QStringLiteral(".git"), true);
			preset->setCheckState(0, Qt::Unchecked);
			check(presetTest == QStringLiteral("*.*;!build/;pf:") + sourceControl
					+ QStringLiteral(" -> .git: failed"),
				"Test: the ticked presets count, each named once");
			type(mask, QStringLiteral("*.*"));
			preset = nullptr; // New, Install and Delete make the rows anew

			// New...: a filter from the template, in the user's folder
			chosen = QDir(userDir).filePath(QStringLiteral("mine.txt"));
			click(&dialog, "newFileFilter");
			const QString minePath = QDir(userDir).filePath(QStringLiteral("mine.flt"));
			QFile mineFile(minePath);
			mineFile.open(QIODevice::ReadOnly);
			const QByteArray mineText = mineFile.readAll();
			mineFile.close();
			QTreeWidgetItem *mine = row(QStringLiteral("mine"));
			check(chooserCalls == QStringList{ QStringLiteral("save ") + userDir }
				&& mineText.contains("name: mine\n") && !mineText.contains("${name}")
				&& edited == QStringList{ minePath } && mine != nullptr
				&& list->topLevelItemCount() == 3 && mine->text(2) == minePath
				&& ticked() == QStringList{ QStringLiteral("mine") }
				&& mask->mask() == QStringLiteral("*.*;pf:mine")
				&& !lm::globalFileFilter()->GetFileFilterPath("mine").empty(),
				"New: the template becomes a filter, opened, listed and ticked");

			// Edit...: the editor, and what it saved is read again
			list->setCurrentItem(mine);
			const bool canEdit = usable(&dialog, "editFileFilter")
				&& usable(&dialog, "deleteFileFilter");
			edited.clear();
			click(&dialog, "editFileFilter");
			write(minePath, "name: mine\ndesc: broken\ndef: include\nf: (\n");
			for (int i = 0; i < 200 && row(QStringLiteral("mine")) != nullptr
				&& row(QStringLiteral("mine"))->background(0).style() == Qt::NoBrush; ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
			mine = row(QStringLiteral("mine"));
			printf("  the edited preset: %s\n",
				mine != nullptr ? qPrintable(mine->toolTip(0)) : "(gone)");
			check(canEdit && edited == QStringList{ minePath } && mine != nullptr
				&& mine->background(0).style() != Qt::NoBrush
				&& mine->toolTip(0).startsWith(QCoreApplication::translate("FileFilters",
					"Invalid regular expression"))
				&& row(sourceControl) != nullptr
				&& row(sourceControl)->toolTip(0) == sourceControl
				&& row(sourceControl)->background(0).style() == Qt::NoBrush,
				"Edit: the editor opens, a saved error marks the preset");

			// Install...: a filter file from elsewhere
			const QString elsewhere = dir.filePath(QStringLiteral("downloads/theirs.flt"));
			write(elsewhere, "name: Theirs\ndesc: from elsewhere\ndef: include\nf: \\.tmp$\n");
			chosen = elsewhere;
			chooserCalls.clear();
			click(&dialog, "installFileFilter");
			const QString theirsPath = QDir(userDir).filePath(QStringLiteral("theirs.flt"));
			check(chooserCalls.size() == 1 && chooserCalls.first().startsWith(QStringLiteral("open"))
				&& QFile::exists(theirsPath) && row(QStringLiteral("Theirs")) != nullptr
				&& row(QStringLiteral("Theirs"))->checkState(0) == Qt::Checked
				&& mask->mask() == QStringLiteral("*.*;pf:mine;pf:Theirs")
				&& !lm::globalFileFilter()->GetFileFilterPath("Theirs").empty(),
				"Install: the file is copied to the user's folder, listed and ticked");
			write(elsewhere, "name: Theirs\ndesc: a newer one\ndef: include\nf: \\.tmp$\n");
			asked.clear();
			answer = false;
			click(&dialog, "installFileFilter");
			QFile kept(theirsPath);
			kept.open(QIODevice::ReadOnly);
			const bool keptOld = kept.readAll().contains("from elsewhere");
			kept.close();
			answer = true;
			click(&dialog, "installFileFilter");
			kept.open(QIODevice::ReadOnly);
			const bool tookNew = kept.readAll().contains("a newer one");
			kept.close();
			check(asked == QStringList{ FiltersDialog::tr("Filter file exists. Overwrite?"),
					FiltersDialog::tr("Filter file exists. Overwrite?") }
				&& keptOld && tookNew, "Install: one already there is replaced only on a Yes");

			// Delete...
			list->setCurrentItem(row(QStringLiteral("mine")));
			asked.clear();
			answer = false;
			click(&dialog, "deleteFileFilter");
			const bool keptOnNo = QFile::exists(minePath) && row(QStringLiteral("mine")) != nullptr;
			answer = true;
			click(&dialog, "deleteFileFilter");
			check(asked.size() == 2 && asked.first() == FiltersDialog::tr(
					"Are you sure you want to delete\n\n%1 ?").arg(minePath)
				&& keptOnNo && !QFile::exists(minePath) && row(QStringLiteral("mine")) == nullptr
				&& list->topLevelItemCount() == 3
				&& mask->mask() == QStringLiteral("*.*;pf:Theirs")
				&& ticked() == QStringList{ QStringLiteral("Theirs") }
				&& lm::globalFileFilter()->GetFileFilterPath("mine").empty()
				&& !usable(&dialog, "deleteFileFilter"),
				"Delete: on a Yes the file goes, with its row and its place in the mask");

			// OK: the mask as it stands, into the history; nothing is "*.*"
			dialog.accept();
			check(dialog.result() == QDialog::Accepted
				&& QString::fromStdString(dialog.fileFilter().GetMaskOrExpression())
					== QStringLiteral("*.*;pf:Theirs")
				&& lm::fileFilterHistory().value(0) == QStringLiteral("*.*;pf:Theirs")
				&& lm::fileFilterMask() == QStringLiteral("*.*"),
				"OK: the dialog hands the filter over, the caller puts it in use");
			FiltersDialog empty;
			empty.findChild<FileFilterCombo *>(QStringLiteral("fileFilterMask"))
				->setEditText(QStringLiteral("  "));
			empty.accept();
			check(QString::fromStdString(empty.fileFilter().GetMaskOrExpression())
					== QStringLiteral("*.*"), "OK: an empty mask is *.*");
			QFile::remove(theirsPath);
			lm::loadFilterFiles(lm::globalFileFilter());
		}

		// --- folder comparisons go by the filter ---
		const QString left = dir.filePath(QStringLiteral("L"));
		const QString right = dir.filePath(QStringLiteral("R"));
		for (const QString &root : { left, right })
		{
			const QByteArray side = root == left ? "left\n" : "right\n";
			write(root + QStringLiteral("/a.cpp"), side);
			write(root + QStringLiteral("/b.txt"), side);
			write(root + QStringLiteral("/sub/c.cpp"), side);
			write(root + QStringLiteral("/.git/config"), side);
			write(root + QStringLiteral("/build/out.o"), side);
			write(root + QStringLiteral("/long.dat"), QByteArray(40, root == left ? 'l' : 'r'));
		}
		using Item = lm::FolderCompareItem;
		const auto skippedWith = [&](const QString &maskText) {
			lm::setFileFilterMask(maskText);
			FolderCompareView view;
			view.start(QStringList{ left, right });
			waitFor(&view);
			QStringList skipped;
			for (const char *name : { "a.cpp", "b.txt", "c.cpp", "sub", ".git", "build",
					"long.dat" })
				if (view.rowCategoryForTest(QLatin1String(name)) == Item::Skipped)
					skipped.append(QLatin1String(name));
			if (view.fileFilter() != lm::fileFilterMask())
				skipped.append(QStringLiteral("(pane: ") + view.fileFilter() + QLatin1Char(')'));
			printf("  %s skips: %s\n", qPrintable(maskText),
				skipped.isEmpty() ? "nothing" : qPrintable(skipped.join(QStringLiteral(", "))));
			return skipped;
		};
		check(skippedWith(QStringLiteral("*.*")).isEmpty(), "folder: *.* compares everything");
		check(skippedWith(QStringLiteral("*.cpp"))
				== QStringList{ QStringLiteral("b.txt"), QStringLiteral("long.dat") },
			"folder: a mask leaves the other files out");
		check(skippedWith(QStringLiteral("*.*;!build/")) == QStringList{ QStringLiteral("build") }
			&& skippedWith(QStringLiteral("*.*;!build\\")) == QStringList{ QStringLiteral("build") },
			"folder: a folder mask, with either slash");
		check(skippedWith(QStringLiteral("pf:") + sourceControl)
				== QStringList{ QStringLiteral(".git") },
			"folder: a preset filter leaves the version control folder out");
		check(skippedWith(QStringLiteral("*.*|fe:Size < 20"))
				== QStringList{ QStringLiteral("long.dat") },
			"folder: a filter expression is evaluated");
		lm::setFileFilterMask(QStringLiteral("*.*"));

		// --- the status bar pane and OK in the dialog ---
		{
			MainWindow window;
			window.openFileComparison({ left + QStringLiteral("/a.cpp"),
				right + QStringLiteral("/a.cpp") });
			window.openFolderComparison(QStringList{ left, right });
			settle();
			auto *tabs = window.findChild<QTabWidget *>();
			auto *file = window.findChild<FileCompareView *>();
			auto *folder = window.findChild<FolderCompareView *>();
			if (tabs == nullptr || file == nullptr || folder == nullptr)
				return 2;
			waitFor(folder);
			auto *pane = folder->findChild<QToolButton *>(QStringLiteral("fileFilterPane"));
			check(pane != nullptr && pane->text() == QStringLiteral("*.*")
				&& folder->fileFilter() == QStringLiteral("*.*"),
				"pane: the folder comparison shows its file filter");
			if (pane == nullptr)
				return 1;
			int fileScans = 0;
			QObject::connect(file, &FileCompareView::rescanned, [&fileScans]() { ++fileScans; });

			// the dialog is window-modal: edit it and press a button from
			// inside its event loop
			const auto answering = [&window](const QString &maskText,
				QDialogButtonBox::StandardButton pressed)
			{
				QTimer::singleShot(0, &window, [&window, maskText, pressed]() {
					auto *dialog = window.findChild<FiltersDialog *>();
					auto *buttons = dialog != nullptr
						? dialog->findChild<QDialogButtonBox *>() : nullptr;
					if (buttons == nullptr)
					{
						printf("no Filters dialog to answer\n");
						std::exit(3);
					}
					dialog->showPage(FiltersDialog::FileFiltersPage);
					dialog->findChild<FileFilterCombo *>(QStringLiteral("fileFilterMask"))
						->setEditText(maskText);
					buttons->button(pressed)->click();
				});
			};

			// a click on the pane opens the dialog; a folder comparison in
			// front asks before refreshing
			tabs->setCurrentWidget(folder);
			settle();
			asked.clear();
			answer = false;
			answering(QStringLiteral("*.cpp"), QDialogButtonBox::Ok);
			pane->click();
			const bool startedOnNo = folder->isComparingForTest();
			settle();
			const QString question = QCoreApplication::translate("MessageBoxes",
				"Filters updated. Refresh all open folder compares?\n\n"
				"Select 'No' to refresh later.");
			check(asked == QStringList{ question } && !startedOnNo
				&& lm::fileFilterMask() == QStringLiteral("*.cpp")
				&& QSettings().value(settingsKey).toString() == QStringLiteral("*.cpp")
				&& folder->fileFilter() == QStringLiteral("*.*"),
				"pane click, a new mask, No: saved, the comparison refreshed later");

			asked.clear();
			answer = true;
			answering(QStringLiteral("*.txt"), QDialogButtonBox::Ok);
			pane->click();
			const bool startedOnYes = folder->isComparingForTest();
			waitFor(folder);
			settle();
			check(asked == QStringList{ question } && startedOnYes
				&& folder->fileFilter() == QStringLiteral("*.txt") && pane->text() == QStringLiteral("*.txt")
				&& folder->rowCategoryForTest(QStringLiteral("a.cpp")) == Item::Skipped
				&& folder->rowCategoryForTest(QStringLiteral("b.txt")) == Item::Different,
				"a new mask, Yes: the folder comparison is refreshed with it");

			asked.clear();
			answering(QStringLiteral("*.txt"), QDialogButtonBox::Ok);
			pane->click();
			const bool startedUnchanged = folder->isComparingForTest();
			settle();
			answering(QStringLiteral("*.h"), QDialogButtonBox::Cancel);
			pane->click();
			settle();
			check(asked.isEmpty() && !startedUnchanged
				&& lm::fileFilterMask() == QStringLiteral("*.txt"),
				"the same mask, or Cancel: nothing asked, nothing changed");

			// a text comparison in front: the mask is saved, nothing rescans
			tabs->setCurrentWidget(file);
			settle();
			answering(QStringLiteral("*.*"), QDialogButtonBox::Ok);
			QMetaObject::invokeMethod(&window, "showFilters");
			const bool startedFromText = folder->isComparingForTest();
			settle();
			check(asked.isEmpty() && !startedFromText && fileScans == 0
				&& lm::fileFilterMask() == QStringLiteral("*.*")
				&& folder->fileFilter() == QStringLiteral("*.txt"),
				"text in front: a new mask is only saved");
		}

		// --- the selection screen's "Folder: Filter" field ---
		{
			QSettings().setValue(QStringLiteral("General/VerifyOpenPaths"), true);
			lm::setFileFilterMask(QStringLiteral("*.cpp;*.h"));
			MainWindow window;
			window.openSelector({});
			settle();
			auto *selector = window.findChild<NewComparisonView *>();
			FileFilterCombo *field = selector != nullptr ? selector->filterFieldForTest() : nullptr;
			auto *select = selector != nullptr
				? selector->findChild<QToolButton *>(QStringLiteral("selectFilter")) : nullptr;
			if (field == nullptr || select == nullptr)
				return 2;
			QStringList titles;
			for (const QLabel *label : selector->findChildren<QLabel *>())
				titles.append(label->text());
			check(titles.contains(NewComparisonView::tr("Folder: Filter"))
				&& select->text() == NewComparisonView::tr("Select...")
				&& field->mask() == QStringLiteral("*.cpp;*.h")
				&& field->itemText(0) == QStringLiteral("*.cpp;*.h"),
				"selection: the field shows the filter in use");

			// shown while it may be a folder comparison, usable once it is
			const auto state = [&](const QStringList &paths) {
				MainWindow other;
				other.show();
				other.openSelector(paths);
				auto *screen = other.findChild<NewComparisonView *>();
				screen->verifyPathsForTest();
				FileFilterCombo *combo = screen->filterFieldForTest();
				return QStringLiteral("%1%2").arg(combo->isVisibleTo(screen) ? "shown" : "hidden",
					combo->isEnabled() ? "+usable" : "");
			};
			const QString aFile = left + QStringLiteral("/a.cpp");
			check(state({}) == QStringLiteral("shown")
				&& state({ aFile, right + QStringLiteral("/a.cpp") }) == QStringLiteral("hidden")
				&& state({ left, right }) == QStringLiteral("shown+usable")
				&& state({ left }) == QStringLiteral("shown+usable"),
				"selection: the field follows the kind of the paths");
			QSettings().setValue(QStringLiteral("General/VerifyOpenPaths"), false);
			check(state({ aFile, right + QStringLiteral("/a.cpp") })
					== QStringLiteral("shown+usable"),
				"selection: always there when the paths are not verified");
			QSettings().setValue(QStringLiteral("General/VerifyOpenPaths"), true);

			// what does not parse is marked, with the presets at hand
			type(field, QStringLiteral("pf:No such filter"));
			const int unknownPreset = static_cast<int>(field->errors().size());
			type(field, QStringLiteral("pf:") + sourceControl);
			check(unknownPreset == 1 && field->errors().isEmpty(),
				"selection: the field checks what is typed");

			// Select...: usable with a folder among the paths; the dialog,
			// and its filter back in the field
			const bool selectNeedsFolder = !select->isEnabled();
			selector->addPaths({ left, right });
			selector->verifyPathsForTest();
			const bool selectUsable = select->isEnabled();
			QTimer::singleShot(0, &window, [&window]() {
				auto *dialog = window.findChild<FiltersDialog *>();
				if (dialog == nullptr)
				{
					printf("no Filters dialog to answer\n");
					std::exit(3);
				}
				dialog->showPage(FiltersDialog::FileFiltersPage);
				dialog->findChild<FileFilterCombo *>(QStringLiteral("fileFilterMask"))
					->setEditText(QStringLiteral("*.md"));
				dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
			});
			select->click();
			settle();
			check(selectNeedsFolder && selectUsable && field->mask() == QStringLiteral("*.md")
				&& lm::fileFilterMask() == QStringLiteral("*.md"),
				"selection: Select... opens Filters and takes its filter");

			// Compare: the field's filter is in use from then on
			field->setEditText(QStringLiteral(" *.txt "));
			QPushButton *compare = nullptr;
			for (QPushButton *candidate : selector->findChildren<QPushButton *>())
				if (candidate->text() == NewComparisonView::tr("Compare"))
					compare = candidate;
			if (compare == nullptr)
				return 2;
			compare->click();
			settle();
			auto *folder = window.findChild<FolderCompareView *>();
			waitFor(folder);
			settle();
			check(folder != nullptr && lm::fileFilterMask() == QStringLiteral("*.txt")
				&& QSettings().value(settingsKey).toString() == QStringLiteral("*.txt")
				&& lm::fileFilterHistory().value(0) == QStringLiteral("*.txt")
				&& folder->fileFilter() == QStringLiteral("*.txt")
				&& folder->rowCategoryForTest(QStringLiteral("a.cpp")) == Item::Skipped,
				"selection: Compare puts the field's filter in use");
		}

		lm::setFileFilterMask(QStringLiteral("*.*"));
		lm::setFiltersDirsForTest(QString(), QString());
		lm::setMessageSinkForTest([](const QString &) {});
		lm::setQuestionSinkForTest([](const QString &, bool *) { return false; });
		FiltersDialog::setFileChooserForTest([](bool, const QString &) { return QString(); });
		FiltersDialog::setEditorForTest([](const QString &) {});
		printf("file filters: %s\n", ok ? "ok" : "FAILED");
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestFilterMenuOpt))
	{
		// WinMerge's file filter helper menu (the "=" button, the arrow of
		// "Select..."): its items, the masks and conditions they make, the
		// Filter Condition dialog, and that the engine takes what they make
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
			for (int i = 0; i < 3; ++i)
				QCoreApplication::processEvents();
		};
		using Menu = FileFilterMenu;
		QWidget host;
		Menu menu(&host);
		QStringList chosen;
		int reopened = 0;
		QObject::connect(&menu, &Menu::maskChosen, [&chosen](const QString &mask) {
			chosen.append(mask);
		});
		QObject::connect(&menu, &Menu::reopenRequested, [&reopened]() { ++reopened; });
		const auto made = [&menu](int command, const QString &masks = QStringLiteral("*.*")) {
			return menu.apply(command, masks).value_or(QStringLiteral("(nothing)"));
		};
		const auto label = [](const QAction *action) {
			return action->isSeparator() ? QStringLiteral("-") : action->text();
		};

		// --- the items ---
		{
			QStringList top;
			for (const QAction *action : menu.actions())
				top.append(label(action));
			const auto t = [](const char *text) {
				return QCoreApplication::translate("FileFilterMenu", text);
			};
			check(top == QStringList{ t("&Clear All"), t("Remove Last Filter &Group"),
					t("R&eset to Default (*.*)"), t("Add E&xclude File"), t("Add Excl&ude Folder"),
					QStringLiteral("-"), t("Add &File Condition"), t("Add F&older Condition"),
					t("Target: &Any (Left/Middle/Right)"), t("Target: &Left"),
					t("Target: &Middle"), t("Target: &Right"), QStringLiteral("-"),
					t("Add &Difference Condition"), t("Target: &Left and Right"),
					t("Target: Left and &Middle"), t("Target: Middle and &Right"),
					t("Target: &All") },
				"menu: upstream's items, in its order");
			int leaves = 0;
			for (const QAction *action : menu.findChildren<QAction *>())
				if (action->menu() == nullptr && !action->isSeparator()
					&& action->data().isValid())
					++leaves;
			const int submenus = static_cast<int>(menu.findChildren<QMenu *>().size());
			printf("  %d items in %d submenus\n", leaves, submenus);
			// upstream has 162, two of them "Additional Properties..."
			check(leaves == 160 && submenus == 27, "menu: every item but the Windows properties");
		}

		// --- what the items make of a mask ---
		check(made(Menu::MaskClear).isEmpty() && made(Menu::MaskAll, QStringLiteral("a;b")) == QStringLiteral("*.*")
			&& made(Menu::MaskRemoveLast, QStringLiteral("a|b|c")) == QStringLiteral("a|b")
			&& made(Menu::MaskRemoveLast, QStringLiteral("a")).isEmpty(),
			"mask: clear, reset, remove the last group");
		check(made(Menu::FileBackup, QStringLiteral("*.cpp"))
				== QStringLiteral("*.cpp;!*.bak;!*.old;!*.orig;!*.swp;!*.swo;!*.tmp;!*.temp;!*.save;!*.backup;!*.*~")
			&& made(Menu::FolderVcs, QString()) == QStringLiteral("!.git\\;!.svn\\;!.hg\\")
			&& made(Menu::FileLog) == QStringLiteral("*.*;!*.log;!*.out;!*.err;!*.trace"),
			"mask: ready-made exclusions join the group in use");
		check(made(Menu::SizeFirst) == QStringLiteral("*.*|fe:Size < 1KB")
			&& made(Menu::SizeLast, QString()) == QStringLiteral("fe:Size >= 1GB")
			&& made(Menu::DateFirst + 3) == QStringLiteral("*.*|fe:Date >= today()")
			&& made(Menu::DateLast) == QStringLiteral(
				"*.*|fe:Date >= startOfYear(startOfYear(now()) - 1day)")
			&& made(Menu::AttrFirst + 2) == QStringLiteral("*.*|fe:AttrStr contains \"H\"")
			&& made(Menu::LinesFirst) == QStringLiteral("*.*|fe:lineCount(Content) < 10"),
			"file conditions: a group of their own");
		check(made(Menu::FolderDateFirst) == QStringLiteral("*.*|de:Date < now() - 1hour")
			&& made(Menu::FolderFilesFirst) == QStringLiteral("*.*|de:Files == 0")
			&& made(Menu::FolderItemsLast) == QStringLiteral("*.*|de:Items >= 1")
			&& made(Menu::FolderTotalSizeFirst) == QStringLiteral("*.*|de:TotalSize < 1KB"),
			"folder conditions");
		check(made(Menu::DiffSizeFirst + 1) == QStringLiteral("*.*|fe:LeftSize != RightSize")
			&& made(Menu::DiffSizeLast) == QStringLiteral("*.*|fe:abs(LeftSize - RightSize) >= 1KB")
			&& made(Menu::DiffDateFirst + 6) == QStringLiteral(
				"*.*|fe:abs(LeftDate - RightDate) < 1second")
			&& made(Menu::DiffAttrEqual) == QStringLiteral("*.*|fe:LeftAttrStr = RightAttrStr"),
			"difference conditions");

		// --- the targets and Recursive: ticked, and the menu comes back ---
		menu.pickForTest(Menu::ConditionLeft);
		const bool leftTicked = menu.actionForTest(Menu::ConditionLeft)->isChecked()
			&& !menu.actionForTest(Menu::ConditionAny)->isChecked();
		const QString leftSize = made(Menu::SizeFirst);
		const QString leftFolder = made(Menu::FolderFilesFirst);
		menu.pickForTest(Menu::FolderStatsRecursive);
		const QString recursive = made(Menu::FolderTotalSizeFirst);
		menu.pickForTest(Menu::FolderStatsRecursive);
		menu.pickForTest(Menu::ConditionAny);
		check(leftTicked && reopened == 4 && chosen.isEmpty()
			&& leftSize == QStringLiteral("*.*|fe:LeftSize < 1KB")
			&& leftFolder == QStringLiteral("*.*|de:LeftFiles == 0")
			&& recursive == QStringLiteral("*.*|de:LeftRecursiveTotalSize < 1KB")
			&& made(Menu::FolderTotalSizeFirst) == QStringLiteral("*.*|de:TotalSize < 1KB"),
			"targets: the side goes into the condition, Recursive into the folder's");
		menu.pickForTest(Menu::ConditionDiffLeftMiddle);
		const QString leftMiddle = made(Menu::DiffSizeFirst);
		const bool comparisonsShown = menu.actionForTest(Menu::DiffSizeLess)->isVisible()
			&& menu.actionForTest(Menu::DiffDateRange)->isVisible();
		menu.pickForTest(Menu::ConditionDiffAll);
		const bool onlyEquality = menu.actionForTest(Menu::DiffSizeFirst)->isVisible()
			&& menu.actionForTest(Menu::DiffSizeFirst + 1)->isVisible()
			&& !menu.actionForTest(Menu::DiffSizeLess)->isVisible()
			&& !menu.actionForTest(Menu::DiffSizeRange)->isVisible()
			&& !menu.actionForTest(Menu::DiffDateLess)->isVisible()
			&& !menu.actionForTest(Menu::DiffDateLast)->isVisible()
			&& menu.actionForTest(Menu::DiffAttrNotEqual)->isVisible();
		const QString allSides = made(Menu::DiffSizeFirst) + QStringLiteral(" / ")
			+ made(Menu::DiffDateFirst + 1) + QStringLiteral(" / ") + made(Menu::DiffAttrNotEqual);
		menu.pickForTest(Menu::ConditionDiffLeftRight);
		check(leftMiddle == QStringLiteral("*.*|fe:LeftSize = MiddleSize") && comparisonsShown
			&& onlyEquality && allSides == QStringLiteral("*.*|fe:allequal(Size) / "
				"*.*|fe:not allequal(Date) / *.*|fe:not allequal(AttrStr)"),
			"difference targets: a pair of sides, or all of them alike");

		// --- every item that needs no dialog makes something the engine parses ---
		{
			auto helper = lm::cloneFileFilter();
			QStringList bad;
			int parsed = 0;
			for (int side = 0; side < 4; ++side)
			{
				menu.pickForTest(Menu::ConditionAny + side);
				menu.pickForTest(Menu::ConditionDiffLeftRight + side);
				for (const QAction *action : menu.findChildren<QAction *>())
				{
					if (action->menu() != nullptr || action->isSeparator()
						|| !action->data().isValid() || !action->isVisible())
						continue;
					const int command = action->data().toInt();
					const bool asks = command == Menu::SizeRange || command == Menu::DateRange
						|| command == Menu::LinesRange || command == Menu::FolderDateRange
						|| command == Menu::FolderFilesRange || command == Menu::FolderItemsRange
						|| command == Menu::FolderTotalSizeRange || command == Menu::DiffSizeRange
						|| command == Menu::DiffDateRange
						|| (command >= Menu::ContentFirst && command <= Menu::ContentLast);
					const bool state = command == Menu::FolderStatsRecursive
						|| (command >= Menu::ConditionAny && command <= Menu::ConditionRight)
						|| (command >= Menu::ConditionDiffLeftRight
							&& command <= Menu::ConditionDiffAll);
					if (asks || state)
						continue;
					const QString mask = made(command);
					++parsed;
					const QStringList errors = lm::fileFilterErrors(helper.get(), mask);
					if (!errors.isEmpty())
						bad.append(mask + QStringLiteral(" -> ") + errors.join(QStringLiteral("; ")));
				}
			}
			menu.pickForTest(Menu::ConditionAny);
			menu.pickForTest(Menu::ConditionDiffLeftRight);
			printf("  %d masks parsed\n", parsed);
			for (const QString &line : bad)
				printf("  does not parse: %s\n", qPrintable(line));
			check(bad.isEmpty() && parsed > 500, "engine: every ready-made condition parses");
		}

		// --- the Filter Condition dialog ---
		chosen.clear();
		const auto asking = [&host](std::function<void(FilterConditionDialog *)> body, bool accept)
		{
			QTimer::singleShot(0, &host, [&host, body, accept]() {
				auto *dialog = host.findChild<FilterConditionDialog *>();
				if (dialog == nullptr)
				{
					printf("no Filter Condition dialog\n");
					std::exit(3);
				}
				if (body)
					body(dialog);
				if (accept)
					dialog->accept();
				else
					dialog->reject();
			});
		};
		const auto part = [](FilterConditionDialog *dialog, const char *name) {
			return dialog->findChild<QWidget *>(QLatin1String(name));
		};
		const auto combo = [&part](FilterConditionDialog *dialog, const char *name) {
			return qobject_cast<QComboBox *>(part(dialog, name));
		};
		const auto shownText = [&part](FilterConditionDialog *dialog, const char *name) {
			const auto *text = qobject_cast<QLabel *>(part(dialog, name));
			return text != nullptr ? text->text() : QString();
		};
		const auto pickOperator = [&combo](FilterConditionDialog *dialog, const QString &pattern) {
			QComboBox *operators = combo(dialog, "conditionOperator");
			operators->setCurrentIndex(operators->findData(pattern));
		};
		QString seen;

		asking([&](FilterConditionDialog *dialog) {
			QComboBox *operators = combo(dialog, "conditionOperator");
			QStringList names;
			for (int i = 0; i < operators->count(); ++i)
				names.append(operators->itemText(i));
			seen = shownText(dialog, "conditionLhs") + QStringLiteral(" | ")
				+ operators->currentText() + QStringLiteral(" | ")
				+ QString::number(names.size()) + QStringLiteral(" | ")
				+ shownText(dialog, "conditionExpression") + QStringLiteral(" | second ")
				+ (combo(dialog, "conditionValue2")->isVisibleTo(dialog) ? "shown" : "hidden")
				+ QStringLiteral(" | case ")
				+ (part(dialog, "conditionMatchCase")->isVisibleTo(dialog) ? "shown" : "hidden");
			pickOperator(dialog, QStringLiteral("isWithin(%1, %2, %3)"));
			seen += QStringLiteral(" | second ")
				+ (combo(dialog, "conditionValue2")->isVisibleTo(dialog) ? "shown" : "hidden");
			combo(dialog, "conditionValue1")->setEditText(QStringLiteral("1KB"));
			combo(dialog, "conditionValue2")->setEditText(QStringLiteral("1MB"));
		}, true);
		menu.pickForTest(Menu::SizeRange);
		printf("  size range: %s\n", qPrintable(seen));
		check(seen == QStringLiteral("Size | ") + FilterConditionDialog::tr("Equals")
				+ QStringLiteral(" | 8 | Size = 0B | second hidden | case hidden | second shown")
			&& chosen == QStringList{ QStringLiteral("fe:isWithin(Size, 1KB, 1MB)") },
			"condition: a size, its operators, a range between two values");

		chosen.clear();
		asking([&](FilterConditionDialog *dialog) {
			seen = shownText(dialog, "conditionLhs") + QStringLiteral(" | ")
				+ combo(dialog, "conditionOperator")->currentData().toString()
				+ QStringLiteral(" | ") + QString::number(combo(dialog, "conditionOperator")->count())
				+ QStringLiteral(" | case ")
				+ (part(dialog, "conditionMatchCase")->isVisibleTo(dialog) ? "shown" : "hidden");
			combo(dialog, "conditionValue1")->setEditText(QStringLiteral("say \"hi\""));
			qobject_cast<QCheckBox *>(part(dialog, "conditionMatchCase"))->setChecked(true);
		}, true);
		menu.pickForTest(Menu::ContentFirst + 3);
		check(seen == QStringLiteral("sublines(Content, 0, 1) | %1 not contains %2 | 4 | case shown")
			&& chosen == QStringList{ QStringLiteral(
				"fe:@cs sublines(Content, 0, 1) not contains \"say \"\"hi\"\"\"") },
			"condition: text in the content, quoted, case sensitive on request");

		chosen.clear();
		asking([&](FilterConditionDialog *dialog) {
			seen = shownText(dialog, "conditionLhs") + QStringLiteral(" | ")
				+ combo(dialog, "conditionOperator")->currentData().toString();
			combo(dialog, "conditionValue1")->setEditText(QStringLiteral("100"));
		}, true);
		menu.pickForTest(Menu::LinesRange);
		check(seen == QStringLiteral("lineCount(Content) | %1 > %2")
			&& chosen == QStringList{ QStringLiteral("fe:lineCount(Content) > 100") },
			"condition: a line count");

		chosen.clear();
		asking([&](FilterConditionDialog *dialog) {
			auto *date = qobject_cast<QDateTimeEdit *>(part(dialog, "conditionDate1"));
			seen = QStringLiteral("date ") + (date->isVisibleTo(dialog) ? "shown" : "hidden")
				+ QStringLiteral(" | value ")
				+ (combo(dialog, "conditionValue1")->isVisibleTo(dialog) ? "shown" : "hidden");
			date->setDate(QDate(2026, 1, 15));
			pickOperator(dialog, QStringLiteral("%1 >= %2"));
		}, true);
		menu.pickForTest(Menu::DateRange);
		check(seen == QStringLiteral("date shown | value hidden")
			&& chosen == QStringList{ QStringLiteral("fe:DateStr >= \"2026-01-15\"") },
			"condition: a date, picked");

		chosen.clear();
		menu.pickForTest(Menu::FolderStatsRecursive);
		asking([&](FilterConditionDialog *dialog) {
			combo(dialog, "conditionValue1")->setEditText(QStringLiteral("10"));
			pickOperator(dialog, QStringLiteral("%1 < %2"));
		}, true);
		menu.pickForTest(Menu::FolderFilesRange);
		menu.pickForTest(Menu::FolderStatsRecursive);
		check(chosen == QStringList{ QStringLiteral("de:RecursiveFiles < 10") },
			"condition: a folder's files, subfolders included");

		chosen.clear();
		asking([&](FilterConditionDialog *dialog) {
			QComboBox *values = combo(dialog, "conditionValue1");
			seen = shownText(dialog, "conditionLhs") + QStringLiteral(" | ")
				+ QString::number(combo(dialog, "conditionOperator")->count())
				+ QStringLiteral(" | ") + values->currentText() + QStringLiteral(" | ")
				+ values->itemText(values->count() - 1);
			pickOperator(dialog, QStringLiteral("%1 < %2"));
			values->setEditText(QStringLiteral("1hour"));
		}, true);
		menu.pickForTest(Menu::DiffDateRange);
		check(seen == QStringLiteral("abs(LeftDate - RightDate) | 8 | 0second | 1week")
			&& chosen == QStringList{ QStringLiteral("fe:abs(LeftDate - RightDate) < 1hour") },
			"condition: the time between two dates");

		chosen.clear();
		asking({}, false);
		menu.pickForTest(Menu::SizeRange);
		check(chosen.isEmpty(), "condition: Cancel adds nothing");
		{
			auto helper = lm::cloneFileFilter();
			const QStringList conditions = { QStringLiteral("*.*|fe:isWithin(Size, 1KB, 1MB)"),
				QStringLiteral("*.*|fe:@cs sublines(Content, 0, 1) not contains \"say \"\"hi\"\"\""),
				QStringLiteral("*.*|fe:lineCount(Content) > 100"),
				QStringLiteral("*.*|fe:DateStr >= \"2026-01-15\""),
				QStringLiteral("*.*|de:RecursiveFiles < 10"),
				QStringLiteral("*.*|fe:abs(LeftDate - RightDate) < 1hour") };
			QStringList bad;
			for (const QString &mask : conditions)
				if (!lm::fileFilterErrors(helper.get(), mask).isEmpty())
					bad.append(mask);
			for (const QString &mask : bad)
				printf("  does not parse: %s\n", qPrintable(mask));
			check(bad.isEmpty(), "engine: what the dialog builds parses");
		}

		// --- the engine goes by what the menu makes ---
		const QString left = dir.filePath(QStringLiteral("L"));
		const QString right = dir.filePath(QStringLiteral("R"));
		const QDateTime longAgo(QDate(2020, 5, 1), QTime(12, 0));
		const auto put = [](const QString &path, const QByteArray &bytes,
			const QDateTime &when = QDateTime())
		{
			QDir().mkpath(QFileInfo(path).absolutePath());
			QFile f(path);
			f.open(QIODevice::WriteOnly | QIODevice::Truncate);
			f.write(bytes);
			f.flush();
			if (when.isValid())
				f.setFileTime(when, QFileDevice::FileModificationTime);
		};
		for (const QString &root : { left, right })
		{
			const bool isLeft = root == left;
			put(root + QStringLiteral("/small.txt"), isLeft ? "left\n" : "right\n");
			put(root + QStringLiteral("/big.txt"), QByteArray(5000, isLeft ? 'l' : 'r'));
			put(root + QStringLiteral("/old.txt"), isLeft ? "left\n" : "right\n", longAgo);
			put(root + QStringLiteral("/.hidden"), isLeft ? "left\n" : "right\n");
			put(root + QStringLiteral("/locked.txt"), isLeft ? "left\n" : "right\n");
			QFile::setPermissions(root + QStringLiteral("/locked.txt"),
				QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
			put(root + QStringLiteral("/needle.txt"),
				isLeft ? "a needle here\nleft\n" : "a needle here\nright\n");
			put(root + QStringLiteral("/long.txt"),
				QByteArray(isLeft ? "left line\n" : "right line\n").repeated(40));
			// the same size on both sides, another length on the right
			put(root + QStringLiteral("/grown.txt"), isLeft ? "12345\n" : "123456789\n");
			// the right one last touched long ago
			put(root + QStringLiteral("/stale.txt"), isLeft ? "left\n" : "right\n",
				isLeft ? QDateTime() : longAgo);
			put(root + QStringLiteral("/full/one.txt"), isLeft ? "left\n" : "right\n");
			put(root + QStringLiteral("/heavy/load.txt"), QByteArray(5000, isLeft ? 'l' : 'r'));
			QDir().mkpath(root + QStringLiteral("/empty"));
		}
		using Item = lm::FolderCompareItem;
		const QStringList names = { QStringLiteral("small.txt"), QStringLiteral("big.txt"),
			QStringLiteral("old.txt"), QStringLiteral(".hidden"), QStringLiteral("locked.txt"),
			QStringLiteral("needle.txt"), QStringLiteral("long.txt"), QStringLiteral("grown.txt"),
			QStringLiteral("stale.txt"), QStringLiteral("full"), QStringLiteral("heavy"),
			QStringLiteral("empty") };
		const auto kept = [&](const QString &maskText) {
			lm::setFileFilterMask(maskText);
			FolderCompareView view;
			view.start(QStringList{ left, right });
			for (int i = 0; i < 400 && view.isComparingForTest(); ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
			QStringList compared;
			for (const QString &name : names)
			{
				const int category = view.rowCategoryForTest(name);
				if (category >= 0 && category != Item::Skipped)
					compared.append(name);
			}
			printf("  %s keeps: %s\n", qPrintable(maskText),
				qPrintable(compared.join(QLatin1Char(' '))));
			return compared;
		};
		const auto without = [&names](const QStringList &dropped) {
			QStringList rest = names;
			for (const QString &name : dropped)
				rest.removeAll(name);
			return rest;
		};
		check(kept(QStringLiteral("*.*")) == names, "engine: everything by default");
		check(kept(made(Menu::SizeFirst)) == without({ QStringLiteral("big.txt") }),
			"engine: File Size, less than 1KB");
		check(kept(made(Menu::DateFirst + 3))
				== without({ QStringLiteral("old.txt") }),
			"engine: Last Modified, today");
		check(kept(made(Menu::AttrFirst + 3)) == without({ QStringLiteral(".hidden") })
			&& kept(made(Menu::AttrFirst + 1)) == without({ QStringLiteral("locked.txt") }),
			"engine: Attributes, not hidden and not read-only");
		check(kept(QStringLiteral("*.*|fe:Content contains \"needle\""))
				== QStringList{ QStringLiteral("needle.txt"), QStringLiteral("full"),
					QStringLiteral("heavy"), QStringLiteral("empty") },
			"engine: File Content, contains");
		check(kept(made(Menu::LinesFirst + 1))
				== QStringList{ QStringLiteral("long.txt"), QStringLiteral("full"),
					QStringLiteral("heavy"), QStringLiteral("empty") },
			"engine: Line Count, 10 or more");
		check(kept(made(Menu::FolderFilesLast)) == without({ QStringLiteral("empty") }),
			"engine: a folder's Files, 1 file or more");
		check(kept(made(Menu::FolderTotalSizeFirst + 1))
				== without({ QStringLiteral("full"), QStringLiteral("empty") }),
			"engine: a folder's Total Size, 1KB or more");
		// (the two sides' texts differ in length by a byte; big.txt alone is
		// the same size on both, grown.txt alone differs by more)
		check(kept(made(Menu::DiffSizeFirst + 1)) == without({ QStringLiteral("big.txt") })
			&& kept(made(Menu::DiffSizeFirst + 7))
				== QStringList{ QStringLiteral("long.txt"), QStringLiteral("full"),
					QStringLiteral("heavy"), QStringLiteral("empty") },
			"engine: difference in File Size, not equal, and by 10 bytes or more");
		check(kept(made(Menu::DiffDateFirst + 13))
				== QStringList{ QStringLiteral("stale.txt"), QStringLiteral("full"),
					QStringLiteral("heavy"), QStringLiteral("empty") },
			"engine: difference in Last Modified, 1 day or more");
		check(kept(made(Menu::FolderBuild, made(Menu::FolderVcs))) == names
			&& kept(QStringLiteral("*.*;!full\\;!heavy\\"))
				== without({ QStringLiteral("full"), QStringLiteral("heavy") }),
			"engine: ready-made folder exclusions");
		lm::setFileFilterMask(QStringLiteral("*.*"));
		for (const QString &root : { left, right })
			QFile::setPermissions(root + QStringLiteral("/locked.txt"),
				QFileDevice::ReadOwner | QFileDevice::WriteOwner);

		// --- where the menu lives ---
		{
			FiltersDialog dialog;
			dialog.showPage(FiltersDialog::FileFiltersPage);
			auto *maskButton = dialog.findChild<QToolButton *>(QStringLiteral("fileFilterMaskMenu"));
			auto *pageMenu = dialog.findChild<FileFilterMenu *>();
			auto *mask = dialog.findChild<FileFilterCombo *>(QStringLiteral("fileFilterMask"));
			if (maskButton == nullptr || pageMenu == nullptr || mask == nullptr)
				return 1;
			mask->setEditText(QStringLiteral("*.cpp"));
			pageMenu->pickForTest(Menu::FileBackup);
			const QString afterNames = mask->mask();
			pageMenu->pickForTest(Menu::SizeFirst);
			const QString afterCondition = mask->mask();
			pageMenu->pickForTest(Menu::MaskRemoveLast);
			const QString afterRemove = mask->mask();
			pageMenu->pickForTest(Menu::MaskAll);
			check(maskButton->text() == QStringLiteral("=")
				&& afterNames.startsWith(QStringLiteral("*.cpp;!*.bak;"))
				&& afterCondition == afterNames + QStringLiteral("|fe:Size < 1KB")
				&& afterRemove == afterNames && mask->mask() == QStringLiteral("*.*")
				&& mask->errors().isEmpty(),
				"File Filters page: the \"=\" button's menu changes the mask");

			MainWindow window;
			window.openSelector({ left, right });
			settle();
			auto *selector = window.findChild<NewComparisonView *>();
			auto *select = selector != nullptr
				? selector->findChild<QToolButton *>(QStringLiteral("selectFilter")) : nullptr;
			auto *selectMenu = selector != nullptr
				? selector->findChild<FileFilterMenu *>() : nullptr;
			if (select == nullptr || selectMenu == nullptr)
				return 1;
			selector->verifyPathsForTest();
			FileFilterCombo *field = selector->filterFieldForTest();
			field->setEditText(QStringLiteral("*.h"));
			selectMenu->pickForTest(Menu::FolderVcs);
			check(select->menu() == selectMenu
				&& select->popupMode() == QToolButton::MenuButtonPopup
				&& field->mask() == QStringLiteral("*.h;!.git\\;!.svn\\;!.hg\\"),
				"selection screen: the arrow of Select... opens the same menu");
		}

		printf("filter menu: %s\n", ok ? "ok" : "FAILED");
		return ok ? 0 : 1;
	}

	if (parser.isSet(selftestDisplayFilterOpt))
	{
		// WinMerge's display filter of the folder window: the filter bar,
		// what its filter hides in the tree and in the flat list without
		// comparing again, the header menu's "Filter by This Column", the
		// Filter by Comparison Result dialog, and the engine's items kept
		// in step with copies, deletes and saves
		QTemporaryDir dir;
		if (!dir.isValid())
			return 2;
		bool ok = true;
		const auto check = [&ok](bool condition, const char *what)
		{
			printf("%s: %s\n", what, condition ? "ok" : "FAILED");
			ok = ok && condition;
		};
		const auto write = [](const QString &path, const QByteArray &bytes)
		{
			QDir().mkpath(QFileInfo(path).absolutePath());
			QFile f(path);
			f.open(QIODevice::WriteOnly | QIODevice::Truncate);
			f.write(bytes);
		};
		const auto settle = []()
		{
			for (int i = 0; i < 3; ++i)
				QCoreApplication::processEvents();
			// a closed bar is deleted later
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		};
		const auto waitFor = [](const FolderCompareView *view)
		{
			for (int i = 0; view != nullptr && i < 400 && view->isComparingForTest(); ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
		};
		using Menu = FileFilterMenu;
		const QString shots = qEnvironmentVariable("LIBREMERGE_SELFTEST_SHOTS");
		const QString language = qEnvironmentVariable("LIBREMERGE_LANGUAGE", QStringLiteral("en"));
		const auto shoot = [&shots, &language](QWidget *widget, const char *name)
		{
			if (!shots.isEmpty())
				widget->grab().save(QStringLiteral("%1/df-%2-%3.png").arg(shots, language,
					QLatin1String(name)));
		};

		// --- two folders: files alike and not, in folders with and without
		// folders of their own ---
		const QString left = dir.filePath(QStringLiteral("left"));
		const QString right = dir.filePath(QStringLiteral("right"));
		const auto both = [&](const char *relative, const QByteArray &l, const QByteArray &r)
		{
			write(left + QLatin1Char('/') + QLatin1String(relative), l);
			write(right + QLatin1Char('/') + QLatin1String(relative), r);
		};
		both("same.txt", "same\n", "same\n");
		both("diff.txt", "one\n", "two\n");
		both("big.txt", QByteArray(2000, 'a'), QByteArray(2000, 'b'));
		both("note.md", "note\n", "note\n");
		write(left + QStringLiteral("/onlyL.txt"), "left\n");
		write(right + QStringLiteral("/onlyR.txt"), "right\n");
		both("sub/subsame.txt", "s\n", "s\n");
		both("sub/subdiff.md", "1\n", "2\n");
		both("sub/deep/deepsame.txt", "d\n", "d\n");
		both("build/out.txt", "o\n", "o\n");
		both("lib/liba.txt", "a\n", "a\n");
		both("lib/inner/innerb.txt", "b\n", "b\n");
		const QStringList files{ QStringLiteral("same.txt"), QStringLiteral("diff.txt"),
			QStringLiteral("big.txt"), QStringLiteral("note.md"), QStringLiteral("onlyL.txt"),
			QStringLiteral("onlyR.txt"), QStringLiteral("subsame.txt"),
			QStringLiteral("subdiff.md"), QStringLiteral("deepsame.txt"),
			QStringLiteral("out.txt"), QStringLiteral("liba.txt"), QStringLiteral("innerb.txt") };
		const QStringList folders{ QStringLiteral("sub"), QStringLiteral("deep"),
			QStringLiteral("build"), QStringLiteral("lib"), QStringLiteral("inner") };
		const QStringList every = files + folders;
		const auto shownOf = [](const FolderCompareView &view, const QStringList &names) {
			QStringList shown;
			for (const QString &name : names)
				if (view.rowShownForTest(name))
					shown.append(name);
			return shown;
		};
		const auto without = [](QStringList names, const QStringList &dropped) {
			for (const QString &name : dropped)
				names.removeAll(name);
			return names;
		};
		const auto say = [](const char *what, const QStringList &names) {
			printf("  %s: %s\n", what, qPrintable(names.join(QLatin1Char(' '))));
		};
		// type a filter in the bar and press Apply
		const auto apply = [&settle](FolderCompareView &view, const QString &text)
		{
			view.showDisplayFilterBar();
			DisplayFilterBar *bar = view.displayFilterBarForTest();
			bar->field()->setEditText(text);
			emit bar->field()->lineEdit()->textEdited(text); // as typed
			bar->findChild<QPushButton *>(QStringLiteral("displayFilterApply"))->click();
			settle();
		};
		const auto treeAction = [](FolderCompareView &view) -> QAction * {
			for (QAction *action : view.findChildren<QAction *>())
				if (action->text() == FolderCompareView::tr("Tree View"))
					return action;
			return nullptr;
		};
		const QString historyKey = lm::displayFilterHistoryKey();
		const auto tinted = [](const FileFilterCombo *field, const char *light, const char *dark) {
			const QString style = field->lineEdit()->styleSheet();
			return style.contains(QLatin1String(light)) || style.contains(QLatin1String(dark));
		};

		{
			FolderCompareView view;
			view.resize(900, 520);
			view.start(QStringList{ left, right });
			waitFor(&view);
			settle();
			check(shownOf(view, every) == every && !view.displayFilterBarShown()
				&& view.displayFilter().isEmpty() && view.hiddenRowsForTest() == 0,
				"start: every item listed, no bar and no filter");

			// --- the bar ---
			view.toggleDisplayFilterBar();
			DisplayFilterBar *bar = view.displayFilterBarForTest();
			if (bar == nullptr)
				return 1;
			auto *applyButton = bar->findChild<QPushButton *>(QStringLiteral("displayFilterApply"));
			auto *closeButton = bar->findChild<QPushButton *>(QStringLiteral("displayFilterClose"));
			auto *menuButton = bar->findChild<QToolButton *>(QStringLiteral("displayFilterMaskMenu"));
			if (applyButton == nullptr || closeButton == nullptr || menuButton == nullptr)
				return 1;
			check(view.displayFilterBarShown()
				&& applyButton->text() == DisplayFilterBar::tr("&Apply")
				&& closeButton->text() == DisplayFilterBar::tr("&Close")
				&& menuButton->text() == QStringLiteral("=")
				&& bar->field()->mask().isEmpty() && bar->field()->count() == 0
				&& bar->field()->lineEdit()->placeholderText() == DisplayFilterBar::tr("e.g. %1")
					.arg(QStringLiteral("*.txt|fe:Size > 100KB")),
				"bar: the field, \"=\", Apply and Close, as upstream's");
			view.toggleDisplayFilterBar();
			settle();
			check(!view.displayFilterBarShown()
				&& view.findChild<DisplayFilterBar *>() == nullptr,
				"bar: the View menu item shows it and closes it");
			view.showDisplayFilterBar();
			DisplayFilterBar *first = view.displayFilterBarForTest();
			view.showDisplayFilterBar();
			check(first != nullptr && view.displayFilterBarForTest() == first,
				"bar: the shortcut shows it and never closes it");

			// --- Apply: a mask ---
			apply(view, QStringLiteral("*.txt"));
			bar = view.displayFilterBarForTest();
			FileFilterCombo *field = bar->field();
			say("*.txt shows", shownOf(view, every));
			check(view.displayFilter() == QStringLiteral("*.txt")
				&& shownOf(view, every) == without(every, { QStringLiteral("note.md"),
					QStringLiteral("subdiff.md") })
				&& view.hiddenRowsForTest() == 2 && !view.isComparingForTest(),
				"Apply: a mask hides the other items, with nothing compared again");
			check(field->isApplied() && tinted(field, "#ffffdc", "#3c3c28")
				&& field->mask() == QStringLiteral("*.txt")
				&& lm::fileFilterHistory(historyKey) == QStringList{ QStringLiteral("*.txt") }
				&& lm::fileFilterHistory().isEmpty(),
				"Apply: the field marks its filter as applied and keeps a history of its own");

			// what the user does to the text takes the mark away; a text
			// put there from code does not
			QKeyEvent letter(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
			QCoreApplication::sendEvent(field->lineEdit(), &letter);
			const bool typingClears = !field->isApplied() && !tinted(field, "#ffffdc", "#3c3c28");
			apply(view, QStringLiteral("*.txt"));
			field = view.displayFilterBarForTest()->field();
			const bool appliedAgain = field->isApplied();
			emit field->activated(0);
			const bool pickingClears = !field->isApplied();
			apply(view, QStringLiteral("*.txt"));
			view.showDisplayFilterBar();
			field = view.displayFilterBarForTest()->field();
			check(typingClears && appliedAgain && pickingClears && field->isApplied()
				&& view.displayFilter() == QStringLiteral("*.txt"),
				"field: typing or picking from the list clears the mark, the shortcut does not");

			// --- Enter applies, Esc closes; the filter stays in use ---
			field->setEditText(QStringLiteral("*.md"));
			QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
			QCoreApplication::sendEvent(field->lineEdit(), &enter);
			settle();
			const QStringList markdown = shownOf(view, every);
			say("*.md shows", markdown);
			QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
			QCoreApplication::sendEvent(view.displayFilterBarForTest()->field()->lineEdit(), &escape);
			settle();
			check(markdown == QStringList{ QStringLiteral("note.md"), QStringLiteral("subdiff.md") }
					+ folders
				&& !view.displayFilterBarShown() && view.displayFilter() == QStringLiteral("*.md")
				&& shownOf(view, every) == markdown,
				"keys: Enter applies; Esc closes the bar and the filter stays in use");

			// --- a filter expression; one that does not parse ---
			apply(view, QStringLiteral("fe:Size >= 1KB"));
			const QStringList large = shownOf(view, every);
			say("fe:Size >= 1KB shows", large);
			apply(view, QStringLiteral("fe:Size >="));
			field = view.displayFilterBarForTest()->field();
			say("a broken expression shows", shownOf(view, every));
			printf("  its error: %s\n", qPrintable(field->errors().join(QStringLiteral(" / "))));
			check(large == QStringList{ QStringLiteral("big.txt") } + folders
				&& !field->errors().isEmpty() && !field->isApplied()
				&& tinted(field, "#ffc8c8", "#502828")
				&& view.displayFilter() == QStringLiteral("fe:Size >="),
				"Apply: a filter expression; one that does not parse is marked, not applied");

			// --- an empty field takes the filter away ---
			apply(view, QString());
			field = view.displayFilterBarForTest()->field();
			check(view.displayFilter().isEmpty() && shownOf(view, every) == every
				&& view.hiddenRowsForTest() == 0 && !field->isApplied()
				&& lm::fileFilterHistory(historyKey) == QStringList{ QStringLiteral("fe:Size >="),
					QStringLiteral("fe:Size >= 1KB"), QStringLiteral("*.md"),
					QStringLiteral("*.txt") },
				"Apply: an empty field shows everything again and leaves the history alone");

			// --- what the field shows when the bar comes up ---
			view.toggleDisplayFilterBar(); // closes
			settle();
			view.toggleDisplayFilterBar(); // the menu item: the list's latest entry
			const QString fromMenu = view.displayFilterBarForTest()->field()->mask();
			apply(view, QStringLiteral("*.md"));
			view.displayFilterBarForTest()->field()->setEditText(QStringLiteral("junk"));
			view.showDisplayFilterBar(); // the shortcut: the filter in use
			check(fromMenu == QStringLiteral("fe:Size >=")
				&& view.displayFilterBarForTest()->field()->mask() == QStringLiteral("*.md"),
				"field: the list's latest entry from the menu, the filter in use from the shortcut");

			// --- the "=" menu changes the field; Apply is still to be pressed ---
			bar = view.displayFilterBarForTest();
			field = bar->field();
			field->setEditText(QStringLiteral("*.txt"));
			bar->menuForTest()->pickForTest(Menu::SizeFirst);
			const QString made = field->mask();
			const bool notYet = view.displayFilter() == QStringLiteral("*.md") && !field->isApplied();
			bar->findChild<QPushButton *>(QStringLiteral("displayFilterApply"))->click();
			settle();
			const QStringList smallTexts = shownOf(view, every);
			say("*.txt|fe:Size < 1KB shows", smallTexts);
			check(made == QStringLiteral("*.txt|fe:Size < 1KB") && notYet
				&& view.displayFilter() == made
				&& smallTexts == without(every, { QStringLiteral("big.txt"),
					QStringLiteral("note.md"), QStringLiteral("subdiff.md") }),
				"\"=\": its menu puts a condition in the field, for Apply to take");
			shoot(&view, "bar");

			// --- the filter is the window's: a new comparison keeps it ---
			view.recompare();
			waitFor(&view);
			settle();
			check(view.displayFilter() == made && shownOf(view, every) == smallTexts,
				"Refresh: the filter stays for the comparison that follows");

			// --- the applied filter's tint follows the theme, as does the
			// list it filters ---
			{
				const lm::ThemeMode mode = lm::Theme::instance()->mode();
				const QLineEdit *edit = view.displayFilterBarForTest()->field()->lineEdit();
				lm::Theme::instance()->setMode(lm::ThemeMode::Light);
				settle();
				const bool light = edit->styleSheet().contains(QStringLiteral("#ffffdc"));
				lm::Theme::instance()->setMode(lm::ThemeMode::Dark);
				settle();
				const bool dark = edit->styleSheet().contains(QStringLiteral("#3c3c28"))
					&& shownOf(view, every) == smallTexts;
				lm::Theme::instance()->setMode(mode);
				settle();
				check(light && dark, "theme: the field's tint and the filtered list follow it");
			}

			// --- the tree and the flat list go by different rules for a
			// folder the filter leaves out ---
			QAction *tree = treeAction(view);
			if (tree == nullptr)
				return 1;
			apply(view, QStringLiteral("!build\\"));
			const QStringList buildInTree = shownOf(view, every);
			tree->setChecked(false);
			settle();
			const QStringList buildInList = shownOf(view, every);
			apply(view, QStringLiteral("!lib\\"));
			const QStringList libInList = shownOf(view, every);
			say("!build\\ in the tree shows", buildInTree);
			say("!build\\ in the list shows", buildInList);
			say("!lib\\ in the list shows", libInList);
			check(buildInTree == every
				&& buildInList == without(every, { QStringLiteral("build"), QStringLiteral("out.txt") })
				&& libInList == every,
				"folders: left out in the tree only when empty-handed, in the list unless they hold a folder");
			apply(view, QStringLiteral("*.*|de:Name contains \"deep\""));
			const QStringList deepInList = shownOf(view, every);
			tree->setChecked(true);
			settle();
			const QStringList deepInTree = shownOf(view, every);
			say("de:Name contains \"deep\" in the list shows", deepInList);
			say("de:Name contains \"deep\" in the tree shows", deepInTree);
			check(deepInList == without(every, { QStringLiteral("out.txt"),
					QStringLiteral("liba.txt"), QStringLiteral("innerb.txt"),
					QStringLiteral("build"), QStringLiteral("lib"), QStringLiteral("inner") })
				&& deepInTree == every,
				"folders: a folder condition, by the list's rule and by the tree's");

			// --- with the View menu's filters ---
			apply(view, QStringLiteral("*.txt"));
			view.setShowFilterForTest(FolderCompareView::ShowIdentical, false);
			const QStringList notIdentical = shownOf(view, every);
			view.setShowFilterForTest(FolderCompareView::ShowIdentical, true);
			say("*.txt without the identical shows", notIdentical);
			check(notIdentical == QStringList{ QStringLiteral("diff.txt"), QStringLiteral("big.txt"),
					QStringLiteral("onlyL.txt"), QStringLiteral("onlyR.txt"), QStringLiteral("sub") }
				&& shownOf(view, every) == without(every, { QStringLiteral("note.md"),
					QStringLiteral("subdiff.md") }),
				"View filters: both they and the display filter must let an item through");

			// a row the filter hides leaves the selection: a copy or a
			// delete does not reach what is not on show
			apply(view, QString());
			auto *list = view.findChild<QTreeWidget *>();
			if (list == nullptr)
				return 1;
			const auto selectedNames = [list]() {
				QStringList names;
				for (QTreeWidgetItemIterator it(list); *it != nullptr; ++it)
					if ((*it)->isSelected())
						names.append((*it)->text(0));
				names.sort();
				return names;
			};
			for (QTreeWidgetItemIterator it(list); *it != nullptr; ++it)
				if ((*it)->text(0) == QStringLiteral("note.md")
					|| (*it)->text(0) == QStringLiteral("same.txt")
					|| (*it)->text(0) == QStringLiteral("subdiff.md"))
					(*it)->setSelected(true);
			const QStringList picked = selectedNames();
			apply(view, QStringLiteral("*.txt"));
			check(picked == QStringList{ QStringLiteral("note.md"), QStringLiteral("same.txt"),
					QStringLiteral("subdiff.md") }
				&& selectedNames() == QStringList{ QStringLiteral("same.txt") },
				"selection: a hidden row is no longer selected");
		}

		// --- skipped items: shown when the View menu says so, and then by
		// the display filter alone ---
		{
			lm::setFileFilterMask(QStringLiteral("*.txt"));
			FolderCompareView view;
			view.start(QStringList{ left, right });
			waitFor(&view);
			settle();
			const QStringList byDefault = shownOf(view, files);
			view.setShowFilterForTest(FolderCompareView::ShowSkipped, true);
			const QStringList withSkipped = shownOf(view, files);
			apply(view, QStringLiteral("*.md"));
			const QStringList markdown = shownOf(view, files);
			apply(view, QStringLiteral("*.txt"));
			const QStringList texts = shownOf(view, files);
			say("skipped and *.md shows", markdown);
			check(byDefault == without(files, { QStringLiteral("note.md"), QStringLiteral("subdiff.md") })
				&& withSkipped == files
				&& markdown == QStringList{ QStringLiteral("note.md"), QStringLiteral("subdiff.md") }
				&& texts == byDefault,
				"skipped items: the display filter has the say once they are shown");
			lm::setFileFilterMask(QStringLiteral("*.*"));
		}

		// --- "Filter by This Column" ---
		{
			FolderCompareView view;
			view.resize(900, 520);
			view.start(QStringList{ left, right });
			waitFor(&view);
			settle();
			QStringList columns;
			for (int column = 0; column < 7; ++column)
				columns.append(view.columnRegistryName(column));
			check(columns == QStringList{ QStringLiteral("Name"), QStringLiteral("Path"),
					QStringLiteral("Status"), QStringLiteral("Lsize"), QStringLiteral("Rsize"),
					QStringLiteral("Lmtime"), QStringLiteral("Rmtime") },
				"columns: upstream's names for the ones this list has");
			const auto t = [](const char *text) {
				return QCoreApplication::translate("FileFilterMenu", text);
			};
			// answer the Filter Condition dialog an item opens
			const auto answer = [&view](const QString &value)
			{
				QTimer::singleShot(0, &view, [&view, value]() {
					auto *dialog = view.findChild<FilterConditionDialog *>();
					if (dialog == nullptr)
					{
						printf("no Filter Condition dialog to answer\n");
						std::exit(3);
					}
					dialog->findChild<QComboBox *>(QStringLiteral("conditionValue1"))
						->setEditText(value);
					dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
				});
			};

			// Name: one item that asks; the bar comes up with the filter,
			// applied
			{
				QMenu popup;
				const bool offered = view.buildHeaderMenu(&popup, 0);
				QStringList labels;
				for (const QAction *action : popup.actions())
					labels.append(action->isSeparator() ? QStringLiteral("-") : action->text());
				answer(QStringLiteral("same"));
				if (offered && !popup.actions().isEmpty())
					popup.actions().constLast()->trigger();
				settle();
				DisplayFilterBar *bar = view.displayFilterBarForTest();
				say("Name contains \"same\" shows", shownOf(view, every));
				check(offered && labels == QStringList{ t("&Filter by This Column...") }
					&& view.displayFilter() == QStringLiteral("fe:Name contains \"same\"")
					&& bar != nullptr && bar->field()->mask() == view.displayFilter()
					&& bar->field()->isApplied()
					&& shownOf(view, every) == QStringList{ QStringLiteral("same.txt"),
						QStringLiteral("subsame.txt"), QStringLiteral("deepsame.txt") } + folders,
					"Name: asks for a condition, shows the bar and applies the filter");
			}
			// Folder: the condition joins what the bar's field holds
			{
				view.displayFilterBarForTest()->field()->setEditText(QStringLiteral("*.txt"));
				QMenu popup;
				const bool offered = view.buildHeaderMenu(&popup, 1);
				answer(QStringLiteral("sub"));
				if (offered && !popup.actions().isEmpty())
					popup.actions().constLast()->trigger();
				settle();
				say("*.txt in a folder named sub shows", shownOf(view, files));
				check(view.displayFilter() == QStringLiteral("*.txt|fe:Folder contains \"sub\"")
					&& shownOf(view, files) == QStringList{ QStringLiteral("subsame.txt"),
						QStringLiteral("deepsame.txt") },
					"Folder: the condition joins the filter in the bar's field");
			}
			// Comparison result: its own dialog
			const auto byResult = [&](const QStringList &boxes, bool exclude, bool emptyFirst)
			{
				if (DisplayFilterBar *bar = view.displayFilterBarForTest())
					bar->field()->setEditText(QString());
				QMenu popup;
				if (!view.buildHeaderMenu(&popup, 2) || popup.actions().isEmpty())
					return QStringLiteral("(no menu)");
				QString state;
				QTimer::singleShot(0, &view, [&]() {
					auto *dialog = view.findChild<ComparisonResultFilterDialog *>();
					if (dialog == nullptr)
					{
						printf("no Filter by Comparison Result dialog to answer\n");
						std::exit(3);
					}
					QStringList visible;
					for (const QCheckBox *box : dialog->findChildren<QCheckBox *>())
						if (box->isVisibleTo(dialog))
							visible.append(box->text());
					state = QString::number(visible.size());
					QPushButton *okButton = dialog->findChild<QDialogButtonBox *>()
						->button(QDialogButtonBox::Ok);
					if (emptyFirst)
					{
						// nothing ticked: OK does nothing
						okButton->click();
						state += dialog->isVisible() ? QStringLiteral(" stays") : QStringLiteral(" gone");
					}
					if (exclude)
						dialog->findChild<QRadioButton *>(QStringLiteral("resultExclude"))
							->setChecked(true);
					for (const QString &box : boxes)
						dialog->findChild<QCheckBox *>(box)->setChecked(true);
					shoot(dialog, "result-2way");
					okButton->click();
				});
				popup.actions().constLast()->trigger();
				settle();
				return state;
			};
			{
				const QString state = byResult({ QStringLiteral("resultDifferent"),
					QStringLiteral("resultLeftOnly") }, false, true);
				const QString included = view.displayFilter();
				const QStringList includedFiles = shownOf(view, files);
				byResult({ QStringLiteral("resultIdentical") }, true, false);
				say("not identical shows", shownOf(view, files));
				check(state == QStringLiteral("5 stays")
					&& included == QStringLiteral("fe:(Different) or (LeftExists and not RightExists)")
					&& includedFiles == QStringList{ QStringLiteral("diff.txt"),
						QStringLiteral("big.txt"), QStringLiteral("onlyL.txt"),
						QStringLiteral("subdiff.md") }
					&& view.displayFilter() == QStringLiteral("fe:not (Identical)")
					&& shownOf(view, files) == QStringList{ QStringLiteral("diff.txt"),
						QStringLiteral("big.txt"), QStringLiteral("onlyL.txt"),
						QStringLiteral("onlyR.txt"), QStringLiteral("subdiff.md") },
					"Comparison result: the results to keep or to leave out, as one condition");
			}
			// sizes and dates: a submenu, about the column's own side
			{
				view.displayFilterBarForTest()->field()->setEditText(QString());
				QMenu popup;
				const bool offered = view.buildHeaderMenu(&popup, 3);
				auto *sizes = popup.findChild<FileFilterMenu *>();
				int leaves = 0;
				for (const QAction *action : sizes != nullptr ? sizes->actions() : QList<QAction *>())
					leaves += action->isSeparator() || action->menu() != nullptr ? 0 : 1;
				const QString title = sizes != nullptr ? sizes->title() : QString();
				if (sizes != nullptr)
					sizes->pickForTest(Menu::SizeFirst + 1);
				settle();
				const QString leftSize = view.displayFilter();
				const QStringList leftLarge = shownOf(view, files);

				view.displayFilterBarForTest()->field()->setEditText(QString());
				QMenu rightPopup;
				view.buildHeaderMenu(&rightPopup, 4);
				if (auto *rightSizes = rightPopup.findChild<FileFilterMenu *>())
					rightSizes->pickForTest(Menu::SizeFirst);
				settle();
				const QString rightSize = view.displayFilter();
				say("RightSize < 1KB shows", shownOf(view, files));
				check(offered && title == t("&Filter by This Column") && leaves == 15
					&& popup.actions().size() == 1
					&& leftSize == QStringLiteral("fe:LeftSize >= 1KB")
					&& leftLarge == QStringList{ QStringLiteral("big.txt") }
					&& rightSize == QStringLiteral("fe:RightSize < 1KB")
					&& shownOf(view, files) == without(files, { QStringLiteral("big.txt"),
						QStringLiteral("onlyL.txt") }),
					"sizes: a submenu of conditions on the column's side");

				view.displayFilterBarForTest()->field()->setEditText(QString());
				QMenu datePopup;
				view.buildHeaderMenu(&datePopup, 6);
				auto *dates = datePopup.findChild<FileFilterMenu *>();
				QStringList groups;
				for (const QAction *action : dates != nullptr ? dates->actions() : QList<QAction *>())
					groups.append(action->text());
				if (dates != nullptr)
					dates->pickForTest(Menu::DateFirst + 3);
				settle();
				say("RightDate today shows", shownOf(view, files));
				check(groups == QStringList{ t("&Hour"), t("&Day"), t("&Week"), t("&Month"),
						t("&Year"), t("&Custom Range...") }
					&& view.displayFilter() == QStringLiteral("fe:RightDate >= today()")
					&& shownOf(view, files) == without(files, { QStringLiteral("onlyL.txt") }),
					"dates: the same, with upstream's ranges");
			}
			// with the bar closed, the condition joins the filter in use
			{
				apply(view, QStringLiteral("*.txt"));
				view.toggleDisplayFilterBar();
				settle();
				QMenu popup;
				view.buildHeaderMenu(&popup, 3);
				if (auto *sizes = popup.findChild<FileFilterMenu *>())
					sizes->pickForTest(Menu::SizeFirst + 1);
				settle();
				check(view.displayFilter() == QStringLiteral("*.txt|fe:LeftSize >= 1KB")
					&& view.displayFilterBarShown()
					&& shownOf(view, files) == QStringList{ QStringLiteral("big.txt") },
					"bar closed: the condition joins the filter in use, and the bar comes back");
			}
		}

		// --- the engine's items follow a copy, a delete and a save ---
		{
			FolderCompareView view;
			view.start(QStringList{ left, right });
			waitFor(&view);
			settle();
			const QString leftOnly = QStringLiteral("fe:LeftExists and not RightExists");
			apply(view, leftOnly);
			const QStringList before = shownOf(view, files);
			view.copyRowForTest(QStringLiteral("onlyL.txt"), 0, 1);
			view.applyDisplayFilter();
			settle();
			const QStringList afterCopy = shownOf(view, files);
			apply(view, QStringLiteral("fe:Identical"));
			const bool copyIsIdentical = view.rowShownForTest(QStringLiteral("onlyL.txt"));
			view.deleteRowForTest(QStringLiteral("same.txt"), { 1 });
			apply(view, leftOnly);
			const QStringList afterDelete = shownOf(view, files);
			// a save that left both files alike, and larger
			write(left + QStringLiteral("/diff.txt"), QByteArray(3000, 'z'));
			write(right + QStringLiteral("/diff.txt"), QByteArray(3000, 'z'));
			view.updateSavedItem({ left + QStringLiteral("/diff.txt"),
				right + QStringLiteral("/diff.txt") }, 0);
			apply(view, QStringLiteral("fe:Identical and Size >= 1KB"));
			say("after the save, identical and large", shownOf(view, files));
			check(before == QStringList{ QStringLiteral("onlyL.txt") } && afterCopy.isEmpty()
				&& copyIsIdentical && afterDelete == QStringList{ QStringLiteral("same.txt") }
				&& shownOf(view, files) == QStringList{ QStringLiteral("diff.txt") },
				"operations: a copy, a delete and a save change what the filter finds");

			// what was done to the rows is still there when the list is
			// built again, as a change of theme or of tree mode does
			view.deleteRowForTest(QStringLiteral("note.md"), { 0, 1 });
			apply(view, QString());
			const auto states = [&view]() {
				return QList<int>{ view.rowCategoryForTest(QStringLiteral("onlyL.txt")),
					view.rowCategoryForTest(QStringLiteral("same.txt")),
					view.rowCategoryForTest(QStringLiteral("diff.txt")),
					view.rowCategoryForTest(QStringLiteral("note.md")) };
			};
			const QList<int> done = states();
			if (QAction *tree = treeAction(view))
			{
				tree->setChecked(false);
				settle();
				tree->setChecked(true);
				settle();
			}
			check(done == QList<int>{ lm::FolderCompareItem::Identical,
					lm::FolderCompareItem::LeftOnly, lm::FolderCompareItem::Identical, -1 }
				&& states() == done,
				"operations: the rows keep what was done to them when the list is built again");
		}
		{
			// a folder copied to the other side: in the flat list a folder
			// the filter leaves out takes what is in it along
			write(left + QStringLiteral("/solo/inside.txt"), "i\n");
			FolderCompareView view;
			view.start(QStringList{ left, right });
			waitFor(&view);
			settle();
			if (QAction *tree = treeAction(view))
				tree->setChecked(false);
			const QStringList solo{ QStringLiteral("solo"), QStringLiteral("inside.txt") };
			apply(view, QStringLiteral("de:LeftExists and not RightExists"));
			const QStringList before = shownOf(view, solo);
			view.copyRowForTest(QStringLiteral("solo"), 0, 1);
			view.applyDisplayFilter();
			settle();
			check(before == solo && shownOf(view, solo).isEmpty()
				&& QFileInfo::exists(right + QStringLiteral("/solo/inside.txt")),
				"operations: a copied folder is on both sides for the filter too");
			if (QAction *tree = treeAction(view))
				tree->setChecked(true);
		}

		// --- three folders ---
		{
			const QString roots[3] = { dir.filePath(QStringLiteral("a")),
				dir.filePath(QStringLiteral("b")), dir.filePath(QStringLiteral("c")) };
			const auto three = [&](const char *name, const char *l, const char *m, const char *r)
			{
				const char *texts[3] = { l, m, r };
				for (int i = 0; i < 3; ++i)
					if (texts[i] != nullptr)
						write(roots[i] + QLatin1Char('/') + QLatin1String(name), texts[i]);
			};
			three("same3.txt", "x\n", "x\n", "x\n");
			three("ldiff.txt", "L\n", "x\n", "x\n");
			three("mdiff.txt", "x\n", "M\n", "x\n");
			three("rdiff.txt", "x\n", "x\n", "R\n");
			three("alldiff.txt", "1\n", "2\n", "3\n");
			three("onlyA.txt", "a\n", nullptr, nullptr);
			three("onlyB.txt", nullptr, "b\n", nullptr);
			three("onlyC.txt", nullptr, nullptr, "c\n");
			three("noA.txt", nullptr, "n\n", "n\n");
			three("noB.txt", "n\n", nullptr, "n\n");
			three("noC.txt", "n\n", "n\n", nullptr);
			const QStringList names{ QStringLiteral("same3.txt"), QStringLiteral("ldiff.txt"),
				QStringLiteral("mdiff.txt"), QStringLiteral("rdiff.txt"),
				QStringLiteral("alldiff.txt"), QStringLiteral("onlyA.txt"),
				QStringLiteral("onlyB.txt"), QStringLiteral("onlyC.txt"), QStringLiteral("noA.txt"),
				QStringLiteral("noB.txt"), QStringLiteral("noC.txt") };
			FolderCompareView view;
			view.resize(1000, 520);
			view.start(QStringList{ roots[0], roots[1], roots[2] });
			waitFor(&view);
			settle();
			QStringList columns;
			for (int column = 3; column < 9; ++column)
				columns.append(view.columnRegistryName(column));

			int boxesShown = 0;
			const auto byResult = [&](const char *box)
			{
				if (DisplayFilterBar *bar = view.displayFilterBarForTest())
					bar->field()->setEditText(QString());
				QMenu popup;
				if (!view.buildHeaderMenu(&popup, 2) || popup.actions().isEmpty())
					return QStringList{ QStringLiteral("(no menu)") };
				QTimer::singleShot(0, &view, [&view, &boxesShown, &shoot, box]() {
					auto *dialog = view.findChild<ComparisonResultFilterDialog *>();
					if (dialog == nullptr)
					{
						printf("no Filter by Comparison Result dialog to answer\n");
						std::exit(3);
					}
					boxesShown = 0;
					for (const QCheckBox *candidate : dialog->findChildren<QCheckBox *>())
						boxesShown += candidate->isVisibleTo(dialog) ? 1 : 0;
					dialog->findChild<QCheckBox *>(QLatin1String(box))->setChecked(true);
					shoot(dialog, "result-3way");
					dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
				});
				popup.actions().constLast()->trigger();
				settle();
				return shownOf(view, names);
			};
			const auto one = [](const char *name) { return QStringList{ QLatin1String(name) }; };
			const QStringList identical = byResult("resultIdentical");
			const QStringList different = byResult("resultDifferent");
			say("three folders, identical", identical);
			say("three folders, different", different);
			check(columns == QStringList{ QStringLiteral("Lsize"), QStringLiteral("Msize"),
					QStringLiteral("Rsize"), QStringLiteral("Lmtime"), QStringLiteral("Mmtime"),
					QStringLiteral("Rmtime") }
				&& boxesShown == 12 && identical == one("same3.txt")
				&& different == QStringList{ QStringLiteral("ldiff.txt"),
					QStringLiteral("mdiff.txt"), QStringLiteral("rdiff.txt"),
					QStringLiteral("alldiff.txt") },
				"three folders: the middle columns, and every result in the dialog");
			check(byResult("resultLeftOnly") == one("onlyA.txt")
				&& byResult("resultMiddleOnly") == one("onlyB.txt")
				&& byResult("resultRightOnly") == one("onlyC.txt")
				&& byResult("resultLeftOnlyMissing") == one("noA.txt")
				&& byResult("resultMiddleOnlyMissing") == one("noB.txt")
				&& byResult("resultRightOnlyMissing") == one("noC.txt"),
				"three folders: on one side only, missing on one side only");
			const QStringList leftDiffers = byResult("resultLeftOnlyDifferent");
			const QStringList middleDiffers = byResult("resultMiddleOnlyDifferent");
			const QStringList rightDiffers = byResult("resultRightOnlyDifferent");
			const QString rightCondition = view.displayFilter();
			// what upstream's dialog writes for the right side
			apply(view, QStringLiteral(
				"fe:not DifferentLeftMiddle and DifferentMiddleRight and not DifferentLeftRight"));
			const QStringList upstreams = shownOf(view, names);
			say("only the left differs", leftDiffers);
			say("only the middle differs", middleDiffers);
			say("only the right differs", rightDiffers);
			say("upstream's condition for it finds", upstreams);
			// (the engine marks the odd side out the same way whether its
			// file differs, is the only one or is the one missing: upstream's
			// conditions for the left and the middle find all three)
			check(leftDiffers == QStringList{ QStringLiteral("ldiff.txt"),
					QStringLiteral("onlyA.txt"), QStringLiteral("noA.txt") }
				&& middleDiffers == QStringList{ QStringLiteral("mdiff.txt"),
					QStringLiteral("onlyB.txt"), QStringLiteral("noB.txt") }
				&& rightDiffers == QStringList{ QStringLiteral("rdiff.txt"),
					QStringLiteral("onlyC.txt"), QStringLiteral("noC.txt") }
				&& rightCondition == QStringLiteral(
					"fe:DifferentLeftMiddle and DifferentMiddleRight")
				&& upstreams.isEmpty(),
				"three folders: one side alone different, the right one included");
		}

		// --- the View menu's item ---
		{
			MainWindow window;
			window.resize(1100, 640);
			auto *action = window.findChild<QAction *>(QStringLiteral("displayFilterBarAction"));
			if (action == nullptr)
				return 1;
			const bool idle = !action->isEnabled();
			window.openFolderComparison(left, right);
			settle();
			auto *folder = window.findChild<FolderCompareView *>();
			waitFor(folder);
			settle();
			if (folder == nullptr)
				return 1;
			const bool usable = action->isEnabled() && !action->isChecked();
			action->trigger();
			settle();
			const bool shownAndTicked = folder->displayFilterBarShown() && action->isChecked();
			apply(*folder, QStringLiteral("*.txt"));
			window.show();
			settle();
			shoot(&window, "window");
			action->trigger();
			settle();
			const bool closed = !folder->displayFilterBarShown() && !action->isChecked()
				&& folder->displayFilter() == QStringLiteral("*.txt");
			// with the shortcut's keys down the command only ever shows the
			// bar, the filter in use in its field
			const Qt::KeyboardModifiers chord = Qt::ControlModifier | Qt::ShiftModifier;
			window.displayFilterBarCommand(chord);
			const DisplayFilterBar *byKeys = folder->displayFilterBarForTest();
			window.displayFilterBarCommand(chord);
			const bool keysOnlyShow = byKeys != nullptr
				&& folder->displayFilterBarForTest() == byKeys
				&& byKeys->filterText() == QStringLiteral("*.txt");
			window.displayFilterBarCommand(Qt::NoModifier);
			settle();
			const bool menuCloses = !folder->displayFilterBarShown();
			window.openBlankComparison();
			settle();
			check(action->text() == MainWindow::tr("Displa&y Filter Bar")
				&& action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L)
				&& action->isCheckable() && idle && usable && shownAndTicked && closed
				&& !action->isEnabled(),
				"View menu: Display Filter Bar, for the folder comparison in front");
			check(keysOnlyShow && menuCloses,
				"View menu: the shortcut only shows the bar, the item shows and closes it");
		}

		printf("display filter: %s\n", ok ? "ok" : "FAILED");
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

		// the find bar keeps its own Esc: it closes, the tab stays. That Esc
		// is a shortcut, and shortcuts answer in the active window only: a
		// run whose windows cannot come to the front (another application
		// holds it, on a real desktop) has nothing to check here
		{
			MainWindow window;
			window.show();
			window.openFileComparison({ left, right });
			auto *view = window.findChild<FileCompareView *>();
			view->showFindBar();
			for (int i = 0; i < 40 && !window.isActiveWindow(); ++i)
			{
				QThread::msleep(25);
				QCoreApplication::processEvents();
			}
			QPointer<QLineEdit> findEdit;
			for (QLineEdit *edit : view->findChildren<QLineEdit *>())
				if (edit->isVisible())
					findEdit = edit;
			if (!window.isActiveWindow())
			{
				printf("Esc closes the find bar only: skipped, the window is not active\n");
			}
			else
			{
				const int before = tabCount(window);
				pressEsc(findEdit);
				check(findEdit != nullptr && !findEdit->isVisible()
					&& tabCount(window) == before, "Esc closes the find bar only");
			}
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

		// closing the window offers to save every comparison's unsaved
		// changes, and one Cancel keeps it open (CMainFrame::OnClose)
		{
			const QString mine = dir.filePath(QStringLiteral("mine.txt"));
			const QString theirs = dir.filePath(QStringLiteral("theirs.txt"));
			write(mine, "a\n");
			write(theirs, "b\n");
			const auto read = [](const QString &path)
			{
				QFile f(path);
				f.open(QIODevice::ReadOnly);
				return f.readAll();
			};
			QList<int> answers; // one per prompt: 0 cancels, 1 saves, 2 discards
			int prompts = 0;
			MainWindow::setSavePromptForTest([&]() { return answers.value(prompts++, 0); });
			MainWindow window;
			window.show();
			window.openFileComparison({ mine, theirs });
			window.openFileComparison({ leftCsv, rightCsv });
			window.openFileComparison({ left, right });
			QCoreApplication::processEvents();
			auto *tabs = window.findChild<QTabWidget *>();
			auto *text = qobject_cast<FileCompareView *>(tabs->widget(0));
			auto *table = qobject_cast<TableCompareView *>(tabs->widget(1));
			if (text == nullptr || table == nullptr)
				return 2;
			text->typeAtForTest(0, 0, QStringLiteral("kept "));
			table->gotoFirstDiff();
			table->copyCurrentDiff(0);

			// the text asks first and is saved, the table then cancels
			answers = { 1, 0 };
			const bool closed = window.close();
			check(!closed && window.isVisible() && prompts == 2
				&& read(mine) == "kept a\n" && !text->isModified()
				&& table->isModified() && tabs->currentIndex() == 1,
				"closing the window: each unsaved tab asks, Cancel keeps it open");
			// only the table is left to ask, and it is discarded
			prompts = 0;
			answers = { 2 };
			check(window.close() && !window.isVisible() && prompts == 1
				&& read(rightCsv) == "k,v\n1,b\n",
				"closing the window: Discard lets it close");

			// nothing unsaved: no prompt
			prompts = 0;
			MainWindow plain;
			plain.show();
			plain.openFileComparison({ left, right });
			QCoreApplication::processEvents();
			check(plain.close() && prompts == 0, "closing the window: nothing unsaved, nothing asked");
			MainWindow::setSavePromptForTest([]() { return 2; });
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
