// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileFilterMenu.h"

#include <vector>
#include <QActionGroup>
#include <QCoreApplication>

#include "ComparisonResultFilterDialog.h"
#include "FileFilterHelper.h"
#include "FilterConditionDialog.h"

namespace
{

QString text(const char *source)
{
	return QCoreApplication::translate("FileFilterMenu", source);
}

// the items' texts, as upstream's IDR_POPUP_FILTERMENU has them

const char *const kSizeTexts[] = {
	QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 1KB"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "1KB or more"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 10KB"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "10KB or more"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 100KB"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "100KB or more"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 1MB"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "1MB or more"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 10MB"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "10MB or more"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 100MB"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "100MB or more"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 1GB"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "1GB or more"),
};

struct DateGroup
{
	const char *title;
	std::vector<const char *> items;
};

const DateGroup kDateGroups[] = {
	{ QT_TRANSLATE_NOOP("FileFilterMenu", "&Hour"), {
		QT_TRANSLATE_NOOP("FileFilterMenu", "More than 1 hour ago"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Within 1 hour") } },
	{ QT_TRANSLATE_NOOP("FileFilterMenu", "&Day"), {
		QT_TRANSLATE_NOOP("FileFilterMenu", "Before today"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Today"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Before yesterday"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Yesterday"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Since yesterday") } },
	{ QT_TRANSLATE_NOOP("FileFilterMenu", "&Week"), {
		QT_TRANSLATE_NOOP("FileFilterMenu", "Before this week"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "This week"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Before last week"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Last week"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Since last week") } },
	{ QT_TRANSLATE_NOOP("FileFilterMenu", "&Month"), {
		QT_TRANSLATE_NOOP("FileFilterMenu", "Before this month"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "This month"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Before last month"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Last month"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Since last month") } },
	{ QT_TRANSLATE_NOOP("FileFilterMenu", "&Year"), {
		QT_TRANSLATE_NOOP("FileFilterMenu", "Before this year"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "This year"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Before last year"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Last year"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Since last year") } },
};

const char *const kComparisonTexts[] = {
	QT_TRANSLATE_NOOP("FileFilterMenu", "Equal"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "Not Equal"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "Less Than"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "Less Than or Equal to"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "Greater Than"),
	QT_TRANSLATE_NOOP("FileFilterMenu", "Greater Than or Equal to"),
};

// the conditions the runs of commands stand for, "%1" (and "%2") being
// the property they are about

const char *const kSizeConditions[] = {
	"%1 < 1KB", "%1 >= 1KB", "%1 < 10KB", "%1 >= 10KB", "%1 < 100KB", "%1 >= 100KB",
	"%1 < 1MB", "%1 >= 1MB", "%1 < 10MB", "%1 >= 10MB", "%1 < 100MB", "%1 >= 100MB",
	"%1 < 1GB", "%1 >= 1GB",
};

const char *const kDateConditions[] = {
	"%1 < now() - 1hour", "%1 >= now() - 1hour",
	"%1 < today()", "%1 >= today()",
	"%1 < today() - 1day", "inRange(%1, today() - 1day, today())", "%1 >= today() - 1day",
	"%1 < startOfWeek(now())", "%1 >= startOfWeek(now())",
	"%1 < startOfWeek(now()) - 7days",
	"inRange(%1, startOfWeek(now()) - 7days, startOfWeek(now()))",
	"%1 >= startOfWeek(now()) - 7days",
	"%1 < startOfMonth(now())", "%1 >= startOfMonth(now())",
	"%1 < startOfMonth(startOfMonth(now()) - 1day)",
	"inRange(%1, startOfMonth(startOfMonth(now()) - 1day), startOfMonth(now()))",
	"%1 >= startOfMonth(startOfMonth(now()) - 1day)",
	"%1 < startOfYear(now())", "%1 >= startOfYear(now())",
	"%1 < startOfYear(startOfYear(now()) - 1day)",
	"inRange(%1, startOfYear(startOfYear(now()) - 1day), startOfYear(now()))",
	"%1 >= startOfYear(startOfYear(now()) - 1day)",
};

const char *const kAttrConditions[] = {
	"%1 contains \"R\"", "%1 not contains \"R\"",
	"%1 contains \"H\"", "%1 not contains \"H\"",
	"%1 contains \"S\"", "%1 not contains \"S\"",
};

const char *const kContentTransforms[] = {
	"%1", "%1",
	"sublines(%1, 0, 1)", "sublines(%1, 0, 1)",
	"sublines(%1, 0, 10)", "sublines(%1, 0, 10)",
	"sublines(%1, -10)", "sublines(%1, -10)",
	"sublines(%1, -1)", "sublines(%1, -1)",
};

const char *const kLineCountConditions[] = {
	"lineCount(%1) < 10", "lineCount(%1) >= 10",
	"lineCount(%1) < 100", "lineCount(%1) >= 100",
	"lineCount(%1) < 1000", "lineCount(%1) >= 1000",
	"lineCount(%1) < 10000", "lineCount(%1) >= 10000",
	"lineCount(%1) < 100000", "lineCount(%1) >= 100000",
};

const char *const kCountConditions[] = { "%1 == 0", "%1 >= 1" };

const char *const kDiffSizeConditions[] = {
	"%1 = %2", "%1 != %2", "%1 < %2", "%1 <= %2", "%1 > %2", "%1 >= %2",
	"abs(%1 - %2) < 10", "abs(%1 - %2) >= 10",
	"abs(%1 - %2) < 100", "abs(%1 - %2) >= 100",
	"abs(%1 - %2) < 1KB", "abs(%1 - %2) >= 1KB",
};

const char *const kDiffDateConditions[] = {
	"%1 = %2", "%1 != %2", "%1 < %2", "%1 <= %2", "%1 > %2", "%1 >= %2",
	"abs(%1 - %2) < 1second", "abs(%1 - %2) >= 1second",
	"abs(%1 - %2) < 1minute", "abs(%1 - %2) >= 1minute",
	"abs(%1 - %2) < 1hour", "abs(%1 - %2) >= 1hour",
	"abs(%1 - %2) < 1day", "abs(%1 - %2) >= 1day",
	"abs(%1 - %2) < 1week", "abs(%1 - %2) >= 1week",
};

QAction *addItem(QMenu *menu, const QString &title, int command)
{
	QAction *action = menu->addAction(title);
	action->setData(command);
	return action;
}

/** Hour, Day, Week, Month and Year, then the custom range. */
void addDateItems(QMenu *menu, int first, int range)
{
	int command = first;
	for (const DateGroup &group : kDateGroups)
	{
		QMenu *sub = menu->addMenu(text(group.title));
		for (const char *item : group.items)
			addItem(sub, text(item), command++);
	}
	addItem(menu, text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Custom Range...")), range);
}

/** The condition template with its property, or its two. */
QString condition(const char *pattern, const QString &first, const QString &second = QString())
{
	return second.isEmpty() ? QString::fromLatin1(pattern).arg(first)
		: QString::fromLatin1(pattern).arg(first, second);
}

} // namespace

FileFilterMenu::FileFilterMenu(QWidget *parent)
	: FileFilterMenu(MainMenu, parent)
{
}

FileFilterMenu::FileFilterMenu(Contents contents, QWidget *parent)
	: QMenu(parent)
{
	if (contents == MainMenu)
		build();
	connect(this, &QMenu::triggered, this, &FileFilterMenu::picked);
	connect(this, &QMenu::aboutToShow, this, &FileFilterMenu::showState);
	showState();
}

FileFilterMenu *FileFilterMenu::appendColumnFilter(QMenu *popup, const QString &column,
	bool threeWay)
{
	// what filters by a column: an item that asks for the condition, or a
	// submenu (IDR_POPUP_FILTERMENU_SIZE, IDR_POPUP_FILTERMENU_DATE).
	// Upstream's table goes on with columns this folder list does not
	// have: extension, binary, difference counts, creation time,
	// attributes, encoding, line ends, unpacker and prediffer
	enum Kind { AskItem, SizeMenu, DateMenu };
	struct Mapping
	{
		const char *suffix;
		Kind kind;
		int command;
	};
	static const Mapping mappings[] = {
		{ "Name", AskItem, FileName },
		{ "Path", AskItem, RelativeFolder },
		{ "Status", AskItem, ComparisonResult },
		{ "StatusAbbr", AskItem, ComparisonResult },
		{ "mtime", DateMenu, 0 },
		{ "size", SizeMenu, 0 },
		{ "sizeShort", SizeMenu, 0 },
	};
	const auto lookUp = [](const QString &name) -> const Mapping * {
		for (const Mapping &mapping : mappings)
			if (name == QLatin1String(mapping.suffix))
				return &mapping;
		return nullptr;
	};

	// the whole name first, about any side; then the name behind the
	// letter of a side
	int side = 0;
	const Mapping *mapping = lookUp(column);
	if (mapping == nullptr && !column.isEmpty())
	{
		const QChar first = column.at(0);
		if (first == QLatin1Char('L') || first == QLatin1Char('M')
			|| first == QLatin1Char('R'))
		{
			side = first == QLatin1Char('L') ? 1 : first == QLatin1Char('M') ? 2 : 3;
			mapping = lookUp(column.mid(1));
		}
	}
	if (mapping == nullptr)
		return nullptr;

	auto *menu = new FileFilterMenu(ColumnMenu, popup);
	menu->m_targetSide = side;
	menu->m_threeWay = threeWay;
	if (!popup->isEmpty())
		popup->addSeparator();
	if (mapping->kind == AskItem)
	{
		const int command = mapping->command;
		QAction *action = popup->addAction(
			text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Filter by This Column...")));
		connect(action, &QAction::triggered, menu, [menu, command]() { menu->pick(command); });
		return menu;
	}
	menu->setTitle(text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Filter by This Column")));
	if (mapping->kind == SizeMenu)
	{
		for (int i = 0; i < 14; ++i)
			addItem(menu, text(kSizeTexts[i]), SizeFirst + i);
		addItem(menu, text(QT_TRANSLATE_NOOP("FileFilterMenu", "Custom Range...")), SizeRange);
	}
	else
	{
		addDateItems(menu, DateFirst, DateRange);
	}
	popup->addMenu(menu);
	return menu;
}

QWidget *FileFilterMenu::dialogParent() const
{
	return m_dialogParent != nullptr ? m_dialogParent : parentWidget();
}

void FileFilterMenu::pick(int command)
{
	const std::optional<QString> result = apply(command, m_source ? m_source() : QString());
	if (result.has_value())
		emit maskChosen(*result);
}

void FileFilterMenu::build()
{
	const QString customRange = text(QT_TRANSLATE_NOOP("FileFilterMenu", "Custom Range..."));

	addItem(this, text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Clear All")), MaskClear);
	addItem(this, text(QT_TRANSLATE_NOOP("FileFilterMenu", "Remove Last Filter &Group")),
		MaskRemoveLast);
	addItem(this, text(QT_TRANSLATE_NOOP("FileFilterMenu", "R&eset to Default (*.*)")),
		MaskAll);

	QMenu *excludeFile = addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "Add E&xclude File")));
	addItem(excludeFile, text(QT_TRANSLATE_NOOP("FileFilterMenu", "&System Files")), FileSys);
	addItem(excludeFile, text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Editor Backup Files")),
		FileBackup);
	addItem(excludeFile, text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Compiled Binary Files")),
		FileBin);
	addItem(excludeFile, text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Log Files")), FileLog);
	addItem(excludeFile, text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Temporary or Cache Files")),
		FileTemp);

	QMenu *excludeFolder = addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu",
		"Add Excl&ude Folder")));
	addItem(excludeFolder, text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Version Control")),
		FolderVcs);
	addItem(excludeFolder, text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Build Artifacts")),
		FolderBuild);
	addItem(excludeFolder, text(QT_TRANSLATE_NOOP("FileFilterMenu", "&IDE")), FolderIde);
	addSeparator();

	// --- conditions on the files ---
	QMenu *file = addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "Add &File Condition")));
	QMenu *fileSize = file->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "File &Size")));
	for (int i = 0; i < 14; ++i)
		addItem(fileSize, text(kSizeTexts[i]), SizeFirst + i);
	addItem(fileSize, customRange, SizeRange);
	addDateItems(file->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Last Modified"))),
		DateFirst, DateRange);
	QMenu *attributes = file->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Attributes")));
	const char *const attributeTexts[] = {
		QT_TRANSLATE_NOOP("FileFilterMenu", "Read-only files"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Not read-only files"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Hidden files"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Not hidden files"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "System files"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Not system files"),
	};
	for (int i = 0; i < 6; ++i)
		addItem(attributes, text(attributeTexts[i]), AttrFirst + i);
	QMenu *content = file->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "File &Content")));
	const char *const contentTexts[] = {
		QT_TRANSLATE_NOOP("FileFilterMenu", "Contains string..."),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Does not contain string..."),
		QT_TRANSLATE_NOOP("FileFilterMenu", "First line contains string..."),
		QT_TRANSLATE_NOOP("FileFilterMenu", "First line does not contain string..."),
		QT_TRANSLATE_NOOP("FileFilterMenu", "First 10 lines contain string..."),
		QT_TRANSLATE_NOOP("FileFilterMenu", "First 10 lines do not contain string..."),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Last 10 lines contain string..."),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Last 10 lines do not contain string..."),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Last line contains string..."),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Last line does not contain string..."),
	};
	for (int i = 0; i < 10; ++i)
		addItem(content, text(contentTexts[i]), ContentFirst + i);
	QMenu *lines = file->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Line Count")));
	const char *const lineTexts[] = {
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 10"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "10 or more"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 100"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "100 or more"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 1000"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "1000 or more"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 10000"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "10000 or more"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 100000"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "100000 or more"),
	};
	for (int i = 0; i < 10; ++i)
		addItem(lines, text(lineTexts[i]), LinesFirst + i);
	addItem(lines, customRange, LinesRange);

