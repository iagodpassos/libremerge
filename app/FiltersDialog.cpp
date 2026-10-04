// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "FiltersDialog.h"

#include <stdexcept>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "EngineOptions.h"
#include "FileFilter.h"
#include "FileFilterCombo.h"
#include "FileFilterHelper.h"
#include "FileFilterMgr.h"
#include "FileFilters.h"
#include "ItemCheckStyle.h"
#include "MessageBoxes.h"

namespace
{

const QString kStartPageKey = QStringLiteral("Settings/FilterStartPage");
/** The file a new filter starts from, in the shipped filters' folder. */
const QString kFilterTemplate = QStringLiteral("FileFilter.tmpl");

std::function<QString(bool, const QString &)> &fileChooser()
{
	static std::function<QString(bool, const QString &)> chooser;
	return chooser;
}

std::function<void(const QString &)> &editorStandIn()
{
	static std::function<void(const QString &)> editor;
	return editor;
}

/** The parts of a mask's last filter group ("a;b|c;d" ends in "c;d"). */
QStringList lastGroupParts(const QString &mask, std::vector<String> *groups)
{
	*groups = FileFilterHelper::SplitFilterGroups(mask.toStdString());
	QStringList parts;
	for (const QString &part : QString::fromStdString(groups->back()).split(QLatin1Char(';')))
		parts.append(part.trimmed());
	return parts;
}

/** FileFiltersDlg's GetPresetFiltersFromLastGroup: the presets a mask
    names there, "pf:<name>". */
QStringList presetFiltersFromLastGroup(const QString &mask)
{
	std::vector<String> groups;
	QStringList presets;
	for (const QString &part : lastGroupParts(mask, &groups))
		if (part.startsWith(QStringLiteral("pf:")))
			presets.append(part.mid(3));
	return presets;
}

/** RemovePresetFiltersFromLastGroup: the mask without them. */
QString removePresetFiltersFromLastGroup(const QString &mask)
{
	std::vector<String> groups;
	QStringList kept;
	for (const QString &part : lastGroupParts(mask, &groups))
		if (!part.startsWith(QStringLiteral("pf:")))
			kept.append(part);
	groups.back() = kept.join(QLatin1Char(';')).toStdString();
	return QString::fromStdString(FileFilterHelper::JoinFilterGroups(groups));
}

/** AddPresetFiltersToLastGroup: the mask plus the ticked presets. */
QString addPresetFiltersToLastGroup(const QString &mask, const QTreeWidget *list)
{
	std::vector<String> groups = FileFilterHelper::SplitFilterGroups(mask.toStdString());
	QString last = QString::fromStdString(groups.back());
	for (int i = 0; i < list->topLevelItemCount(); ++i)
	{
		const QTreeWidgetItem *item = list->topLevelItem(i);
		if (item->checkState(0) != Qt::Checked)
			continue;
		if (!last.isEmpty())
			last += QLatin1Char(';');
		last += QStringLiteral("pf:") + item->text(0);
	}
	groups.back() = last.toStdString();
	return QString::fromStdString(FileFilterHelper::JoinFilterGroups(groups));
}

QString chooseFilterFile(QWidget *parent, bool save, const QString &folder,
	const QString &title)
{
	if (fileChooser())
		return fileChooser()(save, folder);
	const QString types = FiltersDialog::tr("File Filters (*.flt);;All Files (*)");
	return save ? QFileDialog::getSaveFileName(parent, title, folder, types)
		: QFileDialog::getOpenFileName(parent, title, folder, types);
}

/** The substitution list's columns, as SubstitutionFiltersDlg::InitList
    inserts them. */
enum SubstitutionColumn
{
	ColumnPattern,
	ColumnReplacement,
	ColumnUseRegExp,
	ColumnCaseSensitive,
	ColumnWholeWord,
	SubstitutionColumnCount
};

/** Only the two text columns edit in place: the others hold a yes or no
    (CSubeditList's boolean value columns), switched with a click. */
class TextColumnsDelegate : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
		const QModelIndex &index) const override
	{
		if (index.column() > ColumnReplacement)
			return nullptr;
		return QStyledItemDelegate::createEditor(parent, option, index);
	}
};

