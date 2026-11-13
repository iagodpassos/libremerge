// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "OptionsDialog.h"

#include "BinaryFiles.h"
#include "ColorButton.h"
#include "FileOps.h"
#include "ColorSettings.h"
#include "ImageFormats.h"
#include "EngineOptions.h"
#include "HexDialogs.h"
#include "ItemCheckStyle.h"
#include "MessageBoxes.h"
#include "Settings.h"
#include "SubeditList.h"
#include "TableFiles.h"
#include "Theme.h"
#include "WildcardDropList.h"

#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QFocusEvent>
#include <QFontInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

// engine
#include "ExConverter.h"
#include "OptionsMgr.h"
#include "OptionsDef.h"
#include "OptionsEditorSyntax.h"
#include "OptionsProject.h"
#include "OptionsSyntaxColors.h"
#include "SyntaxColors.h"
#include "TextDefinition.h"
#include "charsets.h"

namespace
{
const QString kScrollFirst = QStringLiteral("General/ScrollToFirstDiff");
const QString kScrollFirstInline =
	QStringLiteral("General/ScrollToFirstInlineDiff");
const QString kShowSelector = QStringLiteral("General/ShowSelectorAtStartup");
const QString kAskClose = QStringLiteral("General/AskCloseMultipleTabs");
// defaults: WinMerge's OptionsInit
const QString kCloseWithEsc = QStringLiteral("General/CloseWithEsc");
const QString kVerifyPaths = QStringLiteral("General/VerifyOpenPaths");
const QString kPreserveFileTime = QStringLiteral("General/PreserveFileTime");
const QString kCloseSelector = QStringLiteral("General/CloseSelectorOnCompare");
const QString kAutoComplete = QStringLiteral("General/AutoCompleteSource");
const QString kAutoReload = QStringLiteral("General/AutoReloadModifiedFiles");
const QString kBackup = QStringLiteral("Backup/FileCompare");
const QString kLanguage = QStringLiteral("Appearance/Language");
// PropEditor's MAX_TABSIZE
const int kMaxTabSize = 64;

/** A label over its control, the way WinMerge's pages stack them; its
    accelerator letter goes to the control. */
void addLabeled(QVBoxLayout *box, const QString &label, QWidget *control)
{
	auto *text = new QLabel(label, control->parentWidget());
	text->setBuddy(control);
	box->addWidget(text);
	box->addWidget(control);
}

/** WinMerge's pattern fields of the Compare pages (WildcardDropList with
    6 buttons to a row), their drop-down offering WinMerge's defaults. */
lm::WildcardDropList *patternList(QWidget *parent, const QString &fixedPatterns)
{
	auto *list = new lm::WildcardDropList(parent);
	list->setColumns(6);
	list->setFixedPatterns(fixedPatterns);
	return list;
}

/** A row's label on the color pages, right-aligned before its buttons
    (RTEXT). */
QLabel *rowLabel(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	return label;
}

/** A column's heading on the color pages (CTEXT). */
QLabel *columnLabel(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	label->setAlignment(Qt::AlignCenter);
	return label;
}

/** A CEColor as the COLORREF the color buttons hold. */
int colorRefOf(const CEColor &color)
{
	return static_cast<int>(static_cast<uint32_t>(color));
}

/** The text colors of the Text page (PropTextColors), the ones "Use
    customized text colors" sets. */
constexpr int kTextColorIndexes[] = { COLORINDEX_WHITESPACE, COLORINDEX_BKGND,
	COLORINDEX_NORMALTEXT, COLORINDEX_SELBKGND, COLORINDEX_SELTEXT, COLORINDEX_SELMARGIN };

/** A field for one character, as narrow as WinMerge's. */
QLineEdit *characterField(QWidget *parent)
{
	auto *field = new QLineEdit(parent);
	field->setFixedWidth(field->fontMetrics().horizontalAdvance(QLatin1Char('M')) * 4);
	return field;
}

QLabel *noteLabel(const QString &text, QWidget *page)
{
	auto *note = new QLabel(text, page);
	note->setWordWrap(true);
	return note;
}
} // namespace

bool OptionsDialog::scrollToFirstDiff()
{
	return lm::setting(kScrollFirst, false).toBool();
}

bool OptionsDialog::scrollToFirstInlineDiff()
{
	return lm::setting(kScrollFirstInline, false).toBool();
}

bool OptionsDialog::showSelectorAtStartup()
{
	return lm::setting(kShowSelector, false).toBool();
}

bool OptionsDialog::askBeforeClosingMultipleTabs()
{
	return lm::setting(kAskClose, false).toBool();
}

OptionsDialog::CloseWithEsc OptionsDialog::closeWithEsc()
{
	const int mode = lm::setting(kCloseWithEsc, EscTabOrMainWindow).toInt();
	return mode >= EscDisabled && mode <= EscMainWindowIfOneTab
		? static_cast<CloseWithEsc>(mode) : EscTabOrMainWindow;
}

bool OptionsDialog::verifyOpenPaths()
{
	return lm::setting(kVerifyPaths, true).toBool();
}

bool OptionsDialog::preserveFileTime()
{
	return lm::setting(kPreserveFileTime, false).toBool();
}

bool OptionsDialog::closeSelectorOnCompare()
{
	return lm::setting(kCloseSelector, false).toBool();
}

OptionsDialog::AutoCompleteSource OptionsDialog::autoCompleteSource()
{
	const int source = lm::setting(kAutoComplete, AutoCompleteFileSystem).toInt();
	return source >= AutoCompleteDisabled && source <= AutoCompleteRecentList
		? static_cast<AutoCompleteSource>(source) : AutoCompleteFileSystem;
}

OptionsDialog::AutoReload OptionsDialog::autoReloadModifiedFiles()
{
	const int mode = lm::setting(kAutoReload, AutoReloadOnWindowActivated).toInt();
	return mode >= AutoReloadDisabled && mode <= AutoReloadImmediately
		? static_cast<AutoReload>(mode) : AutoReloadOnWindowActivated;
}

OptionsDialog::OptionsDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Options"));
#ifndef Q_OS_MACOS
	// window-modal so Wayland/GNOME centers it over the application
	// window; not on macOS, where a Preferences sheet would be unusual
	setWindowModality(Qt::WindowModal);
#endif
	auto *layout = new QVBoxLayout(this);

	auto *body = new QHBoxLayout;
	// WinMerge's page tree (CPreferencesDlg): a category's pages sit
	// indented under it, always expanded
	m_categories = new QTreeWidget(this);
	m_categories->setHeaderHidden(true);
	m_categories->setRootIsDecorated(false);
	m_categories->setItemsExpandable(false);
	m_categories->setIndentation(16);
	m_categories->setFixedWidth(170);
	const auto addCategory = [this](QTreeWidgetItem *parent,
		const QString &text, int page) {
		auto *item = parent != nullptr ? new QTreeWidgetItem(parent)
			: new QTreeWidgetItem(m_categories);
		item->setText(0, text);
		item->setData(0, Qt::UserRole, page);
		return item;
	};
	addCategory(nullptr, tr("General"), GeneralPage);
	QTreeWidgetItem *compare = addCategory(nullptr, tr("Compare"), -1);
	addCategory(compare, tr("General"), ComparePage);
	addCategory(compare, tr("Folder"), FolderPage);
	addCategory(compare, tr("Table"), TablePage);
	addCategory(compare, tr("Binary"), BinaryPage);
	addCategory(compare, tr("Image"), ImagePage);
	addCategory(nullptr, tr("Message Boxes"), MessageBoxesPage);
	QTreeWidgetItem *editor = addCategory(nullptr, tr("Editor"), -1);
	addCategory(editor, tr("General"), EditorPage);
	addCategory(editor, tr("Compare/Merge"), EditorCompareMergePage);
	addCategory(editor, tr("Syntax"), EditorSyntaxPage);
	QTreeWidgetItem *colors = addCategory(nullptr, tr("Colors"), -1);
	addCategory(colors, tr("Color Schemes"), ColorSchemesPage);
	addCategory(colors, tr("Differences"), MergeColorsPage);
	addCategory(colors, tr("Syntax"), SyntaxColorsPage);
	addCategory(colors, tr("Text"), TextColorsPage);
	addCategory(colors, tr("Markers"), MarkerColorsPage);
	addCategory(colors, tr("Folder Compare"), DirColorsPage);
	addCategory(colors, tr("System"), SysColorsPage);
	addCategory(nullptr, tr("Archive Support"), ArchivePage);
	addCategory(nullptr, tr("Project"), ProjectPage);
	addCategory(nullptr, tr("Backup Files"), BackupPage);
	addCategory(nullptr, tr("Codepage"), CodepagePage);
	m_categories->expandAll();
	body->addWidget(m_categories);

	auto *pageArea = new QVBoxLayout;
	m_pages = new QStackedWidget(this);
	m_pages->addWidget(buildGeneralPage());
	m_pages->addWidget(buildComparePage());
	m_pages->addWidget(buildFolderPage());
	m_pages->addWidget(buildTablePage());
	m_pages->addWidget(buildBinaryPage());
	m_pages->addWidget(buildImagePage());
	m_pages->addWidget(buildMessageBoxesPage());
	m_pages->addWidget(buildEditorPage());
	m_pages->addWidget(buildEditorCompareMergePage());
	m_pages->addWidget(buildEditorSyntaxPage());
	m_pages->addWidget(buildColorSchemesPage());
	m_pages->addWidget(buildMergeColorsPage());
	m_pages->addWidget(buildSyntaxColorsPage());
	m_pages->addWidget(buildTextColorsPage());
	m_pages->addWidget(buildMarkerColorsPage());
	m_pages->addWidget(buildDirColorsPage());
	m_pages->addWidget(buildSysColorsPage());
	m_pages->addWidget(buildArchivePage());
	m_pages->addWidget(buildProjectPage());
	m_pages->addWidget(buildBackupPage());
	m_pages->addWidget(buildCodepagePage());
	pageArea->addWidget(m_pages, 1);
	// every WinMerge page has its own Defaults button, resetting only it
	auto *defaultsRow = new QHBoxLayout;
	defaultsRow->addStretch(1);
	m_defaultsButton = new QPushButton(tr("Defaults"), this);
	connect(m_defaultsButton, &QPushButton::clicked, this,
		[this]() { restoreDefaults(m_pages->currentIndex()); });
	defaultsRow->addWidget(m_defaultsButton);
	pageArea->addLayout(defaultsRow);
	body->addLayout(pageArea, 1);
	layout->addLayout(body, 1);

	connect(m_categories, &QTreeWidget::currentItemChanged, this,
		[this](QTreeWidgetItem *item) { selectCategory(item); });
	m_categories->setCurrentItem(m_categories->topLevelItem(0));

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		save();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	m_cancelButton = buttons->button(QDialogButtonBox::Cancel);
	layout->addWidget(buttons);

	load();
	resize(640, 460);
}

/** A category opens its first page, and the title names the page's path,
    as CPreferencesDlg::OnSelchangedPages does. */
void OptionsDialog::selectCategory(QTreeWidgetItem *item)
{
	if (item == nullptr)
		return;
	QTreeWidgetItem *pageItem = item;
	while (pageItem->childCount() > 0)
		pageItem = pageItem->child(0);
	m_pages->setCurrentIndex(pageItem->data(0, Qt::UserRole).toInt());
	// of the Colors pages, only System has a Defaults button upstream
	const int current = m_pages->currentIndex();
	m_defaultsButton->setVisible(current < ColorSchemesPage || current >= SysColorsPage);
	QString path = pageItem->text(0);
	for (QTreeWidgetItem *parent = pageItem->parent(); parent != nullptr;
		parent = parent->parent())
		path = parent->text(0) + QStringLiteral(" > ") + path;
	setWindowTitle(tr("Options (%1)").arg(path));
}

