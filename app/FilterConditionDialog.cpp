// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilterConditionDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace
{

/** A value the way a filter expression quotes a string. */
QString quoted(QString value)
{
	value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
	return QLatin1Char('"') + value + QLatin1Char('"');
}

/** strutils::format_strings: "%1" to "%3" stand for the arguments, and
    any other character behind a "%" for itself. Not QString::arg, which
    would take a "%2" inside an argument for a place and complain about a
    pattern without one (the left-hand side can be a whole filter). */
QString formatted(const QString &pattern, const QStringList &arguments)
{
	QString result;
	for (int i = 0; i < pattern.size(); ++i)
	{
		if (pattern.at(i) != QLatin1Char('%'))
		{
			result += pattern.at(i);
			continue;
		}
		if (++i >= pattern.size())
			break;
		const int place = pattern.at(i).unicode() - '0';
		if (place > 0 && place <= arguments.size())
			result += arguments.at(place - 1);
		else
			result += pattern.at(i);
	}
	return result;
}

} // namespace

FilterConditionDialog::FilterConditionDialog(bool difference, int side, const QString &field,
	const QString &propertyName, const QString &defaultOperator, const QString &transform,
	bool recursive, QWidget *parent)
	: QDialog(parent)
	, m_difference(difference)
	, m_side(side)
	, m_field(field)
	, m_propertyName(propertyName)
	, m_transform(transform)
	, m_recursive(recursive)
{
	setObjectName(QStringLiteral("filterConditionDialog"));
	setWindowModality(Qt::WindowModal);
	setWindowTitle(tr("Filter Condition"));

	auto *layout = new QVBoxLayout(this);
	auto *grid = new QGridLayout;
	grid->addWidget(new QLabel(tr("Left-hand side:"), this), 0, 0);
	auto *lhs = new QLabel(leftHandSide(), this);
	lhs->setObjectName(QStringLiteral("conditionLhs"));
	lhs->setTextInteractionFlags(Qt::TextSelectableByMouse);
	grid->addWidget(lhs, 0, 1, 1, 2);

	grid->addWidget(new QLabel(tr("Operator:"), this), 1, 0);
	m_operator = new QComboBox(this);
	m_operator->setObjectName(QStringLiteral("conditionOperator"));
	grid->addWidget(m_operator, 1, 1);

	grid->addWidget(new QLabel(tr("Right-hand side:"), this), 2, 0);
	m_value1 = new QComboBox(this);
	m_value1->setObjectName(QStringLiteral("conditionValue1"));
	m_value2 = new QComboBox(this);
	m_value2->setObjectName(QStringLiteral("conditionValue2"));
	for (QComboBox *value : { m_value1, m_value2 })
	{
		value->setEditable(true);
		value->setInsertPolicy(QComboBox::NoInsert);
		value->setCompleter(nullptr);
		value->setMinimumContentsLength(12);
	}
	m_date1 = new QDateTimeEdit(QDateTime::currentDateTime(), this);
	m_date1->setObjectName(QStringLiteral("conditionDate1"));
	m_date2 = new QDateTimeEdit(QDateTime::currentDateTime(), this);
	m_date2->setObjectName(QStringLiteral("conditionDate2"));
	const QString dateFormat = usesDateTimePicker() ? QStringLiteral("yyyy-MM-dd HH:mm:ss")
		: QStringLiteral("yyyy-MM-dd");
	for (QDateTimeEdit *date : { m_date1, m_date2 })
	{
		date->setDisplayFormat(dateFormat);
		date->setCalendarPopup(true);
	}
	// the first value's place holds the combo or the date, as does the second's
	grid->addWidget(m_value1, 2, 1);
	grid->addWidget(m_value2, 2, 2);
	grid->addWidget(m_date1, 2, 1);
	grid->addWidget(m_date2, 2, 2);

	m_matchCase = new QCheckBox(tr("Match case"), this);
	m_matchCase->setObjectName(QStringLiteral("conditionMatchCase"));
	grid->addWidget(m_matchCase, 3, 0, 1, 3);

	grid->addWidget(new QLabel(tr("Expression:"), this), 4, 0, Qt::AlignTop);
	m_expressionLabel = new QLabel(this);
	m_expressionLabel->setObjectName(QStringLiteral("conditionExpression"));
	m_expressionLabel->setWordWrap(true);
	m_expressionLabel->setMinimumHeight(2 * fontMetrics().lineSpacing());
	m_expressionLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_expressionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	grid->addWidget(m_expressionLabel, 4, 1, 1, 2);
	grid->setColumnStretch(1, 1);
	grid->setColumnStretch(2, 1);
	layout->addLayout(grid);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &FilterConditionDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// the operators the kind of property takes, each with its template
	using Operators = QList<QPair<QString, QString>>;
	Operators operators;
	if (comparesNumbers() || isDuration() || usesDatePicker() || usesDateTimePicker())
	{
		operators = {
			{ tr("Equals"), QStringLiteral("%1 = %2") },
			{ tr("Does not equal"), QStringLiteral("%1 != %2") },
			{ tr("Less than"), QStringLiteral("%1 < %2") },
			{ tr("Less than or equal to"), QStringLiteral("%1 <= %2") },
			{ tr("Greater than or equal to"), QStringLiteral("%1 >= %2") },
			{ tr("Greater than"), QStringLiteral("%1 > %2") },
			{ tr("Between"), QStringLiteral("isWithin(%1, %2, %3)") },
			{ tr("Not Between"), QStringLiteral("not isWithin(%1, %2, %3)") },
		};
	}
	else if (m_field == QStringLiteral("Content"))
	{
		operators = {
			{ tr("Contains"), QStringLiteral("%1 contains %2") },
			{ tr("Not Contains"), QStringLiteral("%1 not contains %2") },
			{ tr("Contains (regex)"), QStringLiteral("%1 recontains %2") },
			{ tr("Not Contains (regex)"), QStringLiteral("%1 not recontains %2") },
		};
	}
	else if (isStringField(false))
	{
		operators = {
			{ tr("Equals"), QStringLiteral("%1 = %2") },
			{ tr("Does not equal"), QStringLiteral("%1 != %2") },
			{ tr("Match (wildcard)"), QStringLiteral("%1 like %2") },
			{ tr("Not match (wildcard)"), QStringLiteral("%1 not like %2") },
			{ tr("Match (regex)"), QStringLiteral("%1 matches %2") },
			{ tr("Not match (regex)"), QStringLiteral("%1 not matches %2") },
			{ tr("Contains"), QStringLiteral("%1 contains %2") },
			{ tr("Not Contains"), QStringLiteral("%1 not contains %2") },
			{ tr("Contains (regex)"), QStringLiteral("%1 recontains %2") },
			{ tr("Not Contains (regex)"), QStringLiteral("%1 not recontains %2") },
		};
	}
	for (const auto &entry : operators)
	{
		m_operator->addItem(entry.first, entry.second);
		if (entry.second == defaultOperator)
			m_operator->setCurrentIndex(m_operator->count() - 1);
	}

	// the values offered; anything else may be typed
	QStringList values;
	QString first;
	if (m_field == QStringLiteral("Size") || m_field == QStringLiteral("TotalSize"))
	{
		values = { QStringLiteral("0B"), QStringLiteral("1B"), QStringLiteral("10B"),
			QStringLiteral("100B"), QStringLiteral("1KB"), QStringLiteral("10KB"),
			QStringLiteral("100KB"), QStringLiteral("1MB"), QStringLiteral("10MB"),
			QStringLiteral("100MB"), QStringLiteral("1GB") };
		first = QStringLiteral("0B");
	}
	else if (isDuration())
	{
		values = { QStringLiteral("0second"), QStringLiteral("1second"),
			QStringLiteral("1minute"), QStringLiteral("1hour"), QStringLiteral("1day"),
			QStringLiteral("1week") };
		first = QStringLiteral("0second");
	}
	else if (comparesNumbers())
	{
		values = { QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("10"),
			QStringLiteral("100"), QStringLiteral("1000"), QStringLiteral("10000"),
			QStringLiteral("100000") };
		first = QStringLiteral("0");
	}
	for (QComboBox *value : { m_value1, m_value2 })
	{
		value->addItems(values);
		value->setEditText(first);
	}

	// "Match case" is for the text properties only
	m_matchCase->setVisible(isStringField()
		&& m_transform != QStringLiteral("toDateTime(%1)")
		&& m_transform != QStringLiteral("toNumber(%1)"));

	connect(m_operator, &QComboBox::currentIndexChanged, this,
		&FilterConditionDialog::operatorChanged);
	connect(m_matchCase, &QCheckBox::toggled, this, &FilterConditionDialog::showExpression);
	for (QComboBox *value : { m_value1, m_value2 })
		connect(value, &QComboBox::editTextChanged, this,
			&FilterConditionDialog::showExpression);
	for (QDateTimeEdit *date : { m_date1, m_date2 })
		connect(date, &QDateTimeEdit::dateTimeChanged, this,
			&FilterConditionDialog::showExpression);
	operatorChanged();
	resize(520, sizeHint().height());
}