QTreeWidget *makeList(QWidget *parent)
{
	auto *list = new QTreeWidget(parent);
	list->setRootIsDecorated(false);
	list->setUniformRowHeights(true);
	list->setAllColumnsShowFocus(true);
	list->setSelectionMode(QAbstractItemView::SingleSelection);
	// a click on the selected row edits its text, as a list view's label
	// editing does; so do a double click and the platform's edit key
	list->setEditTriggers(QAbstractItemView::DoubleClicked
		| QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
	lm::ensureItemCheckBoxes(list);
	return list;
}

QPushButton *makeButton(const QString &text, const char *name, QWidget *parent)
{
	auto *button = new QPushButton(text, parent);
	button->setObjectName(QLatin1String(name));
	// Enter belongs to OK, whichever button was used last
	button->setAutoDefault(false);
	return button;
}

QTreeWidgetItem *selectedRow(const QTreeWidget *list)
{
	return list->selectedItems().value(0);
}

Qt::CheckState checkState(bool on)
{
	return on ? Qt::Checked : Qt::Unchecked;
}

} // namespace

FiltersDialog::FiltersDialog(QWidget *parent)
	: QDialog(parent)
{
	setObjectName(QStringLiteral("filtersDialog"));
	setWindowModality(Qt::WindowModal);
	setWindowTitle(tr("Filters"));
	// a copy of each to edit; the preset files are read again first, in
	// case they were edited meanwhile
	m_fileFilter = lm::cloneFileFilter();
	m_lineFiltersEnabled = lm::lineFiltersEnabled();
	lm::copyLineFilters(&m_lineFilters);
	lm::copySubstitutionFilters(&m_substitutionFilters);

	auto *layout = new QVBoxLayout(this);
	m_tabs = new QTabWidget(this);
	m_tabs->addTab(buildFileFiltersPage(), tr("File Filters"));
	m_tabs->addTab(buildLineFiltersPage(), tr("Line Filters"));
	m_tabs->addTab(buildSubstitutionFiltersPage(), tr("Substitution Filters"));
	layout->addWidget(m_tabs, 1);

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &FiltersDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	resize(820, 460);

	// opens on the page it was last accepted on
	showPage(QSettings().value(kStartPageKey, 0).toInt());
}

FiltersDialog::~FiltersDialog() = default;

void FiltersDialog::showPage(int page)
{
	m_tabs->setCurrentIndex(page >= 0 && page < m_tabs->count() ? page : 0);
}

int FiltersDialog::currentPage() const
{
	return m_tabs->currentIndex();
}

void FiltersDialog::setFileChooserForTest(
	std::function<QString(bool save, const QString &folder)> chooser)
{
	fileChooser() = std::move(chooser);
}

void FiltersDialog::setEditorForTest(std::function<void(const QString &path)> editor)
{
	editorStandIn() = std::move(editor);
}