	// --- conditions on the folders ---
	QMenu *folder = addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "Add F&older Condition")));
	addDateItems(folder->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Last Modified"))),
		FolderDateFirst, FolderDateRange);
	folder->addSeparator();
	QMenu *files = folder->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Files")));
	addItem(files, text(QT_TRANSLATE_NOOP("FileFilterMenu", "0 files")), FolderFilesFirst);
	addItem(files, text(QT_TRANSLATE_NOOP("FileFilterMenu", "1 file or more")),
		FolderFilesFirst + 1);
	addItem(files, customRange, FolderFilesRange);
	QMenu *items = folder->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Items")));
	addItem(items, text(QT_TRANSLATE_NOOP("FileFilterMenu", "0 items")), FolderItemsFirst);
	addItem(items, text(QT_TRANSLATE_NOOP("FileFilterMenu", "1 item or more")),
		FolderItemsFirst + 1);
	addItem(items, customRange, FolderItemsRange);
	// (upstream's menu stops at "Less than 1GB" here: thirteen, not fourteen)
	QMenu *totalSize = folder->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Total Size")));
	for (int i = 0; i < 13; ++i)
		addItem(totalSize, text(kSizeTexts[i]), FolderTotalSizeFirst + i);
	addItem(totalSize, customRange, FolderTotalSizeRange);
	addItem(folder, text(QT_TRANSLATE_NOOP("FileFilterMenu", "&Recursive")),
		FolderStatsRecursive)->setCheckable(true);

	// the side the conditions above are about
	auto *sides = new QActionGroup(this);
	const char *const sideTexts[] = {
		QT_TRANSLATE_NOOP("FileFilterMenu", "Target: &Any (Left/Middle/Right)"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Target: &Left"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Target: &Middle"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Target: &Right"),
	};
	for (int i = 0; i < 4; ++i)
	{
		QAction *action = addItem(this, text(sideTexts[i]), ConditionAny + i);
		action->setCheckable(true);
		sides->addAction(action);
	}
	addSeparator();

	// --- conditions on the difference between two sides ---
	QMenu *difference = addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu",
		"Add &Difference Condition")));
	QMenu *diffSize = difference->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu",
		"File &Size")));
	for (int i = 0; i < 6; ++i)
		addItem(diffSize, text(kComparisonTexts[i]), DiffSizeFirst + i);
	diffSize->addSeparator()->setData(DiffSizeRange);
	const char *const diffSizeTexts[] = {
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 10B"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "10B or more"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 100B"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "100B or more"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 1KB"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "1KB or more"),
	};
	for (int i = 0; i < 6; ++i)
		addItem(diffSize, text(diffSizeTexts[i]), DiffSizeFirst + 6 + i);
	addItem(diffSize, customRange, DiffSizeRange);
	QMenu *diffDate = difference->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu",
		"&Last Modified")));
	for (int i = 0; i < 6; ++i)
		addItem(diffDate, text(kComparisonTexts[i]), DiffDateFirst + i);
	diffDate->addSeparator()->setData(DiffDateRange);
	const char *const diffDateTexts[] = {
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 1 second"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "1 second or more"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 1 minute"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "1 minute or more"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 1 hour"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "1 hour or more"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 1 day"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "1 day or more"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Less than 1 week"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "1 week or more"),
	};
	for (int i = 0; i < 10; ++i)
		addItem(diffDate, text(diffDateTexts[i]), DiffDateFirst + 6 + i);
	addItem(diffDate, customRange, DiffDateRange);
	QMenu *diffAttributes = difference->addMenu(text(QT_TRANSLATE_NOOP("FileFilterMenu",
		"&Attributes")));
	addItem(diffAttributes, text(kComparisonTexts[0]), DiffAttrEqual);
	addItem(diffAttributes, text(kComparisonTexts[1]), DiffAttrNotEqual);

	// the sides a difference is taken between
	auto *pairs = new QActionGroup(this);
	const char *const pairTexts[] = {
		QT_TRANSLATE_NOOP("FileFilterMenu", "Target: &Left and Right"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Target: Left and &Middle"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Target: Middle and &Right"),
		QT_TRANSLATE_NOOP("FileFilterMenu", "Target: &All"),
	};
	for (int i = 0; i < 4; ++i)
	{
		QAction *action = addItem(this, text(pairTexts[i]), ConditionDiffLeftRight + i);
		action->setCheckable(true);
		pairs->addAction(action);
	}
}

