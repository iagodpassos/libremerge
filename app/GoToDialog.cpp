// SPDX-License-Identifier: GPL-3.0-or-later
#include "GoToDialog.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace
{

std::function<bool(GoToDialog *)> &presenter()
{
	static std::function<bool(GoToDialog *)> handler;
	return handler;
}

} // namespace

GoToDialog::GoToDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Go to"));
	auto *grid = new QGridLayout(this);

	// "Go to:", the range the number may take, and the number (digits only,
	// as upstream's ES_NUMBER field)
	auto *label = new QLabel(tr("G&o to:"), this);
	m_range = new QLabel(this);
	m_range->setObjectName(QStringLiteral("gotoRange"));
	m_number = new QLineEdit(this);
	m_number->setObjectName(QStringLiteral("gotoNumber"));
	m_number->setValidator(new QRegularExpressionValidator(
		QRegularExpression(QStringLiteral("[0-9]*")), m_number));
	label->setBuddy(m_number);
	grid->addWidget(label, 0, 0);
	grid->addWidget(m_range, 0, 1);
	grid->addWidget(m_number, 0, 2);

	// the file, and what to go to
	auto *fileBox = new QGroupBox(tr("File"), this);
	auto *fileColumn = new QVBoxLayout(fileBox);
	auto *fileGroup = new QButtonGroup(this);
	const QString fileTexts[3] = { tr("&Left"), tr("&Middle"), tr("&Right") };
	for (int i = 0; i < 3; ++i)
	{
		m_files[i] = new QRadioButton(fileTexts[i], fileBox);
		fileGroup->addButton(m_files[i], i);
		fileColumn->addWidget(m_files[i]);
	}
	auto *whatBox = new QGroupBox(tr("Go to what"), this);
	auto *whatColumn = new QVBoxLayout(whatBox);
	auto *whatGroup = new QButtonGroup(this);
	m_toLine = new QRadioButton(tr("Li&ne"), whatBox);
	m_toDifference = new QRadioButton(tr("&Difference"), whatBox);
	whatGroup->addButton(m_toLine, 0);
	whatGroup->addButton(m_toDifference, 1);
	whatColumn->addWidget(m_toLine);
	whatColumn->addWidget(m_toDifference);
	whatColumn->addStretch(1);
	grid->addWidget(fileBox, 1, 0);
	grid->addWidget(whatBox, 1, 1);

	auto *buttons = new QVBoxLayout;
	buttons->addStretch(1);
	m_go = new QPushButton(tr("&Go to"), this);
	m_go->setObjectName(QStringLiteral("gotoGo"));
	m_go->setDefault(true);
	auto *cancel = new QPushButton(tr("Cancel"), this);
	buttons->addWidget(m_go);
	buttons->addWidget(cancel);
	grid->addLayout(buttons, 1, 2);
	connect(m_go, &QPushButton::clicked, this, &QDialog::accept);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

	// OnChangeParam, and OnBnClicked for either group
	connect(m_number, &QLineEdit::textChanged, this, [this]() { updateGoButton(); });
	for (QButtonGroup *group : { fileGroup, whatGroup })
		connect(group, &QButtonGroup::idClicked, this, [this]() {
			updateRange();
			updateGoButton();
		});
}

/** The values upstream's caller sets, and OnInitDialog: no middle file in
    a 2-way comparison, no differences to go to when there are none. */
void GoToDialog::init(const QString &number, int file, int files,
	const std::array<int, 3> &lastLines, int lastDifference)
{
	m_lastLines = lastLines;
	m_lastDifference = lastDifference;
	m_files[qBound(0, file, 2)]->setChecked(true);
	m_toLine->setChecked(true);
	m_files[1]->setEnabled(files >= 3);
	m_toDifference->setEnabled(lastDifference != 0);
	m_number->setText(number);
	m_number->selectAll();
	updateRange();
	updateGoButton();
}

int GoToDialog::number() const
{
	bool ok = false;
	const int value = m_number->text().toInt(&ok);
	return ok ? value : 0;
}

int GoToDialog::file() const
{
	for (int i = 0; i < 3; ++i)
		if (m_files[i]->isChecked())
			return i;
	return 0;
}

bool GoToDialog::goesToLine() const
{
	return !m_toDifference->isChecked();
}

/** GetRangeMax: the last line of the file chosen, or the last difference. */
int GoToDialog::rangeMax() const
{
	return goesToLine() ? m_lastLines[file()] : m_lastDifference;
}

/** UpdateRange: "(1-N)", nothing when there is nothing to go to. */
void GoToDialog::updateRange()
{
	const int max = rangeMax();
	m_range->setText(max > 0 ? QStringLiteral("(1-%1)").arg(max) : QString());
}

/** UpdateGoToButton: Go to for a number within the range only. */
void GoToDialog::updateGoButton()
{
	const int value = number();
	m_go->setEnabled(value > 0 && value <= rangeMax());
}

bool GoToDialog::run(GoToDialog *dialog)
{
	if (presenter())
		return presenter()(dialog);
	return dialog->exec() == QDialog::Accepted;
}

void GoToDialog::setPresenterForTest(std::function<bool(GoToDialog *dialog)> handler)
{
	presenter() = std::move(handler);
}