QWidget *FiltersDialog::buildFileFiltersPage()
{
	auto *page = new QWidget(this);
	page->setObjectName(QStringLiteral("fileFiltersPage"));
	auto *box = new QVBoxLayout(page);

	box->addWidget(new QLabel(tr("Mask / Filter Expression"), page));
	m_maskCombo = new FileFilterCombo(page);
	m_maskCombo->setObjectName(QStringLiteral("fileFilterMask"));
	m_maskCombo->loadHistory();
	m_maskCombo->lineEdit()->setPlaceholderText(
		tr("e.g. %1").arg(QStringLiteral("*.txt|fe:Size > 100KB")));
	// what is typed is parsed by the dialog's own copy of the filter
	m_maskCombo->setChecker([this](const QString &text) {
		return lm::fileFilterErrors(m_fileFilter.get(), text);
	});
	box->addWidget(m_maskCombo);

	box->addWidget(new QLabel(tr("Preset Filters"), page));
	m_presetList = makeList(page);
	m_presetList->setObjectName(QStringLiteral("presetFilters"));
	m_presetList->setHeaderLabels({ tr("Name"), tr("Description"), tr("Location") });
	m_presetList->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_presetList->header()->resizeSection(0, 170);
	m_presetList->header()->resizeSection(1, 320);
	box->addWidget(m_presetList, 1);

	auto *row = new QHBoxLayout;
	QPushButton *testButton = makeButton(tr("Test..."), "testFileFilter", page);
	QPushButton *installButton = makeButton(tr("Install..."), "installFileFilter", page);
	QPushButton *newButton = makeButton(tr("New..."), "newFileFilter", page);
	m_btnEditPreset = makeButton(tr("Edit..."), "editFileFilter", page);
	m_btnDeletePreset = makeButton(tr("Delete..."), "deleteFileFilter", page);
	row->addWidget(testButton);
	row->addStretch(1);
	row->addWidget(installButton);
	row->addWidget(newButton);
	row->addWidget(m_btnEditPreset);
	row->addWidget(m_btnDeletePreset);
	box->addLayout(row);

	// a preset saved in its editor is read again, and shows its errors
	m_presetWatcher = new QFileSystemWatcher(this);
	const auto presetEdited = [this]() {
		m_fileFilter->ReloadUpdatedFilters();
		showPresetErrors();
	};
	connect(m_presetWatcher, &QFileSystemWatcher::fileChanged, this, presetEdited);
	connect(m_presetWatcher, &QFileSystemWatcher::directoryChanged, this, presetEdited);

	fillPresetList();
	m_maskCombo->setMask(QString::fromStdString(m_fileFilter->GetMaskOrExpression()), false);
	checkPresets(presetFiltersFromLastGroup(m_maskCombo->mask()));

	// ticking a preset writes it into the mask, and a mask typed with
	// presets in it ticks them once the field is left
	connect(m_presetList, &QTreeWidget::itemChanged, this,
		[this](QTreeWidgetItem *, int column) {
			if (column == 0 && !m_checkingPresets)
				presetsToMask();
		});
	connect(m_maskCombo, &FileFilterCombo::focusLeft, this, [this]() {
		if (checkPresets(presetFiltersFromLastGroup(m_maskCombo->mask())))
			presetsToMask();
	});
	connect(m_presetList, &QTreeWidget::itemSelectionChanged, this,
		&FiltersDialog::updateFileButtons);
	connect(m_presetList, &QTreeWidget::itemDoubleClicked, this,
		[this]() { editSelectedFileFilter(); });
	connect(testButton, &QPushButton::clicked, this, &FiltersDialog::testFileFilter);
	connect(installButton, &QPushButton::clicked, this, &FiltersDialog::installFileFilter);
	connect(newButton, &QPushButton::clicked, this, &FiltersDialog::newFileFilter);
	connect(m_btnEditPreset, &QPushButton::clicked, this,
		&FiltersDialog::editSelectedFileFilter);
	connect(m_btnDeletePreset, &QPushButton::clicked, this,
		&FiltersDialog::deleteSelectedFileFilter);
	updateFileButtons();
	return page;
}

void FiltersDialog::fillPresetList()
{
	m_checkingPresets = true;
	m_presetList->clear();
	for (const FileFilterInfo &info : m_fileFilter->GetFileFilters())
	{
		auto *item = new QTreeWidgetItem(m_presetList);
		item->setFlags((item->flags() | Qt::ItemIsUserCheckable)
			& ~(Qt::ItemIsEditable | Qt::ItemIsDropEnabled));
		item->setText(0, QString::fromStdString(info.name));
		item->setText(1, QString::fromStdString(info.description));
		item->setText(2, QString::fromStdString(info.fullpath));
		item->setCheckState(0, Qt::Unchecked);
	}
	m_checkingPresets = false;
	showPresetErrors();
}

QTreeWidgetItem *FiltersDialog::presetRow(const QString &path) const
{
	for (int i = 0; i < m_presetList->topLevelItemCount(); ++i)
		if (m_presetList->topLevelItem(i)->text(2) == path)
			return m_presetList->topLevelItem(i);
	return nullptr;
}

/** A preset whose file has errors shows in red, the errors as its tool
    tip (OnCustomDrawFiltersList, OnInfoTip). */
void FiltersDialog::showPresetErrors()
{
	m_checkingPresets = true;
	const bool dark = palette().color(QPalette::Base).lightness() < 128;
	const QBrush tint(dark ? QColor(80, 40, 40) : QColor(255, 200, 200));
	for (int i = 0; i < m_presetList->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem *item = m_presetList->topLevelItem(i);
		QStringList lines;
		if (const FileFilter *filter
				= m_fileFilter->GetManager()->GetFilterByPath(item->text(2).toStdString()))
			for (const FileFilterErrorInfo &error : filter->errors)
				lines.append(lm::formatFilterError(error));
		for (int column = 0; column < m_presetList->columnCount(); ++column)
		{
			item->setBackground(column, lines.isEmpty() ? QBrush() : tint);
			// without errors a cut-off text shows whole (the list's info tips)
			item->setToolTip(column, lines.isEmpty() ? item->text(column)
				: lines.join(QLatin1Char('\n')));
		}
	}
	m_checkingPresets = false;
}

