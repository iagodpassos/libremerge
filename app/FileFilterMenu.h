// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <QMenu>
#include <QString>

/**
 * WinMerge's file filter helper menu (CFileFilterHelperMenu over
 * IDR_POPUP_FILTERMENU): what the File Filters page's "=" button and the
 * arrow of the selection screen's "Select..." open. Each item makes a
 * change to the mask or filter expression of the field it belongs to:
 * clear it, leave ready-made sets of files or folders out, or add a
 * condition on the files, the folders or the difference between the
 * sides, some of them asked for in the Filter Condition dialog.
 *
 * Its "Additional Properties..." items are not here: they list the
 * Windows property system's properties.
 */
class FileFilterMenu : public QMenu
{
	Q_OBJECT
public:
	/** The commands, in the order of upstream's resource identifiers: a
	    run of them indexes a table of conditions. */
	enum Command
	{
		MaskClear,
		MaskRemoveLast,
		MaskAll,
		FileSys,
		FileBackup,
		FileBin,
		FileLog,
		FileTemp,
		FolderVcs,
		FolderBuild,
		FolderIde,
		// file conditions
		SizeFirst, ///< 14 of them: less than / at least 1KB ... 1GB
		SizeLast = SizeFirst + 13,
		SizeRange,
		DateFirst, ///< 22: hour, day, week, month, year
		DateLast = DateFirst + 21,
		DateRange,
		AttrFirst, ///< 6: read-only, hidden, system and their opposites
		AttrLast = AttrFirst + 5,
		ContentFirst, ///< 10: whole, first line, first 10, last 10, last line
		ContentLast = ContentFirst + 9,
		LinesFirst, ///< 10: less than / at least 10 ... 100000
		LinesLast = LinesFirst + 9,
		LinesRange,
		// folder conditions
		FolderDateFirst, ///< 22, as for files
		FolderDateLast = FolderDateFirst + 21,
		FolderDateRange,
		FolderFilesFirst, ///< 2: none, at least one
		FolderFilesLast = FolderFilesFirst + 1,
		FolderFilesRange,
		FolderItemsFirst,
		FolderItemsLast = FolderItemsFirst + 1,
		FolderItemsRange,
		FolderTotalSizeFirst, ///< 14, as file sizes
		FolderTotalSizeLast = FolderTotalSizeFirst + 13,
		FolderTotalSizeRange,
		FolderStatsRecursive,
		ConditionAny, ///< the side a file or folder condition is about
		ConditionLeft,
		ConditionMiddle,
		ConditionRight,
		// difference conditions
		DiffSizeFirst, ///< 12: the six comparisons, then by how much
		DiffSizeLess = DiffSizeFirst + 2,
		DiffSizeLast = DiffSizeFirst + 11,
		DiffSizeRange,
		DiffDateFirst, ///< 16: the six comparisons, then by how long
		DiffDateLess = DiffDateFirst + 2,
		DiffDateLast = DiffDateFirst + 15,
		DiffDateRange,
		DiffAttrEqual,
		DiffAttrNotEqual,
		ConditionDiffLeftRight, ///< the sides a difference is taken between
		ConditionDiffLeftMiddle,
		ConditionDiffMiddleRight,
		ConditionDiffAll,
	};

	explicit FileFilterMenu(QWidget *parent = nullptr);

	/** Where the mask to change is read from when an item is picked. */
	void setMaskSource(std::function<QString()> source) { m_source = std::move(source); }

	/** CFileFilterHelperMenu::OnCommand: what a command makes of a mask.
	    Nothing when its Filter Condition dialog was cancelled. */
	std::optional<QString> apply(int command, const QString &masks);

	/** Pick an item as a click on it would (for tests). */
	void pickForTest(int command);
	QAction *actionForTest(int command) const;

signals:
	/** An item changed the mask. */
	void maskChosen(const QString &mask);
	/** A target or the Recursive item was picked: upstream shows the menu
	    again at once, for the condition to follow. */
	void reopenRequested();

private:
	void build();
	void showState();
	void picked(QAction *action);
	QString sidePrefix() const;
	QString diffSidePrefix(int index) const;
	QString folderProperty(const QString &name) const;
	std::optional<QString> askCondition(const QString &masks, const QString &prefix,
		bool difference, const QString &field, const QString &defaultOperator,
		const QString &transform, bool recursive = false);

	std::function<QString()> m_source;
	int m_targetSide = 0;     ///< 0 any, 1 left, 2 middle, 3 right
	int m_targetDiffSide = 0; ///< 0 left and right, 1 left and middle, 2 middle and right, 3 all
	bool m_recursive = false;
};
