// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <QDialog>
#include <QString>

class FileFilterCombo;
class LineFilterHelper;
class QGridLayout;

/**
 * WinMerge's "Match Inside/Outside Filter Expressions" dialog
 * (CMatchInsideDlg): the two line filters a range goes between, the
 * lines from a match of the first to a match of the second. Each field
 * takes what the filter bar's does, checked as it is typed, with the
 * bar's history and the "=" menu.
 */
class MatchInsideDialog : public QDialog
{
	Q_OBJECT
public:
	/** The filters the fields start with; an empty one leaves a field
	    with the latest entry of the history. */
	MatchInsideDialog(const QString &filter1, const QString &filter2,
		QWidget *parent = nullptr);
	~MatchInsideDialog() override;

	QString filter1() const { return m_filter1; }
	QString filter2() const { return m_filter2; }

	void accept() override;

private:
	FileFilterCombo *addField(const QString &label, const char *name,
		LineFilterHelper *checker, const QString &initial, int row);

	std::unique_ptr<LineFilterHelper> m_checker1;
	std::unique_ptr<LineFilterHelper> m_checker2;
	QGridLayout *m_grid;
	FileFilterCombo *m_field1;
	FileFilterCombo *m_field2;
	QString m_filter1;
	QString m_filter2;
};