/** Tick exactly the named presets; true when a tick changed. */
bool FiltersDialog::checkPresets(const QStringList &names)
{
	bool changed = false;
	m_checkingPresets = true;
	for (int i = 0; i < m_presetList->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem *item = m_presetList->topLevelItem(i);
		const bool wanted = names.contains(item->text(0));
		if ((item->checkState(0) == Qt::Checked) != wanted)
		{
			item->setCheckState(0, checkState(wanted));
			changed = true;
		}
	}
	m_checkingPresets = false;
	return changed;
}

/** The mask's last group names the ticked presets, in the list's order. */
void FiltersDialog::presetsToMask()
{
	m_maskCombo->setMask(addPresetFiltersToLastGroup(
		removePresetFiltersFromLastGroup(m_maskCombo->mask()), m_presetList), false);
}

void FiltersDialog::selectPreset(const QString &path)
{
	if (QTreeWidgetItem *item = presetRow(path))
	{
		item->setCheckState(0, Qt::Checked);
		m_presetList->scrollToItem(item);
	}
}

void FiltersDialog::updateFileButtons()
{
	const bool selected = selectedRow(m_presetList) != nullptr;
	m_btnEditPreset->setEnabled(selected);
	m_btnDeletePreset->setEnabled(selected);
}

/** WinMerge's Test Filter dialog (CTestFilterDlg): does a name pass the
    mask as it stands, ticked presets included? */
void FiltersDialog::testFileFilter()
{
	// the presets may just have been edited
	m_fileFilter->ReloadUpdatedFilters();
	showPresetErrors();
	m_fileFilter->SetMaskOrExpression(addPresetFiltersToLastGroup(
		removePresetFiltersFromLastGroup(m_maskCombo->mask()), m_presetList).toStdString());

	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("testFilterDialog"));
	dialog.setWindowModality(Qt::WindowModal);
	dialog.setWindowTitle(tr("Test Filter"));
	auto *grid = new QGridLayout(&dialog);
	grid->addWidget(new QLabel(tr("Testing filter:"), &dialog), 0, 0);
	auto *name = new QLabel(QString::fromStdString(m_fileFilter->GetMaskOrExpression()),
		&dialog);
	name->setObjectName(QStringLiteral("testFilterName"));
	name->setTextInteractionFlags(Qt::TextSelectableByMouse);
	grid->addWidget(name, 0, 1);
	grid->addWidget(new QLabel(tr("Enter text to test:"), &dialog), 1, 0);
	auto *text = new QLineEdit(&dialog);
	text->setObjectName(QStringLiteral("testFilterText"));
	grid->addWidget(text, 1, 1);
	auto *isFolder = new QCheckBox(tr("Folder Name"), &dialog);
	isFolder->setObjectName(QStringLiteral("testFilterIsFolder"));
	grid->addWidget(isFolder, 2, 0, 1, 2);
	grid->addWidget(new QLabel(tr("Result:"), &dialog), 3, 0, 1, 2);
	auto *results = new QPlainTextEdit(&dialog);
	results->setObjectName(QStringLiteral("testFilterResults"));
	results->setReadOnly(true);
	results->setLineWrapMode(QPlainTextEdit::NoWrap);
	grid->addWidget(results, 4, 0, 1, 2);
	grid->setColumnStretch(1, 1);
	auto *buttons = new QHBoxLayout;
	buttons->addStretch(1);
	auto *testButton = new QPushButton(tr("Test"), &dialog);
	testButton->setObjectName(QStringLiteral("testFilterRun"));
	testButton->setDefault(true);
	buttons->addWidget(testButton);
	auto *closeButton = new QPushButton(tr("Close"), &dialog);
	closeButton->setAutoDefault(false);
	buttons->addWidget(closeButton);
	grid->addLayout(buttons, 5, 0, 1, 2);

	connect(testButton, &QPushButton::clicked, &dialog, [this, text, isFolder, results]() {
		// the filters match names written with backslashes
		const String name = QString(text->text()).replace(QLatin1Char('/'),
			QLatin1Char('\\')).toStdString();
		const bool passed = isFolder->isChecked() ? m_fileFilter->includeDir(name)
			: m_fileFilter->includeFile(name);
		// (the two words are not translated upstream either)
		results->appendPlainText(text->text() + QStringLiteral(": ")
			+ (passed ? QStringLiteral("passed") : QStringLiteral("failed")));
	});
	connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
	text->setFocus();
	dialog.resize(520, 340);
	dialog.exec();
}