/** The ticks follow the targets, and with every side as the target of a
    difference only "equal" and "not equal" make sense. */
void FileFilterMenu::showState()
{
	const bool allSides = m_targetDiffSide == 3;
	const QList<QAction *> all = findChildren<QAction *>();
	for (QAction *action : all)
	{
		if (action->menu() != nullptr || !action->data().isValid())
			continue;
		const int command = action->data().toInt();
		if (command >= ConditionAny && command <= ConditionRight)
			action->setChecked(command - ConditionAny == m_targetSide);
		else if (command >= ConditionDiffLeftRight && command <= ConditionDiffAll)
			action->setChecked(command - ConditionDiffLeftRight == m_targetDiffSide);
		else if (command == FolderStatsRecursive)
			action->setChecked(m_recursive);
		else if ((command >= DiffSizeLess && command <= DiffSizeRange)
			|| (command >= DiffDateLess && command <= DiffDateRange))
			action->setVisible(!allSides); // the separators carry a range's command
	}
}

QAction *FileFilterMenu::actionForTest(int command) const
{
	const QList<QAction *> all = findChildren<QAction *>();
	for (QAction *action : all)
		if (action->menu() == nullptr && !action->isSeparator() && action->data().isValid()
			&& action->data().toInt() == command)
			return action;
	return nullptr;
}