/** WinMerge's General page, in its order. */
QWidget *OptionsDialog::buildGeneralPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	m_chkScrollFirst = new QCheckBox(
		tr("Automatically scroll to first difference"), page);
	box->addWidget(m_chkScrollFirst);
	m_chkScrollFirstInline = new QCheckBox(
		tr("Automatically scroll to first inline difference"), page);
	box->addWidget(m_chkScrollFirstInline);
	// WinMerge's MDI child windows are this window's tabs
	m_cmbCloseWithEsc = new QComboBox(page);
	m_cmbCloseWithEsc->addItems({ tr("Disabled"), tr("Tab or main window"),
		tr("Tab only"), tr("Close main window if only one tab") });
	addLabeled(box, tr("Close windows with 'Esc':"), m_cmbCloseWithEsc);

	box->addSpacing(4);
	m_chkVerifyPaths = new QCheckBox(tr("Automatically verify paths in the "
		"\"Select Files or Folders\" screen"), page);
	box->addWidget(m_chkVerifyPaths);
	box->addSpacing(4);
	m_chkAskClose = new QCheckBox(
		tr("Ask before closing a window with multiple tabs"), page);
	box->addWidget(m_chkAskClose);
	m_chkPreserveFileTime = new QCheckBox(
		tr("Preserve file time in file compare"), page);
	box->addWidget(m_chkPreserveFileTime);
	m_chkShowSelector = new QCheckBox(
		tr("Show \"Select Files or Folders\" screen at startup"), page);
	box->addWidget(m_chkShowSelector);
	m_chkCloseSelector = new QCheckBox(
		tr("Close \"Select Files or Folders\" screen on clicking the Compare button"),
		page);
	box->addWidget(m_chkCloseSelector);

	m_cmbAutoComplete = new QComboBox(page);
	m_cmbAutoComplete->addItems({ tr("Disabled"), tr("From file system"),
		tr("From Most Recently Used list") });
	addLabeled(box, tr("\"Select Files or Folders\" auto completion:"),
		m_cmbAutoComplete);
	m_cmbAutoReload = new QComboBox(page);
	m_cmbAutoReload->setObjectName(QStringLiteral("autoReload"));
	m_cmbAutoReload->addItems({ tr("Disabled"), tr("Only on window activated"),
		tr("Immediately") });
	addLabeled(box, tr("Auto-reload modified files:"), m_cmbAutoReload);

	// the language (LibreMerge's own) closes the page
	box->addSpacing(8);
	m_cmbLanguage = new QComboBox(page);
	m_cmbLanguage->setObjectName(QStringLiteral("language"));
	m_cmbLanguage->addItem(tr("System default"), QString());
	// (each language under its own name, in the alphabet's order)
	m_cmbLanguage->addItem(QString::fromUtf8("Catal\xC3\xA0"), QStringLiteral("ca"));
	m_cmbLanguage->addItem(QStringLiteral("Deutsch"), QStringLiteral("de"));
	m_cmbLanguage->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
	m_cmbLanguage->addItem(QString::fromUtf8("Espa\xC3\xB1ol"), QStringLiteral("es"));
	m_cmbLanguage->addItem(QString::fromUtf8("Fran\xC3\xA7" "ais"), QStringLiteral("fr"));
	m_cmbLanguage->addItem(
		QString::fromUtf8("Portugu\xC3\xAAs (Brasil)"), QStringLiteral("pt_BR"));
	addLabeled(box, tr("Language:"), m_cmbLanguage);
	box->addWidget(noteLabel(
		tr("Language changes take effect after restarting LibreMerge."), page));

	box->addStretch(1);
	return page;
}

/** WinMerge's Compare > General page (PropCompare), in its order and
    with its texts. "Align similar lines" is not offered yet. */
QWidget *OptionsDialog::buildComparePage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	auto *whitespace = new QGroupBox(tr("Whitespaces"), page);
	auto *whitespaceBox = new QVBoxLayout(whitespace);
	const QString whitespaceTexts[3] = { tr("Compare"), tr("Ignore change"),
		tr("Ignore all") };
	for (int i = 0; i < 3; ++i)
	{
		m_radWhitespace[i] = new QRadioButton(whitespaceTexts[i], whitespace);
		whitespaceBox->addWidget(m_radWhitespace[i]);
	}
	box->addWidget(whitespace);

	const auto addCheck = [page, box](const char *name, const QString &text) {
		auto *check = new QCheckBox(text, page);
		check->setObjectName(QLatin1String(name));
		box->addWidget(check);
		return check;
	};
	m_chkIgnoreBlank = addCheck("ignoreBlankLines", tr("Ignore blank lines"));
	m_chkIgnoreCase = addCheck("ignoreCase", tr("Ignore case"));
	m_chkIgnoreEol = addCheck("ignoreEol",
		tr("Ignore EOL differences (Windows/Unix/Mac)"));
	m_chkIgnoreNumbers = addCheck("ignoreNumbers", tr("Ignore numbers"));
	m_chkIgnoreCodepage = addCheck("ignoreCodepage",
		tr("Ignore codepage differences"));
	m_chkFilterComments = addCheck("filterComments",
		tr("Ignore comment differences"));
	m_chkIgnoreMissingEol = addCheck("ignoreMissingTrailingEol",
		tr("Ignore missing trailing EOL"));
	m_chkIgnoreLineBreaks = addCheck("ignoreLineBreaks",
		tr("Ignore line breaks (treat as spaces)"));
	m_chkMovedBlocks = addCheck("movedBlocks", tr("Enable moved block detection"));

	m_cmbAlgorithm = new QComboBox(page);
	m_cmbAlgorithm->setObjectName(QStringLiteral("diffAlgorithm"));
	m_cmbAlgorithm->addItems({ tr("default"), tr("minimal"), tr("patience"),
		tr("histogram"), tr("none") });
	addLabeled(box, tr("Diff algorithm:"), m_cmbAlgorithm);
	m_chkIndentHeuristic = addCheck("indentHeuristic", tr("Enable indent heuristic"));
	// only the xdiff algorithms take the heuristic (UpdateControls)
	const auto updateIndentHeuristic = [this](int algorithm) {
		m_chkIndentHeuristic->setEnabled(algorithm != 0 && algorithm != 4);
	};
	connect(m_cmbAlgorithm, &QComboBox::currentIndexChanged, this,
		updateIndentHeuristic);
	updateIndentHeuristic(m_cmbAlgorithm->currentIndex());
	m_chkBlankOutIgnored = addCheck("blankOutIgnored",
		tr("Completely unhighlight the ignored differences"));

	box->addStretch(1);
	return page;
}

/** WinMerge's Compare > Folder page (PropCompareFolder). */
QWidget *OptionsDialog::buildFolderPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	m_cmbCompareMethod = new QComboBox(page);
	for (int method = 0; method < lm::kCompareMethodCount; ++method)
		m_cmbCompareMethod->addItem(lm::compareMethodName(method));
	addLabeled(box, tr("Compare method:"), m_cmbCompareMethod);

	box->addSpacing(8);
	box->addWidget(noteLabel(tr("Open comparisons pick the new options up on "
		"Recompare (F5) or when reopened."), page));
	box->addWidget(noteLabel(tr("The status bar of a folder comparison "
		"shows the method in use; click it to switch and recompare."), page));

	box->addStretch(1);
	return page;
}

/** WinMerge's Compare > Table page (PropCompareTable): the patterns that
    open a file as a table, by group (CSV, TSV, a delimiter of one's own),
    each pattern list's drop-down ticking the group's defaults on and off,
    and the quote settings of every table. */
QWidget *OptionsDialog::buildTablePage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);
	const lm::TableSettings defaults;

	auto *csv = new QGroupBox(tr("CSV"), page);
	auto *csvLayout = new QVBoxLayout(csv);
	m_cmbCsvPatterns = patternList(csv, defaults.csvFilePatterns);
	addLabeled(csvLayout, tr("File patterns:"), m_cmbCsvPatterns);
	m_editCsvDelimiter = characterField(csv);
	addLabeled(csvLayout, tr("D&elimiter character:"), m_editCsvDelimiter);
	box->addWidget(csv);

	auto *tsv = new QGroupBox(tr("TSV"), page);
	auto *tsvLayout = new QVBoxLayout(tsv);
	m_cmbTsvPatterns = patternList(tsv, defaults.tsvFilePatterns);
	addLabeled(tsvLayout, tr("File patterns:"), m_cmbTsvPatterns);
	box->addWidget(tsv);

	auto *dsv = new QGroupBox(tr("Custom Delimiter-Separated Values"), page);
	auto *dsvLayout = new QVBoxLayout(dsv);
	m_cmbDsvPatterns = patternList(dsv, defaults.dsvFilePatterns);
	addLabeled(dsvLayout, tr("File patterns:"), m_cmbDsvPatterns);
	m_editDsvDelimiter = characterField(dsv);
	addLabeled(dsvLayout, tr("D&elimiter character:"), m_editDsvDelimiter);
	box->addWidget(dsv);

	m_chkAllowNewlines = new QCheckBox(tr("&Allow newlines in quotes"), page);
	box->addWidget(m_chkAllowNewlines);
	m_editQuote = characterField(page);
	addLabeled(box, tr("&Quote character:"), m_editQuote);

	box->addStretch(1);
	return page;
}

/** WinMerge's Compare > Binary page (PropCompareBinary): the binary file
    patterns, whose drop-down ticks WinMerge's defaults on and off, and the
    hex editor's settings, whose dialogs keep what they set at once. */
QWidget *OptionsDialog::buildBinaryPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);
	m_cmbBinaryPatterns = patternList(page, lm::defaultBinaryFilePatterns());
	addLabeled(box, tr("Binary file &patterns:"), m_cmbBinaryPatterns);

	auto *group = new QGroupBox(tr("Hex editor settings"), page);
	auto *groupLayout = new QVBoxLayout(group);
	auto *viewSettings = new QPushButton(tr("View &Settings..."), group);
	auto *binaryMode = new QPushButton(tr("&Binary Mode..."), group);
	auto *characterSet = new QPushButton(tr("&Character Set..."), group);
	connect(viewSettings, &QPushButton::clicked, this, [this]() {
		HexViewSettingsDialog dialog(this);
		HexViewSettingsDialog::run(&dialog);
	});
	connect(binaryMode, &QPushButton::clicked, this, [this]() {
		HexBinaryModeDialog dialog(this);
		HexBinaryModeDialog::run(&dialog);
	});
	connect(characterSet, &QPushButton::clicked, this, [this]() {
		HexCharacterSetDialog dialog(this);
		HexCharacterSetDialog::run(&dialog);
	});
	groupLayout->addWidget(viewSettings);
	groupLayout->addWidget(binaryMode);
	groupLayout->addWidget(characterSet);
	box->addWidget(group);
	box->addStretch(1);
	return page;
}

/** WinMerge's Compare > Image page (PropCompareImage): the image file
    patterns, whose drop-down ticks WinMerge's defaults on and off, and
    image compare in folder compare. Its other rows are left out: "Prefer
    WIC Decoder" picks a Windows decoder (Qt's decode the images here),
    and "OCR result" shapes the text of upstream's command comparing the
    text Windows' OCR reads in the images, a command LibreMerge does not
    have. The drop-down also leaves out the PDF, SVG and Windows metafile
    patterns upstream adds there, kinds the image comparison cannot open. */
QWidget *OptionsDialog::buildImagePage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);
	m_cmbImagePatterns = patternList(page, lm::defaultImageFilePatterns());
	addLabeled(box, tr("Image file &patterns:"), m_cmbImagePatterns);
	m_chkImageInFolderCompare = new QCheckBox(
		tr("&Enable image compare in folder compare"), page);
	box->addWidget(m_chkImageInFolderCompare);
	box->addStretch(1);
	return page;
}

/** WinMerge's Message Boxes page (PropMessageBoxes): the messages a
    "Don't display this message again" can hide, a tick for each hidden
    one with its remembered answer, and Reset to show them all again. */
QWidget *OptionsDialog::buildMessageBoxesPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	auto *intro = new QHBoxLayout;
	intro->addWidget(noteLabel(tr("Some messages can be hidden by the user. "
		"Press Reset to make all messages visible again."), page), 1);
	auto *reset = new QPushButton(tr("Reset"), page);
	connect(reset, &QPushButton::clicked, this, [this]() {
		resetMessageBoxes();
		lm::showInformation(this, tr("All message boxes are now displayed again."));
	});
	intro->addWidget(reset, 0, Qt::AlignTop);
	box->addLayout(intro);

	m_messageList = new QTreeWidget(page);
	lm::ensureItemCheckBoxes(m_messageList);
	m_messageList->setRootIsDecorated(false);
	m_messageList->setHeaderLabels({ tr("Message"), tr("Answer") });
	m_messageList->header()->setStretchLastSection(false);
	m_messageList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	for (const lm::HideableMessage &message : lm::hideableMessages())
	{
		auto *item = new QTreeWidgetItem(m_messageList);
		item->setText(0, QString(message.text).replace(QLatin1Char('\n'), QLatin1Char(' ')));
		item->setData(0, Qt::UserRole, message.key);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(0, Qt::Unchecked);
		// a question keeps one of several answers, picked from a drop-down
		// like WinMerge's Answer cell; the cell stays empty until ticked
		QComboBox *answers = nullptr;
		if (message.answers.size() > 1)
		{
			auto *cell = new QWidget(m_messageList);
			auto *cellLayout = new QHBoxLayout(cell);
			cellLayout->setContentsMargins(0, 0, 0, 0);
			answers = new QComboBox(cell);
			for (const lm::MessageAnswer answer : message.answers)
				answers->addItem(lm::answerText(answer), static_cast<int>(answer));
			answers->setCurrentIndex(-1);
			answers->hide();
			cellLayout->addWidget(answers);
			m_messageList->setItemWidget(item, 1, cell);
		}
		m_messageAnswers.append(answers);
	}
	// a ticked message shows its answer, an unticked one none
	// (OnLVNItemChanged)
	connect(m_messageList, &QTreeWidget::itemChanged, this,
		[this](QTreeWidgetItem *item, int column) {
			if (column == 0)
				syncMessageAnswer(m_messageList->indexOfTopLevelItem(item));
		});
	box->addWidget(m_messageList, 1);
	return page;
}