/** Copy a filter file from anywhere into the user's filter folder. */
void FiltersDialog::installFileFilter()
{
	const QString source = chooseFilterFile(this, false, QString(),
		tr("Locate Filter File to Install"));
	if (source.isEmpty())
		return;
	const QString folder = lm::userFiltersDir();
	QDir().mkpath(folder);
	const QString target = QDir(folder).filePath(QFileInfo(source).fileName());
	const QString failed = tr("Installing filter file failed.\n\n"
		"Could not copy new filter to folder.");
	if (!QFile::copy(source, target))
	{
		// one of that name is there already: ask, then write over it
		if (!QFileInfo::exists(target))
			lm::showError(this, failed);
		else if (lm::askWarning(this, tr("Filter file exists. Overwrite?"))
			&& (!QFile::remove(target) || !QFile::copy(source, target)))
			lm::showError(this, failed);
		return;
	}
	const String path = QDir::toNativeSeparators(target).toStdString();
	lm::globalFileFilter()->GetManager()->AddFilter(path);
	m_fileFilter->GetManager()->AddFilter(path);
	fillPresetList();
	checkPresets(presetFiltersFromLastGroup(m_maskCombo->mask()));
	selectPreset(QString::fromStdString(path));
	updateFileButtons();
}

/** A new filter file from the template, in the user's filter folder,
    opened for editing. */
void FiltersDialog::newFileFilter()
{
	const QString templatePath = QDir(lm::sharedFiltersDir()).filePath(kFilterTemplate);
	if (!QFileInfo(templatePath).isFile())
	{
		lm::showError(this, tr("Cannot find filter template!\n\n"
			"Copy %1 to LibreMerge/Filters Folder:\n%2.")
			.arg(kFilterTemplate, QDir::toNativeSeparators(templatePath)));
		return;
	}
	const QString folder = lm::userFiltersDir();
	QDir().mkpath(folder);
	QString chosen = chooseFilterFile(this, true, folder,
		tr("Select Filename for New Filter"));
	if (chosen.isEmpty())
		return;
	// the extension is the filters' own, whatever was typed
	const QFileInfo info(chosen);
	const QString extension = QString::fromStdString(FileFilterExt);
	if (info.suffix().isEmpty())
		chosen += extension;
	else if (info.suffix().compare(extension.mid(1), Qt::CaseInsensitive) != 0)
		chosen = QDir(info.path()).filePath(info.completeBaseName() + extension);

	QFile source(templatePath);
	QFile target(chosen);
	if (!source.open(QIODevice::ReadOnly)
		|| !target.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		lm::showError(this, tr("Cannot copy filter template:\n%1\n\n"
			"Make sure the folder exists and is writable.")
			.arg(QDir::toNativeSeparators(templatePath)));
		return;
	}
	QByteArray lines = source.readAll();
	lines.replace("${name}", QFileInfo(chosen).completeBaseName().toUtf8());
	target.write(lines);
	target.close();

	editFileFilter(chosen);
	const String path = QDir::toNativeSeparators(chosen).toStdString();
	if (m_fileFilter->GetManager()->AddFilter(path) != FILTER_OK)
		return;
	// both lists are read again, the new filter where it belongs in them
	lm::loadFilterFiles(lm::globalFileFilter());
	lm::loadFilterFiles(m_fileFilter.get());
	fillPresetList();
	checkPresets(presetFiltersFromLastGroup(m_maskCombo->mask()));
	selectPreset(QString::fromStdString(path));
	updateFileButtons();
}

void FiltersDialog::editSelectedFileFilter()
{
	if (const QTreeWidgetItem *item = selectedRow(m_presetList))
		editFileFilter(item->text(2));
}

/** Open a filter file in an editor; WinMerge is not blocked meanwhile
    either, and the file is read again once it is saved. */