/** GetLHS: the property of the side asked for, inside the transform. */
QString FilterConditionDialog::leftHandSide() const
{
	const auto property = [this](const QString &sidePrefix) {
		if (!m_propertyName.isEmpty())
			return sidePrefix + QStringLiteral("Prop(\"") + m_propertyName
				+ QStringLiteral("\")");
		return sidePrefix + (m_recursive && !m_difference ? QStringLiteral("Recursive")
			: QString()) + m_field;
	};
	if (!m_difference)
	{
		static const char *const sides[] = { "", "Left", "Middle", "Right" };
		return formatted(m_transform, { property(QLatin1String(sides[qBound(0, m_side, 3)])) });
	}
	static const char *const first[] = { "Left", "Left", "Middle" };
	static const char *const second[] = { "Right", "Middle", "Right" };
	const int pair = qBound(0, m_side, 2);
	return formatted(m_transform, { property(QLatin1String(first[pair])),
		property(QLatin1String(second[pair])) });
}

bool FilterConditionDialog::isStringField(bool includeContent) const
{
	if (includeContent && m_field == QStringLiteral("Content"))
		return true;
	return m_field == QStringLiteral("Name") || m_field == QStringLiteral("Folder")
		|| m_field == QStringLiteral("Extension") || m_field == QStringLiteral("Unpacker")
		|| m_field == QStringLiteral("Prediffer") || m_field == QStringLiteral("Line")
		|| m_field.startsWith(QStringLiteral("Column"));
}