/** The Answer cell follows the tick: empty when unticked, and the first
    answer ("OK", or a question's "Yes") when ticked with none chosen. */
void OptionsDialog::syncMessageAnswer(int row)
{
	QTreeWidgetItem *item = m_messageList->topLevelItem(row);
	if (item == nullptr)
		return;
	const bool hidden = item->checkState(0) == Qt::Checked;
	if (QComboBox *answers = m_messageAnswers.value(row))
	{
		answers->setVisible(hidden);
		if (!hidden)
			answers->setCurrentIndex(-1);
		else if (answers->currentIndex() < 0)
			answers->setCurrentIndex(0);
	}
	else
	{
		item->setText(1, hidden ? lm::answerText(lm::AnswerOk) : QString());
	}
}

void OptionsDialog::loadMessageBoxes()
{
	for (int i = 0; i < m_messageList->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem *item = m_messageList->topLevelItem(i);
		const lm::MessageAnswer answer =
			lm::rememberedAnswer(item->data(0, Qt::UserRole).toString());
		QComboBox *answers = m_messageAnswers.value(i);
		if (answers != nullptr)
			answers->setCurrentIndex(answers->findData(static_cast<int>(answer)));
		item->setCheckState(0, answer != lm::NoAnswer ? Qt::Checked : Qt::Unchecked);
		syncMessageAnswer(i);
	}
}

/** WinMerge's Editor page (PropEditor): the highlighting and the parsers
    it takes, from the built-in ones and Tree-sitter's, the original EOL
    and the tabs. The rendering mode (GDI or one of DirectWrite's) is
    Windows' own, which upstream turns off where DirectWrite is missing. */
QWidget *OptionsDialog::buildEditorPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	m_chkHiliteSyntax = new QCheckBox(tr("Highlight syntax"), page);
	box->addWidget(m_chkHiliteSyntax);
	m_cmbHiliteSyntaxMode = new QComboBox(page);
	m_cmbHiliteSyntaxMode->addItems({ tr("Built-in only"), tr("Built-in first"),
		tr("Tree-sitter first"), tr("Tree-sitter only") });
	addLabeled(box, tr("Highlight mode:"), m_cmbHiliteSyntaxMode);
	m_chkMixedEol = new QCheckBox(tr("&Preserve original EOL"), page);
	m_chkMixedEol->setObjectName(QStringLiteral("mixedEol"));
	box->addWidget(m_chkMixedEol);

	auto *tabs = new QGroupBox(tr("Tabs"), page);
	auto *tabsLayout = new QVBoxLayout(tabs);
	// digits only, two at most (ES_NUMBER, DDV_MaxChars)
	m_editTabSize = characterField(tabs);
	m_editTabSize->setObjectName(QStringLiteral("tabSize"));
	m_editTabSize->setMaxLength(2);
	m_editTabSize->setAlignment(Qt::AlignRight);
	m_editTabSize->setValidator(new QRegularExpressionValidator(
		QRegularExpression(QStringLiteral("[0-9]*")), m_editTabSize));
	m_editTabSize->installEventFilter(this);
	addLabeled(tabsLayout, tr("&Tab size:"), m_editTabSize);
	m_radInsertTabs = new QRadioButton(tr("&Insert tabs"), tabs);
	m_radInsertTabs->setObjectName(QStringLiteral("insertTabs"));
	tabsLayout->addWidget(m_radInsertTabs);
	m_radInsertSpaces = new QRadioButton(tr("Insert &spaces"), tabs);
	m_radInsertSpaces->setObjectName(QStringLiteral("insertSpaces"));
	tabsLayout->addWidget(m_radInsertSpaces);
	box->addWidget(tabs);

	box->addStretch(1);
	return page;
}

/** WinMerge's Editor > Compare/Merge page (PropEditorCompareMerge): the
    rescan after edits, what a copy with text selected takes in, and how
    the differences inside the lines show. Its box for the merge result
    pane is left out: there is no such pane here. */
QWidget *OptionsDialog::buildEditorCompareMergePage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	m_chkAutomaticRescan = new QCheckBox(tr("&Automatic rescan"), page);
	m_chkAutomaticRescan->setObjectName(QStringLiteral("automaticRescan"));
	box->addWidget(m_chkAutomaticRescan);

	m_cmbCopyGranularity = new QComboBox(page);
	m_cmbCopyGranularity->setObjectName(QStringLiteral("copyGranularity"));
	m_cmbCopyGranularity->addItems({ tr("Diff hunk"), tr("Inline diff"), tr("Line"),
		tr("Character") });
	auto *granularityLabel = new QLabel(tr("Copy &granularity for selected differences:"), page);
	granularityLabel->setBuddy(m_cmbCopyGranularity);
	box->addWidget(granularityLabel);
	box->addWidget(m_cmbCopyGranularity);

	auto *coloring = new QGroupBox(tr("Line Difference Coloring"), page);
	auto *coloringLayout = new QVBoxLayout(coloring);
	// the dialog's steps in: the levels under the check box, the break
	// type under the word level
	const auto indented = [coloringLayout](QWidget *control, int indent) {
		auto *row = new QHBoxLayout;
		row->addSpacing(indent);
		row->addWidget(control, 1);
		coloringLayout->addLayout(row);
	};
	m_chkViewLineDiffs = new QCheckBox(tr("View line differences"), coloring);
	m_chkViewLineDiffs->setObjectName(QStringLiteral("viewLineDiffs"));
	coloringLayout->addWidget(m_chkViewLineDiffs);
	m_radCharLevel = new QRadioButton(tr("&Character-level"), coloring);
	m_radCharLevel->setObjectName(QStringLiteral("charLevel"));
	indented(m_radCharLevel, 16);
	m_radWordLevel = new QRadioButton(tr("&Word-level:"), coloring);
	m_radWordLevel->setObjectName(QStringLiteral("wordLevel"));
	indented(m_radWordLevel, 16);
	m_cmbBreakType = new QComboBox(coloring);
	m_cmbBreakType->setObjectName(QStringLiteral("breakType"));
	m_cmbBreakType->addItems({ tr("Break at whitespace"),
		tr("Break at whitespace or punctuation") });
	indented(m_cmbBreakType, 32);
	m_editBreakChars = new QLineEdit(coloring);
	m_editBreakChars->setObjectName(QStringLiteral("breakChars"));
	auto *breakCharsLabel = new QLabel(tr("W&ord break characters:"), coloring);
	breakCharsLabel->setBuddy(m_editBreakChars);
	indented(breakCharsLabel, 16);
	indented(m_editBreakChars, 16);
	connect(m_chkViewLineDiffs, &QCheckBox::toggled, this,
		&OptionsDialog::updateLineDiffControls);
	box->addWidget(coloring);

	box->addStretch(1);
	return page;
}

/** UpdateLineDiffControls: the level and the break type are only chosen
    with the line differences shown. */
void OptionsDialog::updateLineDiffControls()
{
	const bool on = m_chkViewLineDiffs->isChecked();
	m_radCharLevel->setEnabled(on);
	m_radWordLevel->setEnabled(on);
	m_cmbBreakType->setEnabled(on);
}

/** PropEditor::OnEnKillfocusTabEdit: a tab size out of range is told as
    the field is left for another control, by key, click or accelerator
    (the page saves it within range all the same). Not when the window
    goes to the back or closes. */
/** WinMerge's Editor > Syntax page (PropEditorSyntax): each file type
    with its extensions, in the order of the types (LangServices'
    LanguageId), the extensions edited in a pattern field whose drop-down
    offers the type's own (InitList). */
QWidget *OptionsDialog::buildEditorSyntaxPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);
	m_syntaxList = new lm::SubeditList(page);
	m_syntaxList->setObjectName(QStringLiteral("syntaxList"));
	m_syntaxList->setHeaderLabels({ tr("File Type"), tr("Extension") });
	// the columns 60 and 256 points wide by WinMerge's 9-point dialog
	// font, measured here by the list's: the same pixels at 96 dpi, and
	// wide enough for the type names at the Mac's 72, where points alone
	// cut them
	const qreal perPoint = QFontInfo(m_syntaxList->font()).pixelSize() / 9.0;
	m_syntaxList->setColumnWidth(0, qRound(60 * perPoint));
	m_syntaxList->setColumnWidth(1, qRound(256 * perPoint));
	m_syntaxList->setReadOnlyColumn(0);
	m_syntaxList->setWildcardDropListColumn(1);
	String fixedPattern[LangServices::SRC_MAX_ENTRY];
	Options::EditorSyntax::GetDefaults(GetOptionsMgr(), fixedPattern);
	for (int i = LangServices::SRC_ABAP; i < LangServices::SRC_MAX_ENTRY; ++i)
	{
		const LangServices::TextDefinition *def = LangServices::GetTextType(i);
		if (def == nullptr)
			continue;
		auto *item = new QTreeWidgetItem(m_syntaxList);
		item->setText(0, QString::fromUtf8(def->name));
		m_syntaxList->setDropListFixedPattern(i - 1, 1,
			QString::fromStdString(fixedPattern[i - 1]));
	}
	box->addWidget(m_syntaxList, 1);
	return page;
}

/** WinMerge's Colors > Color Schemes page (PropColorSchemes): the color
    mode, the scheme of each mode, and the user's own schemes saved and
    deleted. A scheme picked, or another mode, is imported at once: the
    colors are in the options from then on, and only OK closes the
    dialog. */
QWidget *OptionsDialog::buildColorSchemesPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);
	m_cmbColorMode = new QComboBox(page);
	m_cmbColorMode->setObjectName(QStringLiteral("colorMode"));
	m_cmbColorMode->addItems({ tr("Light"), tr("Dark"), tr("Follow system") });
	addLabeled(box, tr("Color &mode:"), m_cmbColorMode);
	m_cmbColorScheme = new QComboBox(page);
	m_cmbColorScheme->setObjectName(QStringLiteral("colorScheme"));
	addLabeled(box, tr("&Light mode scheme:"), m_cmbColorScheme);
	m_cmbColorSchemeDark = new QComboBox(page);
	m_cmbColorSchemeDark->setObjectName(QStringLiteral("colorSchemeDark"));
	addLabeled(box, tr("&Dark mode scheme:"), m_cmbColorSchemeDark);

	auto *custom = new QGroupBox(tr("Your custom schemes"), page);
	auto *customLayout = new QVBoxLayout(custom);
	m_btnSaveScheme = new QPushButton(tr("&Save current scheme..."), custom);
	m_btnSaveScheme->setObjectName(QStringLiteral("saveScheme"));
	m_btnDeleteScheme = new QPushButton(tr("Delete &current scheme..."), custom);
	m_btnDeleteScheme->setObjectName(QStringLiteral("deleteScheme"));
	customLayout->addWidget(m_btnSaveScheme);
	customLayout->addWidget(m_btnDeleteScheme);
	box->addWidget(custom);
	box->addStretch(1);

	// OnCbnSelchangeColorMode, OnCbnSelchangeColorScheme
	for (QComboBox *combo : { m_cmbColorMode, m_cmbColorScheme, m_cmbColorSchemeDark })
		connect(combo, &QComboBox::activated, this, [this]() {
			updateColorSchemeControls();
			updateColorScheme();
		});
	connect(m_btnSaveScheme, &QPushButton::clicked, this, &OptionsDialog::saveCurrentScheme);
	connect(m_btnDeleteScheme, &QPushButton::clicked, this, &OptionsDialog::deleteCurrentScheme);
	return page;
}

/** WinMergeDarkMode::GetEffectiveColorMode: dark for Dark, the system's
    for "Follow system". */
bool OptionsDialog::effectiveColorModeDark() const
{
	switch (m_cmbColorMode->currentIndex())
	{
	case 1: return true;
	case 2: return lm::Theme::instance()->systemDark();
	default: return false;
	}
}

/** GetCurrentColorSchemePath: the file of the scheme of the mode in
    effect. */
QString OptionsDialog::currentColorSchemePath() const
{
	return lm::ColorSchemes::path(effectiveColorModeDark()
		? m_cmbColorSchemeDark->currentText() : m_cmbColorScheme->currentText());
}