void FiltersDialog::editFileFilter(const QString &path)
{
	m_presetWatcher->addPath(path);
	m_presetWatcher->addPath(QFileInfo(path).absolutePath());
	if (editorStandIn())
	{
		editorStandIn()(path);
		return;
	}
#ifdef Q_OS_MACOS
	// the default text editor: a .flt file has no application of its own
	QProcess::startDetached(QStringLiteral("/usr/bin/open"), { QStringLiteral("-t"), path });
#else
	QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}

void FiltersDialog::deleteSelectedFileFilter()
{
	const QTreeWidgetItem *item = selectedRow(m_presetList);
	if (item == nullptr)
		return;
	const QString path = item->text(2);
	if (lm::askWarning(this, tr("Are you sure you want to delete\n\n%1 ?").arg(path)))
	{
		if (QFile::remove(path))
		{
			m_presetWatcher->removePath(path);
			lm::globalFileFilter()->GetManager()->RemoveFilter(path.toStdString());
			m_fileFilter->GetManager()->RemoveFilter(path.toStdString());
			fillPresetList();
			// the mask keeps the presets still listed, and forgets this one
			checkPresets(presetFiltersFromLastGroup(m_maskCombo->mask()));
			presetsToMask();
		}
		else
		{
			lm::showError(this, tr("Failed to delete filter:\n%1\n\n"
				"File may be read-only.").arg(path));
		}
	}
	updateFileButtons();
}

/** FileFiltersDlg::OnOK: the mask as typed, "*.*" when there is none. */
void FiltersDialog::applyFileFilters()
{
	QString mask = m_maskCombo->mask();
	if (mask.trimmed().isEmpty())
		mask = QStringLiteral("*.*");
	m_fileFilter->SetMaskOrExpression(mask.toStdString());
	m_maskCombo->setEditText(mask);
	m_maskCombo->saveHistory();
}

QWidget *FiltersDialog::buildLineFiltersPage()
{
	auto *page = new QWidget(this);
	page->setObjectName(QStringLiteral("lineFiltersPage"));
	auto *box = new QVBoxLayout(page);

	m_chkLineFilters = new QCheckBox(tr("Enable Line Filters"), page);
	m_chkLineFilters->setObjectName(QStringLiteral("enableLineFilters"));
	m_chkLineFilters->setChecked(m_lineFiltersEnabled);
	box->addWidget(m_chkLineFilters);
	box->addWidget(new QLabel(tr("Regular Expressions (one per line):"), page));

	m_lineList = makeList(page);
	m_lineList->setObjectName(QStringLiteral("lineFilters"));
	m_lineList->setHeaderLabel(tr("Regular expression"));
	for (size_t i = 0; i < m_lineFilters.GetCount(); ++i)
	{
		const LineFilterItem &item = m_lineFilters.GetAt(i);
		addLineFilterRow(QString::fromStdString(item.filterStr), item.enabled);
	}
	box->addWidget(m_lineList, 1);

	auto *row = new QHBoxLayout;
	QPushButton *newButton = makeButton(tr("New"), "newLineFilter", page);
	m_btnEditLine = makeButton(tr("Edit"), "editLineFilter", page);
	m_btnRemoveLine = makeButton(tr("Remove"), "removeLineFilter", page);
	row->addWidget(newButton);
	row->addWidget(m_btnEditLine);
	row->addWidget(m_btnRemoveLine);
	row->addStretch(1);
	box->addLayout(row);

	// a new row is empty and not ticked, selected and ready to be typed
	connect(newButton, &QPushButton::clicked, this, [this]() {
		QTreeWidgetItem *item = addLineFilterRow(QString(), false);
		m_lineList->setCurrentItem(item);
		m_lineList->scrollToItem(item);
		editSelectedLineFilter();
	});
	connect(m_btnEditLine, &QPushButton::clicked, this,
		&FiltersDialog::editSelectedLineFilter);
	connect(m_btnRemoveLine, &QPushButton::clicked, this,
		[this]() { removeSelected(m_lineList); });
	connect(m_lineList, &QTreeWidget::itemSelectionChanged, this,
		&FiltersDialog::updateLineButtons);
	// F2 edits on every platform (the platform's own edit key may differ)
	auto *editKey = new QShortcut(QKeySequence(Qt::Key_F2), m_lineList);
	editKey->setContext(Qt::WidgetShortcut);
	connect(editKey, &QShortcut::activated, this,
		&FiltersDialog::editSelectedLineFilter);

	if (m_lineList->topLevelItemCount() > 0)
		m_lineList->setCurrentItem(m_lineList->topLevelItem(0));
	updateLineButtons();
	return page;
}