void FileFilterMenu::pickForTest(int command)
{
	if (QAction *action = actionForTest(command))
		action->trigger();
}

void FileFilterMenu::picked(QAction *action)
{
	if (action == nullptr || action->isSeparator() || !action->data().isValid())
		return;
	const int command = action->data().toInt();
	if (command >= ConditionAny && command <= ConditionRight)
		m_targetSide = command - ConditionAny;
	else if (command >= ConditionDiffLeftRight && command <= ConditionDiffAll)
		m_targetDiffSide = command - ConditionDiffLeftRight;
	else if (command == FolderStatsRecursive)
		m_recursive = !m_recursive;
	else
	{
		pick(command);
		return;
	}
	showState();
	emit reopenRequested();
}

QString FileFilterMenu::sidePrefix() const
{
	static const char *const sides[] = { "", "Left", "Middle", "Right" };
	return QLatin1String(sides[qBound(0, m_targetSide, 3)]);
}

QString FileFilterMenu::diffSidePrefix(int index) const
{
	static const char *const first[] = { "Left", "Left", "Middle" };
	static const char *const second[] = { "Right", "Middle", "Right" };
	if (m_targetDiffSide < 0 || m_targetDiffSide > 2)
		return QString();
	return QLatin1String(index == 0 ? first[m_targetDiffSide] : second[m_targetDiffSide]);
}

