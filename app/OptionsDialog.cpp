// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "OptionsDialog.h"

#include "BinaryFiles.h"
#include "ImageFormats.h"
#include "EngineOptions.h"
#include "HexDialogs.h"
#include "ItemCheckStyle.h"
#include "MessageBoxes.h"
#include "Settings.h"
#include "TableFiles.h"
#include "Theme.h"

#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFocusEvent>
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

const char *const kPatternsProperty = "lmPatterns";

/** The patterns of a pattern list, kept apart from the field: picking an
    item of the drop-down would leave it in the field otherwise. */
void setPatterns(QComboBox *combo, const QString &patterns)
{
	combo->setProperty(kPatternsProperty, patterns);
	combo->setEditText(patterns);
	// a long list shows from its first pattern, as upstream's field does
	combo->lineEdit()->setCursorPosition(0);
}

/** WinMerge's WildcardDropList: a pattern list whose drop-down holds the
    default patterns, each picked turning on or off in the field, which
    also takes any pattern typed. */
void setUpPatternList(QComboBox *combo, const QString &fixedPatterns)
{
	combo->setEditable(true);
	combo->setInsertPolicy(QComboBox::NoInsert);
	combo->addItems(fixedPatterns.split(QLatin1Char(';'), Qt::SkipEmptyParts));
	QObject::connect(combo->lineEdit(), &QLineEdit::textEdited, combo,
		[combo](const QString &text) { combo->setProperty(kPatternsProperty, text); });
	QObject::connect(combo, &QComboBox::activated, combo, [combo](int index) {
		const QString pattern = combo->itemText(index);
		QStringList patterns = combo->property(kPatternsProperty).toString()
			.split(QLatin1Char(';'), Qt::SkipEmptyParts);
		for (QString &p : patterns)
			p = p.trimmed();
		if (patterns.contains(pattern))
			patterns.removeAll(pattern);
		else
			patterns.append(pattern);
		setPatterns(combo, patterns.join(QLatin1Char(';')));
	});
}

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
	m_pages->addWidget(buildBackupPage());
	m_pages->addWidget(buildCodepagePage());
	pageArea->addWidget(m_pages, 1);
	// every WinMerge page has its own Defaults button, resetting only it
	auto *defaultsRow = new QHBoxLayout;
	defaultsRow->addStretch(1);
	auto *defaultsButton = new QPushButton(tr("Defaults"), this);
	connect(defaultsButton, &QPushButton::clicked, this,
		[this]() { restoreDefaults(m_pages->currentIndex()); });
	defaultsRow->addWidget(defaultsButton);
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

	// the theme (LibreMerge's own) sits with the language, closing the page
	box->addSpacing(8);
	m_cmbTheme = new QComboBox(page);
	m_cmbTheme->setObjectName(QStringLiteral("theme"));
	m_cmbTheme->addItem(tr("System default"), static_cast<int>(lm::ThemeMode::System));
	m_cmbTheme->addItem(tr("Light"), static_cast<int>(lm::ThemeMode::Light));
	m_cmbTheme->addItem(tr("Dark"), static_cast<int>(lm::ThemeMode::Dark));
	addLabeled(box, tr("Theme:"), m_cmbTheme);
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
	m_cmbCsvPatterns = new QComboBox(csv);
	setUpPatternList(m_cmbCsvPatterns, defaults.csvFilePatterns);
	addLabeled(csvLayout, tr("File patterns:"), m_cmbCsvPatterns);
	m_editCsvDelimiter = characterField(csv);
	addLabeled(csvLayout, tr("D&elimiter character:"), m_editCsvDelimiter);
	box->addWidget(csv);

	auto *tsv = new QGroupBox(tr("TSV"), page);
	auto *tsvLayout = new QVBoxLayout(tsv);
	m_cmbTsvPatterns = new QComboBox(tsv);
	setUpPatternList(m_cmbTsvPatterns, defaults.tsvFilePatterns);
	addLabeled(tsvLayout, tr("File patterns:"), m_cmbTsvPatterns);
	box->addWidget(tsv);

	auto *dsv = new QGroupBox(tr("Custom Delimiter-Separated Values"), page);
	auto *dsvLayout = new QVBoxLayout(dsv);
	m_cmbDsvPatterns = new QComboBox(dsv);
	setUpPatternList(m_cmbDsvPatterns, defaults.dsvFilePatterns);
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
	m_cmbBinaryPatterns = new QComboBox(page);
	setUpPatternList(m_cmbBinaryPatterns, lm::defaultBinaryFilePatterns());
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
	m_cmbImagePatterns = new QComboBox(page);
	setUpPatternList(m_cmbImagePatterns, lm::defaultImageFilePatterns());
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
	m_cmbTheme->setCurrentIndex(qMax(0, m_cmbTheme->findData(
		static_cast<int>(lm::Theme::instance()->mode()))));
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
	setPatterns(m_cmbBinaryPatterns, lm::binaryFilePatterns());
	setPatterns(m_cmbImagePatterns, lm::imageFilePatterns());
	m_chkImageInFolderCompare->setChecked(lm::imageCompareInFolderCompare());
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
	// takes effect at once, unlike the language
	lm::Theme::instance()->setMode(
		static_cast<lm::ThemeMode>(m_cmbTheme->currentData().toInt()));
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
	setPatterns(m_cmbCsvPatterns, table.csvFilePatterns);
	m_editCsvDelimiter->setText(table.csvDelimiter);
	setPatterns(m_cmbTsvPatterns, table.tsvFilePatterns);
	setPatterns(m_cmbDsvPatterns, table.dsvFilePatterns);
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
		m_cmbTheme->setCurrentIndex(0); // follow the system
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
		setPatterns(m_cmbBinaryPatterns, lm::defaultBinaryFilePatterns());
		break;
	case ImagePage:
		setPatterns(m_cmbImagePatterns, lm::defaultImageFilePatterns());
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
