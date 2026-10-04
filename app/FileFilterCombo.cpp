// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileFilterCombo.h"

#include <QEvent>
#include <QLineEdit>
#include <QTimer>
#include <QToolTip>

#include "FileFilters.h"

namespace
{

// CValidatingEdit: the text is parsed this long after the last change
const int kCheckDelayMs = 700;

} // namespace

FileFilterCombo::FileFilterCombo(QWidget *parent)
	: QComboBox(parent)
{
	setEditable(true);
	setInsertPolicy(QComboBox::NoInsert);
	// no completion from the list: a mask is typed, not looked up
	setCompleter(nullptr);
	setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	setMinimumContentsLength(24);
	lineEdit()->installEventFilter(this);

	m_timer = new QTimer(this);
	m_timer->setSingleShot(true);
	m_timer->setInterval(kCheckDelayMs);
	connect(m_timer, &QTimer::timeout, this, &FileFilterCombo::checkNow);
	connect(this, &QComboBox::editTextChanged, m_timer, qOverload<>(&QTimer::start));
}

void FileFilterCombo::setChecker(std::function<QStringList(const QString &)> checker)
{
	m_checker = std::move(checker);
}

void FileFilterCombo::setMask(const QString &mask, bool listed)
{
	if (listed)
	{
		int index = findText(mask, Qt::MatchExactly | Qt::MatchCaseSensitive);
		if (index < 0)
		{
			insertItem(0, mask);
			index = 0;
		}
		setCurrentIndex(index);
	}
	setEditText(mask);
	checkNow();
}

void FileFilterCombo::loadHistory()
{
	const QString text = currentText();
	clear();
	addItems(lm::fileFilterHistory());
	setEditText(text);
}

void FileFilterCombo::saveHistory()
{
	const QString text = currentText();
	lm::rememberFileFilter(text);
	loadHistory();
	setEditText(text);
}

void FileFilterCombo::checkNow()
{
	m_timer->stop();
	const QStringList before = m_errors;
	m_errors = m_checker ? m_checker(currentText()) : QStringList();
	if (m_errors != before)
		showState();
	emit checked();
}

void FileFilterCombo::showState()
{
	// CValidatingEdit's colors: a red tint, darker on a dark window
	if (m_errors.isEmpty())
	{
		lineEdit()->setStyleSheet(QString());
		lineEdit()->setToolTip(QString());
		QToolTip::hideText();
		return;
	}
	const bool dark = palette().color(QPalette::Base).lightness() < 128;
	lineEdit()->setStyleSheet(dark
		? QStringLiteral("QLineEdit { background: #502828; }")
		: QStringLiteral("QLineEdit { background: #ffc8c8; }"));
	const QString message = m_errors.join(QLatin1Char('\n'));
	lineEdit()->setToolTip(message);
	// shown under the field at once, as its balloon is
	if (isVisible() && lineEdit()->hasFocus())
		QToolTip::showText(mapToGlobal(QPoint(0, height())), message, this);
}

bool FileFilterCombo::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == lineEdit() && event->type() == QEvent::FocusOut)
	{
		checkNow();
		emit focusLeft();
	}
	return QComboBox::eventFilter(watched, event);
}
