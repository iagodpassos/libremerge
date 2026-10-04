// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <QDialog>

#include "LineFiltersList.h"
#include "SubstitutionFiltersList.h"

class FileFilterCombo;
class FileFilterHelper;
class FileFilterMenu;
class QCheckBox;
class QFileSystemWatcher;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * WinMerge's Tools > Filters dialog (CFiltersPropertySheet) with its File
 * Filters, Line Filters and Substitution Filters pages (FileFiltersDlg,
 * LineFiltersDlg and SubstitutionFiltersDlg), control for control. The
 * dialog edits copies of the filters: once it is accepted the caller
 * compares them with the ones in use and saves them
 * (CMainFrame::OnToolsFilters).
 */
class FiltersDialog : public QDialog
{
	Q_OBJECT
public:
	/** The pages, in the sheet's order: the number kept as
	    "FilterStartPage". */
	enum Page
	{
		FileFiltersPage = 0,
		LineFiltersPage = 1,
		SubstitutionFiltersPage = 2,
	};

	explicit FiltersDialog(QWidget *parent = nullptr);
	~FiltersDialog() override;

	/** The filters as the dialog was accepted with. */
	const FileFilterHelper &fileFilter() const { return *m_fileFilter; }
	bool lineFiltersEnabled() const { return m_lineFiltersEnabled; }
	const LineFiltersList &lineFilters() const { return m_lineFilters; }
	const SubstitutionFiltersList &substitutionFilters() const
	{
		return m_substitutionFilters;
	}

	/** Each page's OnApply: the lists are read from the controls and
	    tested, and an expression that does not compile keeps the dialog
	    open on its page. */
	void accept() override;

	void showPage(int page);
	int currentPage() const;

	/** Stand-ins for the file dialogs of New and Install, and for the
	    editor Edit and New open (for tests; an empty function is the real
	    thing). The chooser gets whether it is a save dialog and the
	    folder it starts in, and returns the path picked, or nothing. */
	static void setFileChooserForTest(
		std::function<QString(bool save, const QString &folder)> chooser);
	static void setEditorForTest(std::function<void(const QString &path)> editor);

private:
	QWidget *buildFileFiltersPage();
	void fillPresetList();
	QTreeWidgetItem *presetRow(const QString &path) const;
	void showPresetErrors();
	bool checkPresets(const QStringList &names);
	void presetsToMask();
	void selectPreset(const QString &path);
	void updateFileButtons();
	void testFileFilter();
	void installFileFilter();
	void newFileFilter();
	void editSelectedFileFilter();
	void deleteSelectedFileFilter();
	void editFileFilter(const QString &path);
	void applyFileFilters();
	QWidget *buildLineFiltersPage();
	QWidget *buildSubstitutionFiltersPage();
	QTreeWidgetItem *addLineFilterRow(const QString &filter, bool enabled);
	QTreeWidgetItem *addSubstitutionRow(const SubstitutionFilter &filter);
	void editSelectedLineFilter();
	void removeSelected(QTreeWidget *list);
	void updateLineButtons();
	void updateSubstitutionButtons();
	bool applyLineFilters();
	bool applySubstitutionFilters();

	QTabWidget *m_tabs = nullptr;
	// File Filters
	std::shared_ptr<FileFilterHelper> m_fileFilter;
	FileFilterCombo *m_maskCombo = nullptr;
	FileFilterMenu *m_maskMenu = nullptr;
	QTreeWidget *m_presetList = nullptr;
	QPushButton *m_btnEditPreset = nullptr;
	QPushButton *m_btnDeletePreset = nullptr;
	QFileSystemWatcher *m_presetWatcher = nullptr;
	bool m_checkingPresets = false;
	// Line Filters
	QCheckBox *m_chkLineFilters = nullptr;
	QTreeWidget *m_lineList = nullptr;
	QPushButton *m_btnEditLine = nullptr;
	QPushButton *m_btnRemoveLine = nullptr;
	// Substitution Filters
	QCheckBox *m_chkSubstitutions = nullptr;
	QTreeWidget *m_substitutionList = nullptr;
	QPushButton *m_btnRemoveSubstitution = nullptr;
	QPushButton *m_btnClearSubstitutions = nullptr;

	bool m_lineFiltersEnabled = false;
	LineFiltersList m_lineFilters;
	SubstitutionFiltersList m_substitutionFilters;
};
