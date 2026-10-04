// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QLabel;

/**
 * WinMerge's Filter Condition dialog (CFilterConditionDlg): one condition
 * of a filter expression, put together from an operator and one or two
 * values. What it compares, the left-hand side, comes from the menu item
 * that opened it: a property of the files of one side ("LeftSize"), or
 * of both sides of a difference ("abs(LeftSize - RightSize)").
 */
class FilterConditionDialog : public QDialog
{
	Q_OBJECT
public:
	/**
	 * @param difference the condition is on the two sides of a difference
	 * @param side which side (0 any, 1 left, 2 middle, 3 right), or for a
	 *        difference which pair (0 left and right, 1 left and middle,
	 *        2 middle and right)
	 * @param field the property: "Size", "DateStr", "Content", "Files"...
	 * @param propertyName a named property instead of the field ("Prop")
	 * @param defaultOperator the operator selected at first, as its
	 *        template ("%1 = %2")
	 * @param transform what is made of the property, "%1" for itself
	 *        ("lineCount(%1)", "abs(%1 - %2)")
	 * @param recursive the folder property counts the subfolders too
	 */
	FilterConditionDialog(bool difference, int side, const QString &field,
		const QString &propertyName, const QString &defaultOperator,
		const QString &transform, bool recursive, QWidget *parent = nullptr);

	/** The condition, once the dialog was accepted. */
	QString expression() const { return m_expression; }

	void accept() override;

private:
	QString leftHandSide() const;
	bool isStringField(bool includeContent = true) const;
	bool comparesNumbers() const;
	bool isDuration() const;
	bool usesDatePicker() const;
	bool usesDateTimePicker() const;
	QString currentExpression() const;
	void operatorChanged();
	void showExpression();

	bool m_difference;
	int m_side;
	QString m_field;
	QString m_propertyName;
	QString m_transform;
	bool m_recursive;
	QString m_expression;

	QComboBox *m_operator;
	QComboBox *m_value1;
	QComboBox *m_value2;
	QDateTimeEdit *m_date1;
	QDateTimeEdit *m_date2;
	QCheckBox *m_matchCase;
	QLabel *m_expressionLabel;
};
