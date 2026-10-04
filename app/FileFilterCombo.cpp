// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileFilterCombo.h"

#include <QEvent>
#include <QLineEdit>
#include <QTimer>
#include <QToolTip>

#include "FileFilters.h"
#include "Theme.h"

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

	m_historyKey = lm::fileFilterHistoryKey();
	m_timer = new QTimer(this);
	m_timer->setSingleShot(true);
	m_timer->setInterval(kCheckDelayMs);
	connect(m_timer, &QTimer::timeout, this, &FileFilterCombo::checkNow);
	connect(this, &QComboBox::editTextChanged, m_timer, qOverload<>(&QTimer::start));
	// CBN_EDITCHANGE and CBN_SELCHANGE: only what the user does to the
	// text makes it no longer the applied one, not a text set from code
	connect(lineEdit(), &QLineEdit::textEdited, this, [this]() { setApplied(false); });
	connect(this, &QComboBox::activated, this, [this]() { setApplied(false); });
	// the tints have a set of their own for the dark theme
	connect(lm::Theme::instance(), &lm::Theme::changed, this, &FileFilterCombo::themeChanged);
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
	addItems(lm::fileFilterHistory(m_historyKey));
	setEditText(text);
}

void FileFilterCombo::saveHistory()
{
	const QString text = currentText();
	lm::rememberFileFilter(text, m_historyKey);
	loadHistory();
	setEditText(text);
}

void FileFilterCombo::loadHistoryAndShowLatest()
{
	clear();
	addItems(lm::fileFilterHistory(m_historyKey));
	if (count() > 0)
		setCurrentIndex(0);
	else
		clearEditText();
}

void FileFilterCombo::setApplied(bool applied)
{
	if (m_applied == applied)
		return;
	m_applied = applied;
	showState();
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

/** CValidatingEdit's colors, darker on a dark window: a red tint for what
    does not parse, which comes before the pale yellow of a filter that is
    applied. */
void FileFilterCombo::showTint()
{
	// by the application's theme, not this field's palette: a field with
	// a style sheet on it is left out when a new palette is handed down
	const bool dark = lm::Theme::instance()->dark();
	QString style;
	if (!m_errors.isEmpty())
		style = dark ? QStringLiteral("QLineEdit { background: #502828; }")
			: QStringLiteral("QLineEdit { background: #ffc8c8; }");
	else if (m_applied)
		style = dark ? QStringLiteral("QLineEdit { background: #3c3c28; }")
			: QStringLiteral("QLineEdit { background: #ffffdc; }");
	if (lineEdit()->styleSheet() != style)
		lineEdit()->setStyleSheet(style);
}

void FileFilterCombo::showState()
{
	showTint();
	if (m_errors.isEmpty())
	{
		lineEdit()->setToolTip(QString());
		QToolTip::hideText();
		return;
	}
	const QString message = m_errors.join(QLatin1Char('\n'));
	lineEdit()->setToolTip(message);
	// shown under the field at once, as its balloon is
	if (isVisible() && lineEdit()->hasFocus())
		QToolTip::showText(mapToGlobal(QPoint(0, height())), message, this);
}

void FileFilterCombo::themeChanged()
{
	if (lineEdit()->styleSheet().isEmpty())
		return;
	// the tint comes off for the field to take the new theme's palette:
	// left out of the handing down, it kept the old one, which taking the
	// style sheet off puts back as if it had been set on purpose
	lineEdit()->setStyleSheet(QString());
	lineEdit()->setPalette(QPalette());
	setPalette(QPalette());
	showTint();
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