/** ReloadColorSchemes: every scheme of the folders in both lists. */
void OptionsDialog::reloadColorSchemes()
{
	const QStringList names = lm::ColorSchemes::names();
	for (QComboBox *combo : { m_cmbColorScheme, m_cmbColorSchemeDark })
	{
		combo->clear();
		combo->addItems(names);
	}
}

void OptionsDialog::showColorSchemes(const QString &light, const QString &dark)
{
	m_cmbColorScheme->setCurrentIndex(m_cmbColorScheme->findText(light));
	m_cmbColorSchemeDark->setCurrentIndex(m_cmbColorSchemeDark->findText(dark));
}

/** UpdateControls: each scheme's list where its mode can be in effect,
    Delete for a scheme of the user's own. */
void OptionsDialog::updateColorSchemeControls()
{
	const int mode = m_cmbColorMode->currentIndex();
	m_cmbColorScheme->setEnabled(mode == 0 || mode == 2);
	m_cmbColorSchemeDark->setEnabled(mode == 1 || mode == 2);
	m_btnDeleteScheme->setEnabled(lm::ColorSchemes::isPrivatePath(currentColorSchemePath()));
}

/** The schemes the lists show (WriteOptions); a list with no scheme at
    all leaves its option as it was. */
void OptionsDialog::writeColorSchemeOptions()
{
	if (m_cmbColorScheme->count() > 0)
		lm::setColorSchemeName(false, m_cmbColorScheme->currentText());
	if (m_cmbColorSchemeDark->count() > 0)
		lm::setColorSchemeName(true, m_cmbColorSchemeDark->currentText());
}

/** UpdateColorScheme: the options written with the mode the colors are
    now of (OPT_COLOR_MODE_EFFECTIVE), and that mode's scheme imported;
    then the dialog can only be accepted (OnColorSchemeChanged). The mode
    itself takes effect with OK, which writes the schemes only: a mode
    whose colors were not imported here is then the Theme's to import. */
void OptionsDialog::updateColorScheme()
{
	writeColorSchemeOptions();
	lm::setColorsAreDark(effectiveColorModeDark());
	if (!lm::importColorScheme(currentColorSchemePath()))
	{
		lm::showWarning(this, tr("Failed to import options from file."));
		return;
	}
	m_colorsImported = true;
	m_cancelButton->setEnabled(false);
	// OnColorSchemeChanged: the color pages show the scheme's colors (the
	// other pages keep what they were left with: upstream reads every
	// page again, an unsaved change of theirs lost)
	loadColorPages();
}

/** OnSaveCurrentScheme: the colors in a file of the user's schemes'
    folder, which the lists then show. */
void OptionsDialog::saveCurrentScheme()
{
	const QString folder = lm::ColorSchemes::privateFolder();
	QDir().mkpath(folder);
	QString path = lm::askSaveAsPath(this, tr("Select file for export"), folder,
		tr("Options files (*.ini);;All Files (*)"));
	if (path.isEmpty())
		return;
	if (QFileInfo(path).suffix().isEmpty())
		path += QStringLiteral(".ini");
	const QString light = m_cmbColorScheme->currentText();
	const QString dark = m_cmbColorSchemeDark->currentText();
	lm::saveColorScheme(path);
	reloadColorSchemes();
	showColorSchemes(light, dark);
}

/** OnDeleteCurrentScheme: the scheme in effect, when it is the user's
    own, deleted once confirmed; its mode takes WinMerge's default scheme
    again. */
void OptionsDialog::deleteCurrentScheme()
{
	const QString path = currentColorSchemePath();
	if (!lm::ColorSchemes::isPrivatePath(path))
		return;
	if (!lm::askWarning(this, tr("Are you sure you want to delete\n\n%1 ?")
			.arg(QDir::toNativeSeparators(path))))
		return;
	QFile file(path);
	if (!file.remove())
	{
		lm::showWarning(this, file.errorString());
		return;
	}
	QString light = m_cmbColorScheme->currentText();
	QString dark = m_cmbColorSchemeDark->currentText();
	reloadColorSchemes();
	if (!effectiveColorModeDark())
		light = QStringLiteral("Default");
	else
		dark = QStringLiteral("VS Dark");
	showColorSchemes(light, dark);
	writeColorSchemeOptions();
	updateColorSchemeControls();
	updateColorScheme();
}

OptionsDialog::~OptionsDialog() = default;

/** WinMerge's Colors > Differences page (PropMergeColors): each kind of
    difference with its background, its deleted lines' and its text
    color, the text color shown once ticked (none, the line's own colors,
    unticked). */
QWidget *OptionsDialog::buildMergeColorsPage()
{
	auto *page = new QWidget(this);
	auto *grid = new QGridLayout(page);
	grid->addWidget(columnLabel(tr("Background"), page), 0, 1);
	grid->addWidget(columnLabel(tr("Deleted"), page), 0, 2);
	grid->addWidget(columnLabel(tr("Text"), page), 0, 3, 1, 2);
	struct Row
	{
		const char *label;
		std::string background, deleted, text;
	};
	const Row rows[] = {
		{ QT_TR_NOOP("Difference:"), OPT_CLR_DIFF, OPT_CLR_DIFF_DELETED, OPT_CLR_DIFF_TEXT },
		{ QT_TR_NOOP("Selected Difference:"), OPT_CLR_SELECTED_DIFF,
			OPT_CLR_SELECTED_DIFF_DELETED, OPT_CLR_SELECTED_DIFF_TEXT },
		{ QT_TR_NOOP("Ignored Difference:"), OPT_CLR_TRIVIAL_DIFF,
			OPT_CLR_TRIVIAL_DIFF_DELETED, OPT_CLR_TRIVIAL_DIFF_TEXT },
		{ QT_TR_NOOP("Moved:"), OPT_CLR_MOVEDBLOCK, OPT_CLR_MOVEDBLOCK_DELETED,
			OPT_CLR_MOVEDBLOCK_TEXT },
		{ QT_TR_NOOP("Selected Moved:"), OPT_CLR_SELECTED_MOVEDBLOCK,
			OPT_CLR_SELECTED_MOVEDBLOCK_DELETED, OPT_CLR_SELECTED_MOVEDBLOCK_TEXT },
		{ QT_TR_NOOP("Same As Next (3 panes):"), OPT_CLR_SNP, OPT_CLR_SNP_DELETED, OPT_CLR_SNP_TEXT },
		{ QT_TR_NOOP("Same As Next (Selected):"), OPT_CLR_SELECTED_SNP,
			OPT_CLR_SELECTED_SNP_DELETED, OPT_CLR_SELECTED_SNP_TEXT },
		{ QT_TR_NOOP("Word Difference:"), OPT_CLR_WORDDIFF, OPT_CLR_WORDDIFF_DELETED,
			OPT_CLR_WORDDIFF_TEXT },
		{ QT_TR_NOOP("Selected Word Diff:"), OPT_CLR_SELECTED_WORDDIFF,
			OPT_CLR_SELECTED_WORDDIFF_DELETED, OPT_CLR_SELECTED_WORDDIFF_TEXT },
	};
	int r = 1;
	for (const Row &row : rows)
	{
		grid->addWidget(rowLabel(tr(row.label), page), r, 0);
		const auto add = [this, page, grid, r](const std::string &option, int column) {
			auto *button = new lm::ColorButton(page);
			button->setObjectName(QString::fromStdString(option));
			connect(button, &QPushButton::clicked, this, [this, button]() { browseColor(button); });
			m_mergeColorBindings.append({ option, button });
			grid->addWidget(button, r, column, Qt::AlignCenter);
			return button;
		};
		add(row.background, 1);
		add(row.deleted, 2);
		auto *useText = new QCheckBox(page);
		useText->setObjectName(QString::fromStdString(row.text) + QStringLiteral("/Use"));
		grid->addWidget(useText, r, 3, Qt::AlignRight | Qt::AlignVCenter);
		lm::ColorButton *text = add(row.text, 4);
		QSizePolicy policy = text->sizePolicy();
		policy.setRetainSizeWhenHidden(true);
		text->setSizePolicy(policy);
		m_mergeTextColors.append({ useText, text });
		// OnUseTextColor: black to start with, none when unticked
		connect(useText, &QCheckBox::clicked, this, [this, useText, text]() {
			text->setVisible(useText->isChecked());
			text->setColorRef(useText->isChecked() ? 0 : -1);
			m_colorsChanged = true;
		});
		++r;
	}
	grid->setColumnStretch(5, 1);
	grid->setRowStretch(r, 1);
	return page;
}

/** WinMerge's Colors > Syntax page (PropSyntaxColors): each kind of text
    a language's parser tells, its color and whether it is bold. */
QWidget *OptionsDialog::buildSyntaxColorsPage()
{
	auto *page = new QWidget(this);
	auto *grid = new QGridLayout(page);
	grid->addWidget(columnLabel(tr("Text"), page), 0, 1);
	const std::pair<int, const char *> rows[] = {
		{ COLORINDEX_KEYWORD, QT_TR_NOOP("Keywords:") },
		{ COLORINDEX_FUNCNAME, QT_TR_NOOP("Function names:") },
		{ COLORINDEX_COMMENT, QT_TR_NOOP("Comments:") },
		{ COLORINDEX_NUMBER, QT_TR_NOOP("Numbers:") },
		{ COLORINDEX_OPERATOR, QT_TR_NOOP("Operators:") },
		{ COLORINDEX_STRING, QT_TR_NOOP("Strings:") },
		{ COLORINDEX_PREPROCESSOR, QT_TR_NOOP("Preprocessor:") },
		{ COLORINDEX_USER1, QT_TR_NOOP("User 1:") },
		{ COLORINDEX_USER2, QT_TR_NOOP("User 2:") },
	};
	int r = 1;
	for (const auto &[index, label] : rows)
	{
		grid->addWidget(rowLabel(tr(label), page), r, 0);
		auto *button = new lm::ColorButton(page);
		button->setObjectName(QStringLiteral("syntaxColor%1").arg(index));
		connect(button, &QPushButton::clicked, this,
			[this, colorIndex = index, button]() { browseSyntaxColor(colorIndex, button); });
		grid->addWidget(button, r, 1, Qt::AlignCenter);
		m_syntaxButtons.insert(index, button);
		auto *bold = new QCheckBox(tr("Bold"), page);
		bold->setObjectName(QStringLiteral("syntaxBold%1").arg(index));
		connect(bold, &QCheckBox::clicked, this, [this, colorIndex = index](bool checked) {
			m_syntaxColors->SetBold(colorIndex, checked);
			m_colorsChanged = true;
		});
		grid->addWidget(bold, r, 2);
		m_syntaxBolds.insert(index, bold);
		++r;
	}
	grid->setColumnStretch(3, 1);
	grid->setRowStretch(r, 1);
	return page;
}

/** WinMerge's Colors > Text page (PropTextColors): the editor's text
    colors, the system's unless "Use customized text colors" is ticked
    (OPT_CLR_DEFAULT_TEXT_COLORING off). Ticking or unticking it puts
    them back to their defaults. */
QWidget *OptionsDialog::buildTextColorsPage()
{
	auto *page = new QWidget(this);
	auto *grid = new QGridLayout(page);
	m_chkCustomTextColors = new QCheckBox(tr("&Use customized text colors"), page);
	m_chkCustomTextColors->setObjectName(QStringLiteral("customTextColors"));
	grid->addWidget(m_chkCustomTextColors, 0, 0, 1, 4);
	auto *background = columnLabel(tr("Background"), page);
	auto *text = columnLabel(tr("Text"), page);
	grid->addWidget(background, 1, 1);
	grid->addWidget(text, 1, 2);
	m_textColorLabels = { background, text };
	struct Row
	{
		const char *label;
		int background;
		int text;
	};
	const Row rows[] = {
		{ QT_TR_NOOP("Whitespace:"), COLORINDEX_WHITESPACE, -1 },
		{ QT_TR_NOOP("Regular text:"), COLORINDEX_BKGND, COLORINDEX_NORMALTEXT },
		{ QT_TR_NOOP("Selection:"), COLORINDEX_SELBKGND, COLORINDEX_SELTEXT },
		{ QT_TR_NOOP("Margin:"), COLORINDEX_SELMARGIN, -1 },
	};
	int r = 2;
	for (const Row &row : rows)
	{
		QLabel *label = rowLabel(tr(row.label), page);
		grid->addWidget(label, r, 0);
		m_textColorLabels.append(label);
		for (const auto &[index, column] : { std::pair<int, int>{ row.background, 1 },
				std::pair<int, int>{ row.text, 2 } })
		{
			if (index < 0)
				continue;
			auto *button = new lm::ColorButton(page);
			button->setObjectName(QStringLiteral("textColor%1").arg(index));
			// BrowseColorAndSave: nothing to choose while the system's
			// colors are in use
			connect(button, &QPushButton::clicked, this, [this, colorIndex = index, button]() {
				if (m_chkCustomTextColors->isChecked())
					browseSyntaxColor(colorIndex, button);
			});
			grid->addWidget(button, r, column, Qt::AlignCenter);
			m_textButtons.insert(index, button);
		}
		++r;
	}
	// OnDefaultsStandardColors: the text colors back to their defaults.
	// (Upstream sets every color of the editor to its default there, the
	// Syntax and Markers pages' too, which those pages go on showing as
	// they were.)
	connect(m_chkCustomTextColors, &QCheckBox::clicked, this, [this]() {
		const ::SyntaxColors defaults;
		for (const int index : kTextColorIndexes)
		{
			m_syntaxColors->SetColor(index, defaults.GetColor(index));
			if (lm::ColorButton *button = m_textButtons.value(index))
				button->setColorRef(colorRefOf(defaults.GetColor(index)));
		}
		updateTextColorControls();
		m_colorsChanged = true;
	});
	grid->setColumnStretch(3, 1);
	grid->setRowStretch(r, 1);
	return page;
}