QTreeWidgetItem *FiltersDialog::addLineFilterRow(const QString &filter, bool enabled)
{
	auto *item = new QTreeWidgetItem(m_lineList);
	item->setFlags((item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable)
		& ~Qt::ItemIsDropEnabled);
	item->setText(0, filter);
	item->setCheckState(0, checkState(enabled));
	return item;
}

void FiltersDialog::editSelectedLineFilter()
{
	m_lineList->setFocus();
	if (QTreeWidgetItem *item = selectedRow(m_lineList))
		m_lineList->editItem(item, 0);
}

void FiltersDialog::removeSelected(QTreeWidget *list)
{
	QTreeWidgetItem *item = selectedRow(list);
	if (item == nullptr)
		return;
	const int index = list->indexOfTopLevelItem(item);
	delete item;
	// the row that took its place, or the last one
	const int next = qMin(list->topLevelItemCount() - 1, index);
	if (next >= 0)
	{
		list->setCurrentItem(list->topLevelItem(next));
		list->scrollToItem(list->topLevelItem(next));
	}
}

void FiltersDialog::updateLineButtons()
{
	const bool selected = selectedRow(m_lineList) != nullptr;
	m_btnEditLine->setEnabled(selected);
	m_btnRemoveLine->setEnabled(selected);
}

QWidget *FiltersDialog::buildSubstitutionFiltersPage()
{
	auto *page = new QWidget(this);
	page->setObjectName(QStringLiteral("substitutionFiltersPage"));
	auto *box = new QVBoxLayout(page);

	auto *note = new QLabel(tr("Changes to the listed pairs below will be ignored "
		"or marked as insignificant. Patches are unaffected."), page);
	note->setWordWrap(true);
	box->addWidget(note);
	m_chkSubstitutions = new QCheckBox(tr("Enable"), page);
	m_chkSubstitutions->setObjectName(QStringLiteral("enableSubstitutionFilters"));
	m_chkSubstitutions->setChecked(m_substitutionFilters.GetEnabled());
	box->addWidget(m_chkSubstitutions);

	m_substitutionList = makeList(page);
	m_substitutionList->setObjectName(QStringLiteral("substitutionFilters"));
	m_substitutionList->setHeaderLabels({ tr("Find what"), tr("Replace with"),
		tr("Regular expression"), tr("Match case"), tr("Match whole word only") });
	m_substitutionList->setItemDelegate(new TextColumnsDelegate(m_substitutionList));
	// the texts share what the three yes/no columns leave; those keep a
	// width of their own, whatever the length of their titles in the
	// language shown (WinMerge gives every column a fixed width)
	QHeaderView *header = m_substitutionList->header();
	header->setStretchLastSection(false);
	header->setTextElideMode(Qt::ElideRight);
	header->setSectionResizeMode(ColumnPattern, QHeaderView::Stretch);
	header->setSectionResizeMode(ColumnReplacement, QHeaderView::Stretch);
	for (int column = ColumnUseRegExp; column < SubstitutionColumnCount; ++column)
	{
		header->setSectionResizeMode(column, QHeaderView::Interactive);
		header->resizeSection(column, 132);
		m_substitutionList->headerItem()->setToolTip(column,
			m_substitutionList->headerItem()->text(column));
	}
	for (size_t i = 0; i < m_substitutionFilters.GetCount(); ++i)
		addSubstitutionRow(m_substitutionFilters.GetAt(i));
	box->addWidget(m_substitutionList, 1);

	auto *row = new QHBoxLayout;
	QPushButton *addButton = makeButton(tr("Add"), "addSubstitutionFilter", page);
	m_btnRemoveSubstitution = makeButton(tr("Remove"), "removeSubstitutionFilter", page);
	m_btnClearSubstitutions = makeButton(tr("Clear"), "clearSubstitutionFilters", page);
	row->addWidget(addButton);
	row->addWidget(m_btnRemoveSubstitution);
	row->addStretch(1);
	row->addWidget(m_btnClearSubstitutions);
	box->addLayout(row);

	// a new pair is ticked, with "<Edit here>" on both sides
	connect(addButton, &QPushButton::clicked, this, [this]() {
		SubstitutionFilter filter;
		filter.enabled = true;
		filter.pattern = tr("<Edit here>").toStdString();
		filter.replacement = filter.pattern;
		QTreeWidgetItem *item = addSubstitutionRow(filter);
		m_substitutionList->setCurrentItem(item);
		m_substitutionList->scrollToItem(item);
		updateSubstitutionButtons();
	});
	connect(m_btnRemoveSubstitution, &QPushButton::clicked, this, [this]() {
		removeSelected(m_substitutionList);
		updateSubstitutionButtons();
	});
	connect(m_btnClearSubstitutions, &QPushButton::clicked, this, [this]() {
		m_substitutionList->clear();
		updateSubstitutionButtons();
	});
	connect(m_substitutionList, &QTreeWidget::itemSelectionChanged, this,
		&FiltersDialog::updateSubstitutionButtons);
	updateSubstitutionButtons();
	return page;
}

