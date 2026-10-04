// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComparisonResultFilterDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QStringList>
#include <QVBoxLayout>

ComparisonResultFilterDialog::ComparisonResultFilterDialog(bool threeWay, QWidget *parent)
	: QDialog(parent)
	, m_threeWay(threeWay)
{
	setObjectName(QStringLiteral("comparisonResultFilterDialog"));
	setWindowModality(Qt::WindowModal);
	setWindowTitle(tr("Filter by Comparison Result"));

	auto *layout = new QVBoxLayout(this);
	auto *mode = new QHBoxLayout;
	auto *include = new QRadioButton(tr("&Include"), this);
	include->setObjectName(QStringLiteral("resultInclude"));
	include->setChecked(true);
	m_exclude = new QRadioButton(tr("&Exclude"), this);
	m_exclude->setObjectName(QStringLiteral("resultExclude"));
	mode->addWidget(include);
	mode->addWidget(m_exclude);
	mode->addStretch(1);
	layout->addLayout(mode);

	auto *group = new QGroupBox(tr("Comparison Results"), this);
	auto *boxes = new QVBoxLayout(group);
	const auto add = [group, boxes](const QString &text, const char *name) {
		auto *box = new QCheckBox(text, group);
		box->setObjectName(QLatin1String(name));
		boxes->addWidget(box);
		return box;
	};
	m_identical = add(tr("Identical"), "resultIdentical");
	m_different = add(tr("Different"), "resultDifferent");
	m_leftOnly = add(tr("Left only"), "resultLeftOnly");
	m_rightOnly = add(tr("Right only"), "resultRightOnly");
	m_skipped = add(tr("Skipped"), "resultSkipped");
	m_middleOnly = add(tr("Middle only"), "resultMiddleOnly");
	boxes->addSpacing(12);
	m_leftOnlyDifferent = add(tr("Left only (different)"), "resultLeftOnlyDifferent");
	m_middleOnlyDifferent = add(tr("Middle only (different)"), "resultMiddleOnlyDifferent");
	m_rightOnlyDifferent = add(tr("Right only (different)"), "resultRightOnlyDifferent");
	boxes->addSpacing(12);
	m_leftOnlyMissing = add(tr("Left only (missing)"), "resultLeftOnlyMissing");
	m_middleOnlyMissing = add(tr("Middle only (missing)"), "resultMiddleOnlyMissing");
	m_rightOnlyMissing = add(tr("Right only (missing)"), "resultRightOnlyMissing");
	// the results only a comparison of three folders has
	for (QCheckBox *box : { m_middleOnly, m_leftOnlyDifferent, m_middleOnlyDifferent,
			m_rightOnlyDifferent, m_leftOnlyMissing, m_middleOnlyMissing,
			m_rightOnlyMissing })
		box->setVisible(threeWay);
	layout->addWidget(group);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &ComparisonResultFilterDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

void ComparisonResultFilterDialog::accept()
{
	m_expression = buildExpression();
	if (!m_expression.isEmpty())
		QDialog::accept();
}

/** BuildExpression: the ticked results, one condition each, joined by
    "or"; "not" in front of them all to leave them out. */
QString ComparisonResultFilterDialog::buildExpression() const
{
	QStringList conditions;
	if (m_identical->isChecked())
		conditions.append(QStringLiteral("Identical"));
	if (m_different->isChecked())
		conditions.append(QStringLiteral("Different"));
	if (m_skipped->isChecked())
		conditions.append(QStringLiteral("Skipped"));

	if (m_threeWay)
	{
		if (m_leftOnly->isChecked())
			conditions.append(QStringLiteral(
				"LeftExists and not MiddleExists and not RightExists"));
		if (m_rightOnly->isChecked())
			conditions.append(QStringLiteral(
				"not LeftExists and not MiddleExists and RightExists"));
		if (m_middleOnly->isChecked())
			conditions.append(QStringLiteral(
				"not LeftExists and MiddleExists and not RightExists"));
		if (m_leftOnlyDifferent->isChecked())
			conditions.append(QStringLiteral(
				"DifferentLeftMiddle and not DifferentMiddleRight"));
		if (m_middleOnlyDifferent->isChecked())
			conditions.append(QStringLiteral(
				"not DifferentLeftMiddle and DifferentMiddleRight and DifferentLeftRight"));
		// upstream writes "not DifferentLeftMiddle and DifferentMiddleRight
		// and not DifferentLeftRight" here, which no item can meet: the
		// engine reads those three names off the two bits that say which
		// side differs alone, and both are set when it is the right one
		if (m_rightOnlyDifferent->isChecked())
			conditions.append(QStringLiteral(
				"DifferentLeftMiddle and DifferentMiddleRight"));
		if (m_leftOnlyMissing->isChecked())
			conditions.append(QStringLiteral(
				"not LeftExists and MiddleExists and RightExists"));
		if (m_middleOnlyMissing->isChecked())
			conditions.append(QStringLiteral(
				"LeftExists and not MiddleExists and RightExists"));
		if (m_rightOnlyMissing->isChecked())
			conditions.append(QStringLiteral(
				"LeftExists and MiddleExists and not RightExists"));
	}
	else
	{
		if (m_leftOnly->isChecked())
			conditions.append(QStringLiteral("LeftExists and not RightExists"));
		if (m_rightOnly->isChecked())
			conditions.append(QStringLiteral("not LeftExists and RightExists"));
	}

	if (conditions.isEmpty())
		return QString();
	const bool exclude = m_exclude->isChecked();
	QString expression;
	if (conditions.size() == 1)
	{
		expression = conditions.first();
	}
	else
	{
		expression = QLatin1Char('(') + conditions.first() + QLatin1Char(')');
		for (int i = 1; i < conditions.size(); ++i)
			expression += QStringLiteral(" or (") + conditions.at(i) + QLatin1Char(')');
	}
	if (exclude)
		expression = QStringLiteral("not (") + expression + QLatin1Char(')');
	return expression;
}