/** WinMerge's Colors > Markers page (PropMarkerColors): the backgrounds
    of the search's marker and of the Marker dialog's three. */
QWidget *OptionsDialog::buildMarkerColorsPage()
{
	auto *page = new QWidget(this);
	auto *grid = new QGridLayout(page);
	grid->addWidget(columnLabel(tr("Background"), page), 0, 1);
	const char *const labels[] = { QT_TR_NOOP("Search Marker:"), QT_TR_NOOP("User Defined Marker 1:"),
		QT_TR_NOOP("User Defined Marker 2:"), QT_TR_NOOP("User Defined Marker 3:") };
	for (int i = 0; i < 4; ++i)
	{
		grid->addWidget(rowLabel(tr(labels[i]), page), i + 1, 0);
		auto *button = new lm::ColorButton(page);
		button->setObjectName(QStringLiteral("markerColor%1").arg(i));
		const int index = COLORINDEX_MARKERBKGND0 + i;
		connect(button, &QPushButton::clicked, this,
			[this, index, button]() { browseSyntaxColor(index, button); });
		grid->addWidget(button, i + 1, 1, Qt::AlignCenter);
		m_markerButtons.insert(index, button);
	}
	grid->setColumnStretch(2, 1);
	grid->setRowStretch(5, 1);
	return page;
}

/** WinMerge's Colors > Folder Compare page (PropDirColors): whether the
    folder comparison colors its items, each result's background and text
    colors, and the list's margin. */
QWidget *OptionsDialog::buildDirColorsPage()
{
	auto *page = new QWidget(this);
	auto *grid = new QGridLayout(page);
	m_chkUseDirColors = new QCheckBox(tr("&Use folder compare colors"), page);
	m_chkUseDirColors->setObjectName(QStringLiteral("useDirColors"));
	connect(m_chkUseDirColors, &QCheckBox::clicked, this, [this]() { m_colorsChanged = true; });
	grid->addWidget(m_chkUseDirColors, 0, 0, 1, 4);
	grid->addWidget(columnLabel(tr("Background"), page), 1, 1);
	grid->addWidget(columnLabel(tr("Text"), page), 1, 2);
	struct Row
	{
		const char *label;
		std::string background, text;
	};
	const Row rows[] = {
		{ QT_TR_NOOP("Items equal:"), OPT_DIRCLR_ITEM_EQUAL, OPT_DIRCLR_ITEM_EQUAL_TEXT },
		{ QT_TR_NOOP("Items different:"), OPT_DIRCLR_ITEM_DIFF, OPT_DIRCLR_ITEM_DIFF_TEXT },
		{ QT_TR_NOOP("Items missing:"), OPT_DIRCLR_ITEM_NOT_EXIST_ALL,
			OPT_DIRCLR_ITEM_NOT_EXIST_ALL_TEXT },
		{ QT_TR_NOOP("Items filtered:"), OPT_DIRCLR_ITEM_FILTERED, OPT_DIRCLR_ITEM_FILTERED_TEXT },
		{ QT_TR_NOOP("Margin:"), OPT_DIRCLR_MARGIN, std::string() },
	};
	int r = 2;
	for (const Row &row : rows)
	{
		grid->addWidget(rowLabel(tr(row.label), page), r, 0);
		for (const auto &[option, column] : { std::pair<std::string, int>{ row.background, 1 },
				std::pair<std::string, int>{ row.text, 2 } })
		{
			if (option.empty())
				continue;
			auto *button = new lm::ColorButton(page);
			button->setObjectName(QString::fromStdString(option));
			connect(button, &QPushButton::clicked, this, [this, button]() { browseColor(button); });
			grid->addWidget(button, r, column, Qt::AlignCenter);
			m_dirColorBindings.append({ option, button });
		}
		++r;
	}
	grid->setColumnStretch(3, 1);
	grid->setRowStretch(r, 1);
	return page;
}

/** WinMerge's Colors > System page (PropSysColors): the system colors of
    the colors in effect, switched on and chosen one by one. */
QWidget *OptionsDialog::buildSysColorsPage()
{
	auto *page = new QWidget(this);
	auto *grid = new QGridLayout(page);
	m_chkSysColorHook = new QCheckBox(tr("&Use custom system colors"), page);
	m_chkSysColorHook->setObjectName(QStringLiteral("sysColorHook"));
	grid->addWidget(m_chkSysColorHook, 0, 0, 1, 3);
	m_cmbSysColorName = new QComboBox(page);
	m_cmbSysColorName->setObjectName(QStringLiteral("sysColorName"));
	const std::pair<const char *, int> names[] = {
		{ "COLOR_ACTIVECAPTION", 2 }, { "COLOR_INACTIVECAPTION", 3 }, { "COLOR_WINDOW", 5 },
		{ "COLOR_WINDOWTEXT", 8 }, { "COLOR_CAPTIONTEXT", 9 }, { "COLOR_APPWORKSPACE", 12 },
		{ "COLOR_HIGHLIGHT", 13 }, { "COLOR_HIGHLIGHTTEXT", 14 }, { "COLOR_3DFACE", 15 },
		{ "COLOR_3DSHADOW", 16 }, { "COLOR_BTNTEXT", 18 }, { "COLOR_INACTIVECAPTIONTEXT", 19 },
		{ "COLOR_3DHIGHLIGHT", 20 }, { "COLOR_3DDKSHADOW", 21 },
		{ "COLOR_GRADIENTACTIVECAPTION", 27 }, { "COLOR_GRADIENTINACTIVECAPTION", 28 },
	};
	for (const auto &[name, index] : names)
		m_cmbSysColorName->addItem(QString::fromLatin1(name), index);
	grid->addWidget(m_cmbSysColorName, 1, 0);
	m_btnSysColor = new lm::ColorButton(page);
	m_btnSysColor->setObjectName(QStringLiteral("sysColor"));
	grid->addWidget(m_btnSysColor, 1, 1, Qt::AlignCenter);
	connect(m_chkSysColorHook, &QCheckBox::clicked, this, [this]() {
		updateSysColorControls();
		m_colorsChanged = true;
	});
	connect(m_cmbSysColorName, &QComboBox::currentIndexChanged, this,
		[this]() { updateSysColorControls(); });
	// BrowseColor: the dialog without the options' custom colors
	connect(m_btnSysColor, &QPushButton::clicked, this, [this]() {
		const int index = m_cmbSysColorName->currentData().toInt();
		const QColor color = lm::chooseColor(this,
			lm::colorFromColorRef(m_sysColors[static_cast<size_t>(index)]), false);
		if (!color.isValid())
			return;
		m_sysColors[static_cast<size_t>(index)] = lm::colorRefFromColor(color);
		m_customSysColors.insert(index);
		m_btnSysColor->setColorRef(m_sysColors[static_cast<size_t>(index)]);
		m_colorsChanged = true;
	});
	grid->setColumnStretch(2, 1);
	grid->setRowStretch(2, 1);
	return page;
}

/** The color pages' ReadOptions, from the options and a copy of the
    editor's colors (also as a scheme is imported, OnColorSchemeChanged). */
void OptionsDialog::loadColorPages()
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr)
		return;
	if (!m_syntaxColors)
		m_syntaxColors = std::make_unique<::SyntaxColors>();
	Options::SyntaxColors::Load(mgr, m_syntaxColors.get());
	for (const ColorBinding &binding : m_mergeColorBindings)
		binding.button->setColorRef(mgr->GetInt(binding.option));
	updateTextColorButtons();
	for (auto it = m_syntaxButtons.cbegin(); it != m_syntaxButtons.cend(); ++it)
	{
		it.value()->setColorRef(colorRefOf(m_syntaxColors->GetColor(it.key())));
		m_syntaxBolds.value(it.key())->setChecked(m_syntaxColors->GetBold(it.key()));
	}
	m_chkCustomTextColors->setChecked(!mgr->GetBool(OPT_CLR_DEFAULT_TEXT_COLORING));
	for (auto it = m_textButtons.cbegin(); it != m_textButtons.cend(); ++it)
		it.value()->setColorRef(colorRefOf(m_syntaxColors->GetColor(it.key())));
	updateTextColorControls();
	for (auto it = m_markerButtons.cbegin(); it != m_markerButtons.cend(); ++it)
		it.value()->setColorRef(colorRefOf(m_syntaxColors->GetColor(it.key())));
	m_chkUseDirColors->setChecked(mgr->GetBool(OPT_DIRCLR_USE_COLORS));
	for (const ColorBinding &binding : m_dirColorBindings)
		binding.button->setColorRef(mgr->GetInt(binding.option));
	// PropSysColors::ReadOptions: the system's colors, the scheme's own
	// where it has them
	m_chkSysColorHook->setChecked(mgr->GetBool(OPT_SYSCOLOR_HOOK_ENABLED));
	for (size_t i = 0; i < m_sysColors.size(); ++i)
		m_sysColors[i] = lm::colorRefFromColor(lm::originalSystemColor(static_cast<int>(i)));
	m_customSysColors.clear();
	for (const auto &[index, color] : lm::systemColorRefs(
			QString::fromStdString(mgr->GetString(OPT_SYSCOLOR_HOOK_COLORS))))
		if (index >= 0 && index < static_cast<int>(m_sysColors.size()))
		{
			m_sysColors[static_cast<size_t>(index)] = color;
			m_customSysColors.insert(index);
		}
	updateSysColorControls();
}

/** The color pages' WriteOptions; the editor's colors then are the copy's
    (CPreferencesDlg::SaveOptions clones it). */
void OptionsDialog::saveColorPages()
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr || !m_syntaxColors)
		return;
	for (const ColorBinding &binding : m_mergeColorBindings)
		mgr->SaveOption(binding.option, binding.button->colorRef());
	mgr->SaveOption(OPT_CLR_DEFAULT_TEXT_COLORING, !m_chkCustomTextColors->isChecked());
	Options::SyntaxColors::Save(mgr, m_syntaxColors.get());
	mgr->SaveOption(OPT_DIRCLR_USE_COLORS, m_chkUseDirColors->isChecked());
	for (const ColorBinding &binding : m_dirColorBindings)
		mgr->SaveOption(binding.option, binding.button->colorRef());
	// SysColorHook::Serialize: the colors set apart from the system's
	QStringList colors;
	for (size_t i = 0; i < m_sysColors.size(); ++i)
	{
		const int index = static_cast<int>(i);
		if (m_customSysColors.contains(index)
			|| m_sysColors[i] != lm::colorRefFromColor(lm::originalSystemColor(index)))
			colors.append(QStringLiteral("%1:0x%2").arg(index)
				.arg(static_cast<uint>(m_sysColors[i]), 8, 16, QLatin1Char('0')));
	}
	mgr->SaveOption(OPT_SYSCOLOR_HOOK_ENABLED, m_chkSysColorHook->isChecked());
	mgr->SaveOption(OPT_SYSCOLOR_HOOK_COLORS, colors.join(QLatin1Char(',')).toStdString());
	lm::reloadColors();
}

/** BrowseColor: the dialog on the button's color, the button the color
    chosen. */
void OptionsDialog::browseColor(lm::ColorButton *button)
{
	const QColor color = lm::chooseColor(this, lm::colorFromColorRef(button->colorRef()));
	if (!color.isValid())
		return;
	button->setColorRef(lm::colorRefFromColor(color));
	m_colorsChanged = true;
}

/** BrowseColorAndSave: the same for a color of the editor's, which the
    copy keeps until OK. */