QString FileFilterMenu::folderProperty(const QString &name) const
{
	return m_recursive ? QStringLiteral("Recursive") + name : name;
}

std::optional<QString> FileFilterMenu::askCondition(const QString &masks, const QString &prefix,
	bool difference, const QString &field, const QString &defaultOperator,
	const QString &transform, bool recursive)
{
	FilterConditionDialog dialog(difference, difference ? m_targetDiffSide : m_targetSide,
		field, QString(), defaultOperator, transform, recursive, dialogParent());
	if (dialog.exec() != QDialog::Accepted)
		return std::nullopt;
	return (masks.isEmpty() ? masks : masks + QLatin1Char('|')) + prefix + dialog.expression();
}

std::optional<QString> FileFilterMenu::apply(int command, const QString &masks)
{
	// more names for the group in use, or a condition as a group of its own
	const auto withNames = [&masks](const char *names) {
		return (masks.isEmpty() ? masks : masks + QLatin1Char(';')) + QLatin1String(names);
	};
	const auto withGroup = [&masks](const QString &prefix, const QString &expression) {
		return (masks.isEmpty() ? masks : masks + QLatin1Char('|')) + prefix + expression;
	};
	const QString fe = QStringLiteral("fe:");
	const QString de = QStringLiteral("de:");
	const QString equals = QStringLiteral("%1 = %2");
	const QString same = QStringLiteral("%1");
	const auto allEqual = [](const QString &property, bool negated) {
		return (negated ? QStringLiteral("not allequal(") : QStringLiteral("allequal("))
			+ property + QLatin1Char(')');
	};

	switch (command)
	{
	case MaskClear:
		return QString();
	case MaskRemoveLast:
	{
		std::vector<String> groups = FileFilterHelper::SplitFilterGroups(masks.toStdString());
		groups.pop_back();
		return QString::fromStdString(FileFilterHelper::JoinFilterGroups(groups));
	}
	case MaskAll:
		return QStringLiteral("*.*");
	case FileSys:
		return withNames("!pagefile.sys;!hiberfil.sys;!swapfile.sys;!Thumbs.db;!desktop.ini");
	case FileBackup:
		return withNames("!*.bak;!*.old;!*.orig;!*.swp;!*.swo;!*.tmp;!*.temp;!*.save;"
			"!*.backup;!*.*~");
	case FileBin:
		return withNames("!*.exe;!*.dll;!*.ocx;!*.sys;!*.drv;!*.cpl;!*.scr;!*.com;!*.jar;"
			"!*.war;!*.obj;!*.o;!*.lib;!*.so;!*.a;!*.class;!*.pyc;!*.pyo");
	case FileLog:
		return withNames("!*.log;!*.out;!*.err;!*.trace");
	case FileTemp:
		return withNames("!*.tmp;!*.temp;!*.cache;!*.dmp;!*.swp");
	case FolderVcs:
		return withNames("!.git\\;!.svn\\;!.hg\\");
	case FolderBuild:
		return withNames("!build\\;!bin\\;!obj\\;!out\\;!dist\\;!release\\;!debug\\;"
			"!target\\;!temp\\;!cache\\");
	case FolderIde:
		return withNames("!.vs\\;!.idea\\;!.vscode\\;!.metadata\\;!.settings\\");
	case SizeRange:
		return askCondition(masks, fe, false, QStringLiteral("Size"), equals, same);
	case DateRange:
		return askCondition(masks, fe, false, QStringLiteral("DateStr"), equals, same);
	case LinesRange:
		return askCondition(masks, fe, false, QStringLiteral("Content"),
			QStringLiteral("%1 > %2"), QStringLiteral("lineCount(%1)"));
	case FolderDateRange:
		return askCondition(masks, de, false, QStringLiteral("DateStr"), equals, same);
	case FolderFilesRange:
		return askCondition(masks, de, false, QStringLiteral("Files"), equals, same,
			m_recursive);
	case FolderItemsRange:
		return askCondition(masks, de, false, QStringLiteral("Items"), equals, same,
			m_recursive);
	case FolderTotalSizeRange:
		return askCondition(masks, de, false, QStringLiteral("TotalSize"), equals, same,
			m_recursive);
	case DiffSizeRange:
		return askCondition(masks, fe, true, QStringLiteral("Size"), equals,
			QStringLiteral("abs(%1 - %2)"));
	case DiffDateRange:
		return askCondition(masks, fe, true, QStringLiteral("Date"), equals,
			QStringLiteral("abs(%1 - %2)"));
	case FileName:
		return askCondition(masks, fe, false, QStringLiteral("Name"),
			QStringLiteral("%1 contains %2"), same);
	case RelativeFolder:
		return askCondition(masks, fe, false, QStringLiteral("Folder"),
			QStringLiteral("%1 contains %2"), same);
	case ComparisonResult:
	{
		ComparisonResultFilterDialog dialog(m_threeWay, dialogParent());
		if (dialog.exec() != QDialog::Accepted)
			return std::nullopt;
		return withGroup(fe, dialog.expression());
	}
	case DiffAttrEqual:
	case DiffAttrNotEqual:
		if (m_targetDiffSide == 3)
			return withGroup(fe, allEqual(QStringLiteral("AttrStr"), command == DiffAttrNotEqual));
		return withGroup(fe, condition(command == DiffAttrEqual ? "%1 = %2" : "%1 != %2",
			diffSidePrefix(0) + QStringLiteral("AttrStr"),
			diffSidePrefix(1) + QStringLiteral("AttrStr")));
	default:
		break;
	}

	if (command >= SizeFirst && command <= SizeLast)
		return withGroup(fe, condition(kSizeConditions[command - SizeFirst],
			sidePrefix() + QStringLiteral("Size")));
	if (command >= DateFirst && command <= DateLast)
		return withGroup(fe, condition(kDateConditions[command - DateFirst],
			sidePrefix() + QStringLiteral("Date")));
	if (command >= AttrFirst && command <= AttrLast)
		return withGroup(fe, condition(kAttrConditions[command - AttrFirst],
			sidePrefix() + QStringLiteral("AttrStr")));
	if (command >= ContentFirst && command <= ContentLast)
	{
		const int index = command - ContentFirst;
		return askCondition(masks, fe, false, QStringLiteral("Content"),
			index % 2 == 0 ? QStringLiteral("%1 contains %2")
				: QStringLiteral("%1 not contains %2"),
			QLatin1String(kContentTransforms[index]));
	}
	if (command >= LinesFirst && command <= LinesLast)
		return withGroup(fe, condition(kLineCountConditions[command - LinesFirst],
			sidePrefix() + QStringLiteral("Content")));
	if (command >= FolderDateFirst && command <= FolderDateLast)
		return withGroup(de, condition(kDateConditions[command - FolderDateFirst],
			sidePrefix() + QStringLiteral("Date")));
	if (command >= FolderFilesFirst && command <= FolderFilesLast)
		return withGroup(de, condition(kCountConditions[command - FolderFilesFirst],
			sidePrefix() + folderProperty(QStringLiteral("Files"))));
	if (command >= FolderItemsFirst && command <= FolderItemsLast)
		return withGroup(de, condition(kCountConditions[command - FolderItemsFirst],
			sidePrefix() + folderProperty(QStringLiteral("Items"))));
	if (command >= FolderTotalSizeFirst && command <= FolderTotalSizeLast)
		return withGroup(de, condition(kSizeConditions[command - FolderTotalSizeFirst],
			sidePrefix() + folderProperty(QStringLiteral("TotalSize"))));
	if (command >= DiffSizeFirst && command <= DiffSizeLast)
	{
		if (m_targetDiffSide == 3)
			return withGroup(fe, allEqual(QStringLiteral("Size"),
				command == DiffSizeFirst + 1));
		return withGroup(fe, condition(kDiffSizeConditions[command - DiffSizeFirst],
			diffSidePrefix(0) + QStringLiteral("Size"),
			diffSidePrefix(1) + QStringLiteral("Size")));
	}
	if (command >= DiffDateFirst && command <= DiffDateLast)
	{
		if (m_targetDiffSide == 3)
			return withGroup(fe, allEqual(QStringLiteral("Date"),
				command == DiffDateFirst + 1));
		return withGroup(fe, condition(kDiffDateConditions[command - DiffDateFirst],
			diffSidePrefix(0) + QStringLiteral("Date"),
			diffSidePrefix(1) + QStringLiteral("Date")));
	}
	return std::nullopt;
}
