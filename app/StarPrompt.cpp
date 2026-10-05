// SPDX-License-Identifier: GPL-3.0-or-later
#include "StarPrompt.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>

#include "ProjectLinks.h"

namespace
{

const QString kDays = QStringLiteral("StarPrompt/DaysUsed");
const QString kLastDay = QStringLiteral("StarPrompt/LastDay");
const QString kOpened = QStringLiteral("StarPrompt/Comparisons");
const QString kShown = QStringLiteral("StarPrompt/Sessions");
const QString kDone = QStringLiteral("StarPrompt/Done");

// the clear room under the card, part of the strip so that it goes with it
const int kGapBelow = 8;

} // namespace

namespace lm
{

StarPrompt *StarPrompt::instance()
{
	static StarPrompt prompt;
	return &prompt;
}

void StarPrompt::noteAppStarted(const QDate &today)
{
	QSettings settings;
	// (nothing is counted past what the request waits for, nor once it
	// was answered)
	const int days = settings.value(kDays).toInt();
	if (settings.value(kDone).toBool() || days >= kDaysOfUse)
		return;
	const QString day = today.toString(Qt::ISODate);
	if (settings.value(kLastDay).toString() == day)
		return;
	settings.setValue(kLastDay, day);
	settings.setValue(kDays, days + 1);
}

void StarPrompt::noteComparisonOpened()
{
	QSettings settings;
	const int opened = settings.value(kOpened).toInt();
	if (settings.value(kDone).toBool() || opened >= kComparisons)
		return;
	settings.setValue(kOpened, opened + 1);
}

bool StarPrompt::due() const
{
	const QSettings settings;
	if (settings.value(kDone).toBool())
		return false;
	if (settings.value(kDays).toInt() < kDaysOfUse
		|| settings.value(kOpened).toInt() < kComparisons)
		return false;
	// a session that has shown the strip keeps it; another one begins
	// only while there are sessions left
	return m_shownThisSession || settings.value(kShown).toInt() < kSessions;
}

void StarPrompt::noteShown()
{
	if (m_shownThisSession)
		return;
	m_shownThisSession = true;
	QSettings settings;
	settings.setValue(kShown, settings.value(kShown).toInt() + 1);
}

void StarPrompt::finish()
{
	QSettings().setValue(kDone, true);
	emit finished();
}

QWidget *StarPrompt::createBar(QWidget *parent)
{
	return due() ? new StarPromptBar(parent) : nullptr;
}

} // namespace lm

StarPromptBar::StarPromptBar(QWidget *parent)
	: QFrame(parent)
{
	setObjectName(QStringLiteral("starPrompt"));
	auto *row = new QHBoxLayout(this);
	row->setContentsMargins(12, 6, 6, 6 + kGapBelow);
	row->setSpacing(10);

	auto *star = new QLabel(QString(QChar(0x2605)), this);
	QPalette gold = star->palette();
	gold.setColor(QPalette::WindowText, QColor(0xd9, 0x8a, 0x00));
	star->setPalette(gold);
	row->addWidget(star);

	auto *text = new QLabel(tr("Enjoying LibreMerge? A star on GitHub helps other people "
		"find the project."), this);
	text->setObjectName(QStringLiteral("starPromptText"));
	text->setWordWrap(true);
	row->addWidget(text, 1);

	// the project's page, where the star is given: the answer "yes"
	auto *button = new QPushButton(tr("Star on GitHub"), this);
	button->setObjectName(QStringLiteral("starPromptButton"));
	button->setAutoDefault(false);
	// (room around the label: the Fusion style gives a button that is
	// not a dialog's default hardly any)
	button->setMinimumWidth(button->sizeHint().width() + 16);
	connect(button, &QPushButton::clicked, this, []() {
		lm::openProjectPage(lm::projectPage());
		lm::StarPrompt::instance()->finish();
	});
	row->addWidget(button);

	// and the answer "no"
	auto *close = new QToolButton(this);
	close->setObjectName(QStringLiteral("starPromptClose"));
	close->setText(QString::fromUtf8("\xE2\x9C\x95"));
	close->setToolTip(tr("Close"));
	close->setAccessibleName(tr("Close"));
	close->setAutoRaise(true);
	connect(close, &QToolButton::clicked, this,
		[]() { lm::StarPrompt::instance()->finish(); });
	row->addWidget(close);

	// either answer, given on any strip, takes every strip away
	connect(lm::StarPrompt::instance(), &lm::StarPrompt::finished, this, [this]() {
		hide();
		deleteLater();
	});
}

/** A quiet card in the colours of the theme in use: the window's own,
    a shade apart, with a faint edge. */
void StarPromptBar::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	QColor fill = palette().color(QPalette::Window);
	fill = fill.lightness() < 128 ? fill.lighter(130) : fill.darker(104);
	QColor edge = palette().color(QPalette::WindowText);
	edge.setAlpha(48);
	painter.setPen(edge);
	painter.setBrush(fill);
	painter.drawRoundedRect(
		QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5 - kGapBelow), 6, 6);
}

void StarPromptBar::showEvent(QShowEvent *event)
{
	QFrame::showEvent(event);
	lm::StarPrompt::instance()->noteShown();
}