void OptionsDialog::browseSyntaxColor(int colorIndex, lm::ColorButton *button)
{
	const QColor color = lm::chooseColor(this,
		lm::colorFromColorRef(colorRefOf(m_syntaxColors->GetColor(colorIndex))));
	if (!color.isValid())
		return;
	const int colorRef = lm::colorRefFromColor(color);
	m_syntaxColors->SetColor(colorIndex, CEColor(static_cast<uint32_t>(colorRef)));
	button->setColorRef(colorRef);
	m_colorsChanged = true;
}

/** UpdateTextColorButtonsState: a text color ticked and shown when it is
    one, unticked and hidden when none. */
void OptionsDialog::updateTextColorButtons()
{
	for (const auto &[box, button] : m_mergeTextColors)
	{
		const bool used = button->colorRef() != -1;
		box->setChecked(used);
		button->setVisible(used);
	}
}

/** EnableColorButtons: the labels as "Use customized text colors" says
    (the buttons stay, and do nothing then). */
void OptionsDialog::updateTextColorControls()
{
	for (QWidget *label : m_textColorLabels)
		label->setEnabled(m_chkCustomTextColors->isChecked());
}

/** UpdateControls: the list and the button with the system colors on, the
    button in the listed color's. */
void OptionsDialog::updateSysColorControls()
{
	const bool enabled = m_chkSysColorHook->isChecked();
	m_cmbSysColorName->setEnabled(enabled);
	m_btnSysColor->setEnabled(enabled);
	const int index = m_cmbSysColorName->currentData().toInt();
	m_btnSysColor->setColorRef(m_sysColors[static_cast<size_t>(qBound(0, index, 30))]);
}

/** OnDefaults: the system's colors, then the system colors of the scheme
    of the mode in effect. (Upstream takes the light mode's scheme,
    OPT_COLOR_SCHEME, whatever the mode: in dark mode Defaults put the
    light scheme's system colors, usually none, on a dark scheme.) */
void OptionsDialog::restoreSysColorDefaults()
{
	for (size_t i = 0; i < m_sysColors.size(); ++i)
		m_sysColors[i] = lm::colorRefFromColor(lm::originalSystemColor(static_cast<int>(i)));
	m_customSysColors.clear();
	bool enabled = false;
	QString colors;
	const QString path = lm::ColorSchemes::path(lm::colorSchemeName(lm::colorsAreDark()));
	if (path.isEmpty() || !lm::readSchemeSystemColors(path, &enabled, &colors))
	{
		COptionsMgr *mgr = GetOptionsMgr();
		enabled = mgr != nullptr && mgr->GetDefault<bool>(OPT_SYSCOLOR_HOOK_ENABLED);
	}
	else
	{
		for (const auto &[index, color] : lm::systemColorRefs(colors))
			if (index >= 0 && index < static_cast<int>(m_sysColors.size()))
			{
				m_sysColors[static_cast<size_t>(index)] = color;
				m_customSysColors.insert(index);
			}
	}
	m_chkSysColorHook->setChecked(enabled);
	updateSysColorControls();
	m_colorsChanged = true;
}

bool OptionsDialog::cancelEnabledForTest() const
{
	return m_cancelButton->isEnabled();
}

void OptionsDialog::reject()
{
	if (m_cancelButton != nullptr && !m_cancelButton->isEnabled())
		return;
	QDialog::reject();
}

bool OptionsDialog::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == m_editTabSize && event->type() == QEvent::FocusOut)
	{
		const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
		const bool left = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason
			|| reason == Qt::MouseFocusReason || reason == Qt::ShortcutFocusReason;
		const int value = m_editTabSize->text().toInt();
		if (left && (value < 1 || value > kMaxTabSize))
			lm::showWarning(this, tr("Tab size value is out of range. Please use 1 - %1.")
				.arg(kMaxTabSize));
	}
	return QDialog::eventFilter(watched, event);
}

/** WinMerge's Archive Support page (PropArchive): whether an archive
    opens as a folder, and whether its content, not its extension, says
    it is one. */
QWidget *OptionsDialog::buildArchivePage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	m_chkArchiveEnable = new QCheckBox(tr("Enable archive file support"), page);
	m_chkArchiveEnable->setObjectName(QStringLiteral("archiveEnable"));
	box->addWidget(m_chkArchiveEnable);
	m_chkArchiveProbe = new QCheckBox(
		tr("Detect archive type from file signature"), page);
	m_chkArchiveProbe->setObjectName(QStringLiteral("archiveProbe"));
	box->addWidget(m_chkArchiveProbe);
	// UpdateControls: the detection only with the support on
	connect(m_chkArchiveEnable, &QCheckBox::toggled,
		m_chkArchiveProbe, &QWidget::setEnabled);

	box->addStretch(1);
	return page;
}

/** WinMerge's Project page (PropProject): which items of a project file
    opening it (Open), loading it into the selection screen (Load) and
    saving one (Save) take, a column of check boxes each. Upstream's
    Plugins row is left out: LibreMerge runs no plugins. */
QWidget *OptionsDialog::buildProjectPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);
	box->addWidget(new QLabel(tr("Items saved to or restored from the project file:"), page));
	m_projectList = new QTreeWidget(page);
	m_projectList->setObjectName(QStringLiteral("projectList"));
	m_projectList->setRootIsDecorated(false);
	m_projectList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_projectList->setAllColumnsShowFocus(true);
	m_projectList->setHeaderLabels({ tr("Item"), tr("Open"), tr("Load"), tr("Save") });
	m_projectList->header()->setSectionsClickable(false);
	m_projectList->header()->setStretchLastSection(false);
	const struct { const char *text; Options::Project::Item item; } rows[] = {
		{ QT_TR_NOOP("File Filters"), Options::Project::Item::FileFilter },
		{ QT_TR_NOOP("Include Subfolders"), Options::Project::Item::IncludeSubfolders },
		{ QT_TR_NOOP("Compare Options"), Options::Project::Item::CompareOptions },
		{ QT_TR_NOOP("Hidden Items"), Options::Project::Item::HiddenItems },
	};
	for (const auto &row : rows)
	{
		auto *item = new QTreeWidgetItem(m_projectList, { tr(row.text) });
		item->setData(0, Qt::UserRole, static_cast<int>(row.item));
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
		for (int column = 1; column <= Options::Project::OperationCount; ++column)
			item->setCheckState(column, Qt::Unchecked);
	}
	lm::ensureItemCheckBoxes(m_projectList);
	// upstream's widths: 150 points for the items, 50 for each operation
	const int point = qMax(1, logicalDpiX());
	m_projectList->setColumnWidth(0, 150 * point / 72);
	for (int column = 1; column <= Options::Project::OperationCount; ++column)
		m_projectList->setColumnWidth(column, qMax(50 * point / 72,
			m_projectList->header()->sectionSizeHint(column)));
	// as tall as its rows, as upstream's list is
	m_projectList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_projectList->setFixedHeight(m_projectList->header()->sizeHint().height()
		+ m_projectList->sizeHintForRow(0) * m_projectList->topLevelItemCount()
		+ 2 * m_projectList->frameWidth() + 2);
	box->addWidget(m_projectList);
	box->addStretch(1);
	return page;
}

/** The Project page's check boxes and the settings (PropProject's
    DoDataExchange). */
static void showProjectSettings(QTreeWidget *list, const Options::Project::Settings settings)
{
	for (int row = 0; row < list->topLevelItemCount(); ++row)
	{
		QTreeWidgetItem *item = list->topLevelItem(row);
		const int index = item->data(0, Qt::UserRole).toInt();
		for (int operation = 0; operation < Options::Project::OperationCount; ++operation)
			item->setCheckState(operation + 1,
				settings[operation][index] ? Qt::Checked : Qt::Unchecked);
	}
}

/** WinMerge's Backup Files page (PropBackups): the file compare switch;
    folder, name and the folder compare switch are not offered yet. */
QWidget *OptionsDialog::buildBackupPage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	box->addWidget(new QLabel(tr("Create backup files for:"), page));
	auto *backupRow = new QHBoxLayout;
	backupRow->addSpacing(16);
	m_chkBackup = new QCheckBox(tr("File compare"), page);
	backupRow->addWidget(m_chkBackup, 1);
	box->addLayout(backupRow);

	box->addSpacing(8);
	box->addWidget(noteLabel(tr("The backup is written next to the original "
		"file, with the .bak extension."), page));

	box->addStretch(1);
	return page;
}

/** WinMerge's Codepage page (PropCodepage): the codepage the files that
    are not Unicode read in, and how a file's encoding is detected. The
    detector is ICU's, where upstream's is Windows' mlang.dll. */
QWidget *OptionsDialog::buildCodepagePage()
{
	auto *page = new QWidget(this);
	auto *box = new QVBoxLayout(page);

	auto *group = new QGroupBox(tr("Default Codepage"), page);
	auto *groupLayout = new QVBoxLayout(group);
	groupLayout->addWidget(noteLabel(
		tr("Select the default codepage for non-Unicode files:"), group));
	m_radCodepage[0] = new QRadioButton(tr("System codepage"), group);
	m_radCodepage[1] = new QRadioButton(
		tr("According to LibreMerge User Interface"), group);
	m_radCodepage[2] = new QRadioButton(tr("Custom codepage:"), group);
	for (int i = 0; i < 3; ++i)
	{
		m_radCodepage[i]->setObjectName(QStringLiteral("codepageMode%1").arg(i));
		groupLayout->addWidget(m_radCodepage[i]);
	}
	// the codepages ICU converts and the ones upstream adds by hand, each
	// "% 5d - name" and sorted as text (CBS_SORT); a number or a name can
	// be typed in too
	m_cmbCustomCodepage = new QComboBox(group);
	m_cmbCustomCodepage->setObjectName(QStringLiteral("customCodepage"));
	m_cmbCustomCodepage->setEditable(true);
	m_cmbCustomCodepage->setInsertPolicy(QComboBox::NoInsert);
	QList<QPair<QString, int>> codepages;
	const auto entry = [](int codepage, const String &desc) {
		return QStringLiteral("%1 - %2").arg(codepage, 5)
			.arg(QString::fromStdString(desc));
	};
	if (IExconverter *converter = Exconverter::getInstance())
	{
		for (const CodePageInfo &info : converter->enumCodePages())
			if (info.codepage != 1200) // UNICODE
				codepages.append({ entry(info.codepage, info.desc), info.codepage });
		for (const int codepage : { 437, 850, 858, 860, 863, 861, 1200, 1201, 65000 })
		{
			String desc;
			converter->getCodepageDescription(codepage, desc);
			const QString text = entry(codepage, desc);
			if (std::none_of(codepages.cbegin(), codepages.cend(),
					[&text](const QPair<QString, int> &item) { return item.first == text; }))
				codepages.append({ text, codepage });
		}
	}
	std::sort(codepages.begin(), codepages.end(),
		[](const QPair<QString, int> &a, const QPair<QString, int> &b) {
			return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
		});
	for (const auto &item : codepages)
		m_cmbCustomCodepage->addItem(item.first, item.second);
	groupLayout->addWidget(m_cmbCustomCodepage);
	box->addWidget(group);
	// the field only with "Custom codepage:"
	connect(m_radCodepage[2], &QRadioButton::toggled,
		m_cmbCustomCodepage, &QWidget::setEnabled);

	// without upstream's "Restart required.": the detection applies to the
	// next file opened, here as in WinMerge
	m_chkDetectCodepage = new QCheckBox(
		tr("Detect codepage for: .html, .rc, .xml"), page);
	m_chkDetectCodepage->setObjectName(QStringLiteral("detectCodepage"));
	box->addWidget(m_chkDetectCodepage);
	m_chkDetectCodepage2 = new QCheckBox(
		tr("Detect codepage for files with ICU"), page);
	m_chkDetectCodepage2->setObjectName(QStringLiteral("detectCodepage2"));
	box->addWidget(m_chkDetectCodepage2);
	// the language the detector picks among, mlang's auto-select types
	m_cmbAutodetectType = new QComboBox(page);
	m_cmbAutodetectType->setObjectName(QStringLiteral("autodetectType"));
	m_cmbAutodetectType->addItem(tr("Auto-Select"), 50001);
	m_cmbAutodetectType->addItem(tr("Chinese Simplified (Auto-Select)"), 50936);
	m_cmbAutodetectType->addItem(tr("Chinese Traditional (Auto-Select)"), 50950);
	m_cmbAutodetectType->addItem(tr("Japanese (Auto-Select)"), 50932);
	m_cmbAutodetectType->addItem(tr("Korean (Auto-Select)"), 50949);
	auto *typeRow = new QHBoxLayout;
	typeRow->addSpacing(16);
	typeRow->addWidget(m_cmbAutodetectType, 1);
	box->addLayout(typeRow);
	connect(m_chkDetectCodepage2, &QCheckBox::toggled,
		m_cmbAutodetectType, &QWidget::setEnabled);

	box->addStretch(1);
	return page;
}

