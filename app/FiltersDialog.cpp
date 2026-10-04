// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "FiltersDialog.h"

#include <stdexcept>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "EngineOptions.h"
#include "ItemCheckStyle.h"
#include "MessageBoxes.h"

namespace
{

const QString kStartPageKey = QStringLiteral("Settings/FilterStartPage");

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
	m_lineFiltersEnabled = lm::lineFiltersEnabled();
	lm::copyLineFilters(&m_lineFilters);
	lm::copySubstitutionFilters(&m_substitutionFilters);

	auto *layout = new QVBoxLayout(this);
	m_tabs = new QTabWidget(this);
	m_tabs->addTab(buildLineFiltersPage(), tr("Line Filters"));
	m_tabs->addTab(buildSubstitutionFiltersPage(), tr("Substitution Filters"));
	layout->addWidget(m_tabs, 1);

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &FiltersDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	resize(780, 440);

	// opens on the page it was last accepted on
	showPage(QSettings().value(kStartPageKey, 0).toInt());
}

void FiltersDialog::showPage(int page)
{
	m_tabs->setCurrentIndex(page == SubstitutionFiltersPage ? 1 : 0);
}

int FiltersDialog::currentPage() const
{
	return m_tabs->currentIndex() == 1 ? SubstitutionFiltersPage : LineFiltersPage;
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
	QSettings().setValue(kStartPageKey, page);
	QDialog::accept();
}