QTreeWidgetItem *FiltersDialog::addSubstitutionRow(const SubstitutionFilter &filter)
{
	auto *item = new QTreeWidgetItem(m_substitutionList);
	item->setFlags((item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable)
		& ~Qt::ItemIsDropEnabled);
	item->setText(ColumnPattern, QString::fromStdString(filter.pattern));
	item->setText(ColumnReplacement, QString::fromStdString(filter.replacement));
	item->setCheckState(ColumnPattern, checkState(filter.enabled));
	item->setCheckState(ColumnUseRegExp, checkState(filter.useRegExp));
	item->setCheckState(ColumnCaseSensitive, checkState(filter.caseSensitive));
	item->setCheckState(ColumnWholeWord, checkState(filter.matchWholeWordOnly));
	return item;
}

void FiltersDialog::updateSubstitutionButtons()
{
	m_btnRemoveSubstitution->setEnabled(selectedRow(m_substitutionList) != nullptr);
	m_btnClearSubstitutions->setEnabled(m_substitutionList->topLevelItemCount() > 0);
}

bool FiltersDialog::applyLineFilters()
{
	m_lineFilters.Empty();
	for (int i = 0; i < m_lineList->topLevelItemCount(); ++i)
	{
		const QTreeWidgetItem *item = m_lineList->topLevelItem(i);
		m_lineFilters.AddFilter(item->text(0).toStdString(),
			item->checkState(0) == Qt::Checked);
	}
	try
	{
		m_lineFilters.MakeFilterList(true);
	}
	catch (const std::runtime_error &e)
	{
		showPage(LineFiltersPage);
		lm::showWarning(this, QString::fromUtf8(e.what()));
		return false;
	}
	m_lineFiltersEnabled = m_chkLineFilters->isChecked();
	return true;
}

bool FiltersDialog::applySubstitutionFilters()
{
	m_substitutionFilters.Empty();
	for (int i = 0; i < m_substitutionList->topLevelItemCount(); ++i)
	{
		const QTreeWidgetItem *item = m_substitutionList->topLevelItem(i);
		const auto ticked = [item](int column) {
			return item->checkState(column) == Qt::Checked;
		};
		const bool useRegExp = ticked(ColumnUseRegExp);
		// a regular expression says by itself where words end
		const bool wholeWord = !useRegExp && ticked(ColumnWholeWord);
		m_substitutionFilters.Add(item->text(ColumnPattern).toStdString(),
			item->text(ColumnReplacement).toStdString(), useRegExp,
			ticked(ColumnCaseSensitive), wholeWord, ticked(ColumnPattern));
	}
	try
	{
		m_substitutionFilters.MakeSubstitutionList(true);
	}
	catch (const std::runtime_error &e)
	{
		showPage(SubstitutionFiltersPage);
		lm::showWarning(this, QString::fromUtf8(e.what()));
		return false;
	}
	m_substitutionFilters.SetEnabled(m_chkSubstitutions->isChecked());
	return true;
}

void FiltersDialog::accept()
{
	// text still being typed in a row counts, as when the in-place edit
	// loses the focus to the OK button
	if (QWidget *editor = focusWidget())
		if (m_lineList->viewport()->isAncestorOf(editor)
			|| m_substitutionList->viewport()->isAncestorOf(editor))
			m_tabs->setFocus();
	const int page = currentPage();
	if (!applyLineFilters() || !applySubstitutionFilters())
		return;
	applyFileFilters();
	QSettings().setValue(kStartPageKey, page);
	QDialog::accept();
}
