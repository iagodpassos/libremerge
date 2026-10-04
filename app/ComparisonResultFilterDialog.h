// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QRadioButton;

/**
 * WinMerge's "Filter by Comparison Result" dialog
 * (CComparisonResultFilterDlg), what the Comparison result column of the
 * folder list filters by: the results to keep, or to leave out, as one
 * condition of a filter expression.
 */
class ComparisonResultFilterDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ComparisonResultFilterDialog(bool threeWay, QWidget *parent = nullptr);

	/** The condition, once the dialog was accepted. */
	QString expression() const { return m_expression; }

	/** OK takes at least one result: with none ticked it does nothing. */
	void accept() override;

private:
	QString buildExpression() const;

	bool m_threeWay;
	QString m_expression;
	QRadioButton *m_exclude;
	QCheckBox *m_identical;
	QCheckBox *m_different;
	QCheckBox *m_leftOnly;
	QCheckBox *m_rightOnly;
	QCheckBox *m_skipped;
	QCheckBox *m_middleOnly;
	QCheckBox *m_leftOnlyDifferent;
	QCheckBox *m_middleOnlyDifferent;
	QCheckBox *m_rightOnlyDifferent;
	QCheckBox *m_leftOnlyMissing;
	QCheckBox *m_middleOnlyMissing;
	QCheckBox *m_rightOnlyMissing;
};
