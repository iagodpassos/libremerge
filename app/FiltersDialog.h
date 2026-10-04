// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

#include "LineFiltersList.h"
#include "SubstitutionFiltersList.h"

class QCheckBox;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * WinMerge's Tools > Filters dialog (CFiltersPropertySheet) with its Line
 * Filters and Substitution Filters pages (LineFiltersDlg and
 * SubstitutionFiltersDlg), control for control; its File Filters page is
 * not here yet. The dialog edits copies of the filters: once it is
 * accepted the caller compares them with the ones in use and saves them
 * (CMainFrame::OnToolsFilters).
 */
class FiltersDialog : public QDialog
{
	Q_OBJECT
public:
	/** The pages, numbered as WinMerge's sheet has them (0 is its File
	    Filters page): the number kept as "FilterStartPage". */
	enum Page
	{
		LineFiltersPage = 1,
		SubstitutionFiltersPage = 2,
	};

	explicit FiltersDialog(QWidget *parent = nullptr);

	/** The filters as the dialog was accepted with. */
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

private:
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