/** The custom codepage's item, or its number alone when none is it. */
void OptionsDialog::showCustomCodepage(int codepage)
{
	const int index = m_cmbCustomCodepage->findData(codepage);
	if (index >= 0)
		m_cmbCustomCodepage->setCurrentIndex(index);
	else
		m_cmbCustomCodepage->setEditText(QString::number(codepage));
}

void OptionsDialog::showAutodetectType(int type)
{
	const int index = m_cmbAutodetectType->findData(type != 0 ? type : 50001);
	if (index >= 0)
		m_cmbAutodetectType->setCurrentIndex(index);
}

void OptionsDialog::load()
{
	QSettings settings;
	m_chkScrollFirst->setChecked(lm::setting(kScrollFirst, false).toBool());
	m_chkScrollFirstInline->setChecked(
		lm::setting(kScrollFirstInline, false).toBool());
	m_cmbCloseWithEsc->setCurrentIndex(closeWithEsc());
	m_chkVerifyPaths->setChecked(verifyOpenPaths());
	m_chkAskClose->setChecked(lm::setting(kAskClose, false).toBool());
	m_chkPreserveFileTime->setChecked(preserveFileTime());
	m_chkShowSelector->setChecked(
		lm::setting(kShowSelector, false).toBool());
	m_chkCloseSelector->setChecked(closeSelectorOnCompare());
	m_cmbAutoComplete->setCurrentIndex(autoCompleteSource());
	m_cmbAutoReload->setCurrentIndex(autoReloadModifiedFiles());
	// Colors > Color Schemes (PropColorSchemes::ReadOptions): the mode as
	// WinMerge numbers it, light, dark, then following the system
	const lm::ThemeMode mode = lm::Theme::instance()->mode();
	m_cmbColorMode->setCurrentIndex(mode == lm::ThemeMode::Light ? 0
		: mode == lm::ThemeMode::Dark ? 1 : 2);
	reloadColorSchemes();
	showColorSchemes(lm::colorSchemeName(false), lm::colorSchemeName(true));
	updateColorSchemeControls();
	loadColorPages();
	const int langIndex =
		m_cmbLanguage->findData(settings.value(kLanguage).toString());
	m_cmbLanguage->setCurrentIndex(qMax(0, langIndex));
	m_chkBackup->setChecked(lm::setting(kBackup, true).toBool());

	if (COptionsMgr *mgr = GetOptionsMgr())
	{
		const int whitespace = mgr->GetInt(OPT_CMP_IGNORE_WHITESPACE);
		m_radWhitespace[whitespace >= 0 && whitespace < 3 ? whitespace : 0]
			->setChecked(true);
		m_chkIgnoreBlank->setChecked(mgr->GetBool(OPT_CMP_IGNORE_BLANKLINES));
		m_chkIgnoreCase->setChecked(mgr->GetBool(OPT_CMP_IGNORE_CASE));
		m_chkIgnoreEol->setChecked(mgr->GetBool(OPT_CMP_IGNORE_EOL));
		m_chkIgnoreNumbers->setChecked(mgr->GetBool(OPT_CMP_IGNORE_NUMBERS));
		m_chkIgnoreCodepage->setChecked(mgr->GetBool(OPT_CMP_IGNORE_CODEPAGE));
		m_chkFilterComments->setChecked(mgr->GetBool(OPT_CMP_FILTER_COMMENTLINES));
		m_chkIgnoreMissingEol->setChecked(
			mgr->GetBool(OPT_CMP_IGNORE_MISSING_TRAILING_EOL));
		m_chkIgnoreLineBreaks->setChecked(mgr->GetBool(OPT_CMP_IGNORE_LINE_BREAKS));
		m_chkMovedBlocks->setChecked(mgr->GetBool(OPT_CMP_MOVED_BLOCKS));
		m_cmbAlgorithm->setCurrentIndex(mgr->GetInt(OPT_CMP_DIFF_ALGORITHM));
		m_chkIndentHeuristic->setChecked(mgr->GetBool(OPT_CMP_INDENT_HEURISTIC));
		m_chkBlankOutIgnored->setChecked(
			mgr->GetBool(OPT_CMP_COMPLETELY_BLANK_OUT_IGNORED_CHANGES));
		m_chkHiliteSyntax->setChecked(mgr->GetBool(OPT_SYNTAX_HIGHLIGHT));
		m_cmbHiliteSyntaxMode->setCurrentIndex(mgr->GetInt(OPT_SYNTAX_HIGHLIGHT_MODE));
		m_chkMixedEol->setChecked(mgr->GetBool(OPT_ALLOW_MIXED_EOL));
		m_editTabSize->setText(QString::number(mgr->GetInt(OPT_TAB_SIZE)));
		(mgr->GetInt(OPT_TAB_TYPE) == 0 ? m_radInsertTabs : m_radInsertSpaces)
			->setChecked(true);
		m_chkAutomaticRescan->setChecked(mgr->GetBool(OPT_AUTOMATIC_RESCAN));
		m_cmbCopyGranularity->setCurrentIndex(mgr->GetInt(OPT_COPY_GRANULARITY));
		m_chkViewLineDiffs->setChecked(mgr->GetBool(OPT_WORDDIFF_HIGHLIGHT));
		(mgr->GetBool(OPT_BREAK_ON_WORDS) ? m_radWordLevel : m_radCharLevel)
			->setChecked(true);
		m_cmbBreakType->setCurrentIndex(mgr->GetInt(OPT_BREAK_TYPE));
		m_editBreakChars->setText(
			QString::fromStdString(mgr->GetString(OPT_BREAK_SEPARATORS)));
		updateLineDiffControls();
		m_chkArchiveEnable->setChecked(mgr->GetBool(OPT_ARCHIVE_ENABLE));
		m_chkArchiveProbe->setChecked(mgr->GetBool(OPT_ARCHIVE_PROBETYPE));
		m_chkArchiveProbe->setEnabled(m_chkArchiveEnable->isChecked());
		Options::Project::Settings project;
		Options::Project::Load(mgr, project);
		showProjectSettings(m_projectList, project);
		const int mode = mgr->GetInt(OPT_CP_DEFAULT_MODE);
		m_radCodepage[mode >= 0 && mode < 3 ? mode : 0]->setChecked(true);
		m_cmbCustomCodepage->setEnabled(m_radCodepage[2]->isChecked());
		showCustomCodepage(mgr->GetInt(OPT_CP_DEFAULT_CUSTOM));
		const int detect = mgr->GetInt(OPT_CP_DETECT);
		m_chkDetectCodepage->setChecked((detect & 1) != 0);
		m_chkDetectCodepage2->setChecked((detect & 2) != 0);
		m_cmbAutodetectType->setEnabled(m_chkDetectCodepage2->isChecked());
		showAutodetectType(static_cast<int>(static_cast<unsigned>(detect) >> 16));
	}
	m_cmbCompareMethod->setCurrentIndex(lm::currentCompareMethod());
	showTableSettings(lm::TableSettings::load());
	m_cmbBinaryPatterns->setPatterns(lm::binaryFilePatterns());
	m_cmbImagePatterns->setPatterns(lm::imageFilePatterns());
	m_chkImageInFolderCompare->setChecked(lm::imageCompareInFolderCompare());
	// Editor > Syntax (PropEditorSyntax::ReadOptions)
	String extensions[LangServices::SRC_MAX_ENTRY];
	Options::EditorSyntax::Load(GetOptionsMgr(), extensions);
	for (int row = 0; row < m_syntaxList->topLevelItemCount(); ++row)
		m_syntaxList->topLevelItem(row)->setText(1, QString::fromStdString(extensions[row]));
	loadMessageBoxes();
}

void OptionsDialog::save()
{
	QSettings settings;
	settings.setValue(kScrollFirst, m_chkScrollFirst->isChecked());
	settings.setValue(kScrollFirstInline, m_chkScrollFirstInline->isChecked());
	settings.setValue(kCloseWithEsc, m_cmbCloseWithEsc->currentIndex());
	settings.setValue(kVerifyPaths, m_chkVerifyPaths->isChecked());
	settings.setValue(kAskClose, m_chkAskClose->isChecked());
	settings.setValue(kPreserveFileTime, m_chkPreserveFileTime->isChecked());
	settings.setValue(kShowSelector, m_chkShowSelector->isChecked());
	settings.setValue(kCloseSelector, m_chkCloseSelector->isChecked());
	settings.setValue(kAutoComplete, m_cmbAutoComplete->currentIndex());
	settings.setValue(kAutoReload, m_cmbAutoReload->currentIndex());
	settings.setValue(kLanguage, m_cmbLanguage->currentData().toString());
	// Colors > Color Schemes (PropColorSchemes::WriteOptions): the
	// schemes, then the mode, which takes effect at once, unlike the
	// language
	writeColorSchemeOptions();
	saveColorPages();
	const int colorMode = m_cmbColorMode->currentIndex();
	lm::Theme::instance()->setMode(colorMode == 0 ? lm::ThemeMode::Light
		: colorMode == 1 ? lm::ThemeMode::Dark : lm::ThemeMode::System);
	settings.setValue(kBackup, m_chkBackup->isChecked());

	if (COptionsMgr *mgr = GetOptionsMgr())
	{
		int whitespace = 0;
		for (int i = 0; i < 3; ++i)
			if (m_radWhitespace[i]->isChecked())
				whitespace = i;
		mgr->SaveOption(OPT_CMP_IGNORE_WHITESPACE, whitespace);
		mgr->SaveOption(OPT_CMP_IGNORE_BLANKLINES,
			m_chkIgnoreBlank->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_CASE, m_chkIgnoreCase->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_EOL, m_chkIgnoreEol->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_NUMBERS,
			m_chkIgnoreNumbers->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_CODEPAGE, m_chkIgnoreCodepage->isChecked());
		mgr->SaveOption(OPT_CMP_FILTER_COMMENTLINES,
			m_chkFilterComments->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_MISSING_TRAILING_EOL,
			m_chkIgnoreMissingEol->isChecked());
		mgr->SaveOption(OPT_CMP_IGNORE_LINE_BREAKS,
			m_chkIgnoreLineBreaks->isChecked());
		mgr->SaveOption(OPT_CMP_MOVED_BLOCKS, m_chkMovedBlocks->isChecked());
		mgr->SaveOption(OPT_CMP_DIFF_ALGORITHM,
			m_cmbAlgorithm->currentIndex());
		mgr->SaveOption(OPT_CMP_INDENT_HEURISTIC,
			m_chkIndentHeuristic->isChecked());
		mgr->SaveOption(OPT_CMP_COMPLETELY_BLANK_OUT_IGNORED_CHANGES,
			m_chkBlankOutIgnored->isChecked());
		mgr->SaveOption(OPT_SYNTAX_HIGHLIGHT, m_chkHiliteSyntax->isChecked());
		mgr->SaveOption(OPT_SYNTAX_HIGHLIGHT_MODE,
			m_cmbHiliteSyntaxMode->currentIndex());
		mgr->SaveOption(OPT_ALLOW_MIXED_EOL, m_chkMixedEol->isChecked());
		mgr->SaveOption(OPT_TAB_SIZE,
			std::clamp(m_editTabSize->text().toInt(), 1, kMaxTabSize));
		mgr->SaveOption(OPT_TAB_TYPE, m_radInsertTabs->isChecked() ? 0 : 1);
		mgr->SaveOption(OPT_AUTOMATIC_RESCAN, m_chkAutomaticRescan->isChecked());
		mgr->SaveOption(OPT_COPY_GRANULARITY, m_cmbCopyGranularity->currentIndex());
		mgr->SaveOption(OPT_WORDDIFF_HIGHLIGHT, m_chkViewLineDiffs->isChecked());
		mgr->SaveOption(OPT_BREAK_ON_WORDS, m_radWordLevel->isChecked());
		mgr->SaveOption(OPT_BREAK_TYPE, m_cmbBreakType->currentIndex());
		mgr->SaveOption(OPT_BREAK_SEPARATORS, m_editBreakChars->text().toStdString());
		// Editor > Syntax (PropEditorSyntax::WriteOptions): each type's
		// extensions without the ones another of them takes in, saved and
		// handed to the type lookup at once
		String extensions[LangServices::SRC_MAX_ENTRY];
		for (int row = 0; row < m_syntaxList->topLevelItemCount(); ++row)
			extensions[row] = lm::removeDuplicatePatterns(
				m_syntaxList->topLevelItem(row)->text(1)).toStdString();
		Options::EditorSyntax::Save(mgr, extensions);
		mgr->SaveOption(OPT_ARCHIVE_ENABLE, m_chkArchiveEnable->isChecked());
		mgr->SaveOption(OPT_ARCHIVE_PROBETYPE, m_chkArchiveProbe->isChecked());
		// the Project page's checks, over the saved settings (the Plugins
		// ones keep theirs)
		Options::Project::Settings project;
		Options::Project::Load(mgr, project);
		for (int row = 0; row < m_projectList->topLevelItemCount(); ++row)
		{
			const QTreeWidgetItem *item = m_projectList->topLevelItem(row);
			const int index = item->data(0, Qt::UserRole).toInt();
			for (int operation = 0; operation < Options::Project::OperationCount; ++operation)
				project[operation][index] = item->checkState(operation + 1) == Qt::Checked;
		}
		Options::Project::Save(mgr, project);
		int mode = 0;
		for (int i = 0; i < 3; ++i)
			if (m_radCodepage[i]->isChecked())
				mode = i;
		mgr->SaveOption(OPT_CP_DEFAULT_MODE, mode);
		// GetEncodingCodePageFromNameString: the number the text starts
		// with, or else the codepage it names; neither keeps the old one
		const QString custom = m_cmbCustomCodepage->currentText();
		int codepage = QRegularExpression(QStringLiteral("^\\s*([+-]?\\d+)"))
			.match(custom).captured(1).toInt();
		if (codepage == 0)
			codepage = static_cast<int>(GetEncodingCodePageFromName(custom.toUtf8().constData()));
		if (codepage != 0)
			mgr->SaveOption(OPT_CP_DEFAULT_CUSTOM, codepage);
		mgr->SaveOption(OPT_CP_DETECT, (m_chkDetectCodepage->isChecked() ? 1 : 0)
			| (m_chkDetectCodepage2->isChecked() ? 2 : 0)
			| (m_cmbAutodetectType->currentData().toInt() << 16));
		mgr->FlushOptions();
	}
	lm::saveCompareMethod(m_cmbCompareMethod->currentIndex());
	// the pattern lists without the patterns another of theirs takes in,
	// as WinMerge saves them
	lm::TableSettings table;
	table.csvFilePatterns = lm::removeDuplicatePatterns(m_cmbCsvPatterns->currentText());
	table.csvDelimiter = m_editCsvDelimiter->text();
	table.tsvFilePatterns = lm::removeDuplicatePatterns(m_cmbTsvPatterns->currentText());
	table.dsvFilePatterns = lm::removeDuplicatePatterns(m_cmbDsvPatterns->currentText());
	table.dsvDelimiter = m_editDsvDelimiter->text();
	table.allowNewlinesInQuotes = m_chkAllowNewlines->isChecked();
	table.quote = m_editQuote->text();
	table.save();
	settings.setValue(QStringLiteral("BinaryCompare/FilePatterns"),
		lm::removeDuplicatePatterns(m_cmbBinaryPatterns->currentText().trimmed()));
	settings.setValue(QStringLiteral("ImageCompare/FilePatterns"),
		lm::removeDuplicatePatterns(m_cmbImagePatterns->currentText().trimmed()));
	settings.setValue(QStringLiteral("ImageCompare/EnableInFolderCompare"),
		m_chkImageInFolderCompare->isChecked());
	for (int i = 0; i < m_messageList->topLevelItemCount(); ++i)
	{
		const QTreeWidgetItem *item = m_messageList->topLevelItem(i);
		lm::MessageAnswer answer = lm::NoAnswer;
		if (item->checkState(0) == Qt::Checked)
		{
			const QComboBox *answers = m_messageAnswers.value(i);
			answer = answers != nullptr
				? static_cast<lm::MessageAnswer>(answers->currentData().toInt())
				: lm::AnswerOk;
		}
		lm::setRememberedAnswer(item->data(0, Qt::UserRole).toString(), answer);
	}
	// the page showed what the command line set, and saved it
	lm::forgetCommandLineSettings();
}