/** The values go into the expression as they are, not as quoted text. */
bool FilterConditionDialog::comparesNumbers() const
{
	static const char *const fields[] = { "Size", "TotalSize", "Files", "Items",
		"Differences", "IgnoredDiffs", "LineLength", "LineNumber", "Codepage" };
	for (const char *field : fields)
		if (m_field == QLatin1String(field))
			return true;
	return m_transform == QStringLiteral("lineCount(%1)")
		|| m_transform.startsWith(QStringLiteral("matchNumber("))
		|| m_transform.startsWith(QStringLiteral("matchBlockNumber("))
		|| m_transform == QStringLiteral("toNumber(%1)")
		|| m_transform.startsWith(QStringLiteral("regexCount("));
}

/** A length of time between two dates ("1hour"). Upstream lists such
    values for the "Date" field but gives it no operator, so that its
    dialog cannot build anything: here it compares like a number. */
bool FilterConditionDialog::isDuration() const
{
	return m_field == QStringLiteral("Date");
}

bool FilterConditionDialog::usesDatePicker() const
{
	return m_field == QStringLiteral("DateStr") || m_transform == QStringLiteral("toDateStr(%1)");
}

bool FilterConditionDialog::usesDateTimePicker() const
{
	return m_transform == QStringLiteral("toDateTime(%1)");
}

/** GetExpression: the operator's template over the left-hand side and
    the values. */
QString FilterConditionDialog::currentExpression() const
{
	if (m_operator->currentIndex() < 0)
		return QString();
	const QString pattern = m_operator->currentData().toString();
	QString value1 = m_value1->currentText();
	QString value2 = m_value2->currentText();
	if (!comparesNumbers() && !isDuration())
	{
		if (usesDatePicker())
		{
			value1 = m_date1->date().toString(QStringLiteral("yyyy-MM-dd"));
			value2 = m_date2->date().toString(QStringLiteral("yyyy-MM-dd"));
		}
		else if (usesDateTimePicker())
		{
			value1 = m_date1->dateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
			value2 = m_date2->dateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
		}
		value1 = quoted(value1);
		value2 = quoted(value2);
		if (usesDateTimePicker())
		{
			value1.prepend(QLatin1Char('d'));
			value2.prepend(QLatin1Char('d'));
		}
	}
	// (not QString::arg, which would take a "%2" inside a value for a place)
	QString result;
	for (int i = 0; i < pattern.size(); ++i)
	{
		if (pattern.at(i) == QLatin1Char('%') && i + 1 < pattern.size())
		{
			const QChar place = pattern.at(i + 1);
			if (place == QLatin1Char('1') || place == QLatin1Char('2')
				|| place == QLatin1Char('3'))
			{
				result += place == QLatin1Char('1') ? leftHandSide()
					: place == QLatin1Char('2') ? value1 : value2;
				++i;
				continue;
			}
		}
		result += pattern.at(i);
	}
	// the directive that makes the comparison case sensitive
	if (m_matchCase->isVisibleTo(this) && m_matchCase->isChecked())
		result.prepend(QStringLiteral("@cs "));
	return result;
}

void FilterConditionDialog::operatorChanged()
{
	// a second value for the operators that take a range
	const bool second = m_operator->currentData().toString().contains(QStringLiteral("%3"));
	const bool dates = usesDatePicker() || usesDateTimePicker();
	m_value1->setVisible(!dates);
	m_value2->setVisible(!dates && second);
	m_date1->setVisible(dates);
	m_date2->setVisible(dates && second);
	showExpression();
}

void FilterConditionDialog::showExpression()
{
	m_expressionLabel->setText(currentExpression());
}

void FilterConditionDialog::accept()
{
	m_expression = currentExpression();
	QDialog::accept();
}