void OptionsDialog::showTableSettings(const lm::TableSettings &table)
{
	m_cmbCsvPatterns->setPatterns(table.csvFilePatterns);
	m_editCsvDelimiter->setText(table.csvDelimiter);
	m_cmbTsvPatterns->setPatterns(table.tsvFilePatterns);
	m_cmbDsvPatterns->setPatterns(table.dsvFilePatterns);
	m_editDsvDelimiter->setText(table.dsvDelimiter);
	m_chkAllowNewlines->setChecked(table.allowNewlinesInQuotes);
	m_editQuote->setText(table.quote);
}

/** One page back to WinMerge's defaults, like its per-page button. */
void OptionsDialog::restoreDefaults(int page)
{
	switch (page)
	{
	case GeneralPage:
		m_chkScrollFirst->setChecked(false);
		m_chkScrollFirstInline->setChecked(false);
		m_cmbCloseWithEsc->setCurrentIndex(EscTabOrMainWindow);
		m_chkVerifyPaths->setChecked(true);
		m_chkAskClose->setChecked(false);
		m_chkPreserveFileTime->setChecked(false);
		m_chkShowSelector->setChecked(false);
		m_chkCloseSelector->setChecked(false);
		m_cmbAutoComplete->setCurrentIndex(AutoCompleteFileSystem);
		m_cmbAutoReload->setCurrentIndex(AutoReloadOnWindowActivated);
		m_cmbLanguage->setCurrentIndex(0);
		break;
	case ComparePage:
		m_radWhitespace[0]->setChecked(true);
		m_chkIgnoreBlank->setChecked(false);
		m_chkIgnoreCase->setChecked(false);
		m_chkIgnoreEol->setChecked(false);
		m_chkIgnoreNumbers->setChecked(false);
		m_chkIgnoreCodepage->setChecked(false);
		m_chkFilterComments->setChecked(false);
		m_chkIgnoreMissingEol->setChecked(false);
		m_chkIgnoreLineBreaks->setChecked(false);
		m_chkMovedBlocks->setChecked(false);
		m_cmbAlgorithm->setCurrentIndex(0);
		m_chkIndentHeuristic->setChecked(true);
		m_chkBlankOutIgnored->setChecked(false);
		break;
	case FolderPage:
		m_cmbCompareMethod->setCurrentIndex(0); // Full Contents
		break;
	case TablePage:
		showTableSettings(lm::TableSettings());
		break;
	case BinaryPage:
		m_cmbBinaryPatterns->setPatterns(lm::defaultBinaryFilePatterns());
		break;
	case ImagePage:
		m_cmbImagePatterns->setPatterns(lm::defaultImageFilePatterns());
		m_chkImageInFolderCompare->setChecked(false);
		break;
	case MessageBoxesPage:
		for (int i = 0; i < m_messageList->topLevelItemCount(); ++i)
			m_messageList->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
		break;
	case EditorPage:
		m_chkHiliteSyntax->setChecked(true);
		m_cmbHiliteSyntaxMode->setCurrentIndex(1); // Built-in first
		m_chkMixedEol->setChecked(false);
		m_editTabSize->setText(QStringLiteral("4"));
		m_radInsertTabs->setChecked(true);
		break;
	case EditorCompareMergePage:
		if (COptionsMgr *mgr = GetOptionsMgr())
		{
			m_chkAutomaticRescan->setChecked(mgr->GetDefault<bool>(OPT_AUTOMATIC_RESCAN));
			m_cmbCopyGranularity->setCurrentIndex(mgr->GetDefault<int>(OPT_COPY_GRANULARITY));
			m_chkViewLineDiffs->setChecked(mgr->GetDefault<bool>(OPT_WORDDIFF_HIGHLIGHT));
			(mgr->GetDefault<bool>(OPT_BREAK_ON_WORDS) ? m_radWordLevel : m_radCharLevel)
				->setChecked(true);
			m_cmbBreakType->setCurrentIndex(mgr->GetDefault<int>(OPT_BREAK_TYPE));
			m_editBreakChars->setText(QString::fromStdString(
				mgr->GetDefault<String>(OPT_BREAK_SEPARATORS)));
			updateLineDiffControls();
		}
		break;
	case EditorSyntaxPage:
	{
		// PropEditorSyntax::OnDefaults
		String extensions[LangServices::SRC_MAX_ENTRY];
		Options::EditorSyntax::GetDefaults(GetOptionsMgr(), extensions);
		for (int row = 0; row < m_syntaxList->topLevelItemCount(); ++row)
			m_syntaxList->topLevelItem(row)->setText(1, QString::fromStdString(extensions[row]));
		break;
	}
	case SysColorsPage:
		restoreSysColorDefaults();
		break;
	case ArchivePage:
		if (COptionsMgr *mgr = GetOptionsMgr())
		{
			m_chkArchiveEnable->setChecked(mgr->GetDefault<bool>(OPT_ARCHIVE_ENABLE));
			m_chkArchiveProbe->setChecked(mgr->GetDefault<bool>(OPT_ARCHIVE_PROBETYPE));
		}
		break;
	case ProjectPage:
		if (COptionsMgr *mgr = GetOptionsMgr())
		{
			// PropProject::OnDefaults
			Options::Project::Settings project;
			for (int operation = 0; operation < Options::Project::OperationCount; ++operation)
				for (int item = 0; item < Options::Project::ItemCount; ++item)
					project[operation][item] = Options::Project::GetDefault(mgr,
						static_cast<Options::Project::Operation>(operation),
						static_cast<Options::Project::Item>(item));
			showProjectSettings(m_projectList, project);
		}
		break;
	case BackupPage:
		m_chkBackup->setChecked(true);
		break;
	case CodepagePage:
		if (COptionsMgr *mgr = GetOptionsMgr())
		{
			m_radCodepage[mgr->GetDefault<int>(OPT_CP_DEFAULT_MODE)]->setChecked(true);
			showCustomCodepage(mgr->GetDefault<int>(OPT_CP_DEFAULT_CUSTOM));
			const unsigned detect = mgr->GetDefault<unsigned>(OPT_CP_DETECT);
			m_chkDetectCodepage->setChecked((detect & 1) != 0);
			m_chkDetectCodepage2->setChecked((detect & 2) != 0);
			showAutodetectType(static_cast<int>(detect >> 16));
		}
		break;
	}
}

QList<OptionsDialog::CategoryInfo> OptionsDialog::categoriesForTest() const
{
	QList<CategoryInfo> categories;
	for (QTreeWidgetItemIterator it(m_categories); *it != nullptr; ++it)
	{
		int depth = 0;
		for (QTreeWidgetItem *parent = (*it)->parent(); parent != nullptr;
			parent = parent->parent())
			++depth;
		categories.append({ (*it)->text(0), depth,
			(*it)->data(0, Qt::UserRole).toInt() });
	}
	return categories;
}

void OptionsDialog::selectCategoryForTest(int index)
{
	QTreeWidgetItemIterator it(m_categories);
	for (int i = 0; i < index && *it != nullptr; ++i)
		++it;
	if (*it != nullptr)
		m_categories->setCurrentItem(*it);
}

int OptionsDialog::currentPageForTest() const
{
	return m_pages->currentIndex();
}

QWidget *OptionsDialog::pageForTest(Page page) const
{
	return m_pages->widget(page);
}

void OptionsDialog::restoreDefaultsForTest()
{
	restoreDefaults(m_pages->currentIndex());
}

QList<QPair<QString, bool>> OptionsDialog::messageBoxesForTest() const
{
	QList<QPair<QString, bool>> rows;
	for (int i = 0; i < m_messageList->topLevelItemCount(); ++i)
	{
		const QTreeWidgetItem *item = m_messageList->topLevelItem(i);
		rows.append({ item->text(0), item->checkState(0) == Qt::Checked });
	}
	return rows;
}

void OptionsDialog::setMessageBoxHiddenForTest(int row, bool hidden)
{
	if (QTreeWidgetItem *item = m_messageList->topLevelItem(row))
		item->setCheckState(0, hidden ? Qt::Checked : Qt::Unchecked);
}

QString OptionsDialog::messageBoxAnswerForTest(int row) const
{
	if (const QComboBox *answers = m_messageAnswers.value(row))
		return answers->currentText();
	const QTreeWidgetItem *item = m_messageList->topLevelItem(row);
	return item != nullptr ? item->text(1) : QString();
}

void OptionsDialog::setMessageBoxAnswerForTest(int row, int answerIndex)
{
	if (QComboBox *answers = m_messageAnswers.value(row))
		answers->setCurrentIndex(answerIndex);
}

/** Reset acts at once, like CMessageBoxDialog::ResetMessageBoxes. */
void OptionsDialog::resetMessageBoxes()
{
	lm::resetHiddenMessages();
	loadMessageBoxes();
}
