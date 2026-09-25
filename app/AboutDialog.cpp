// SPDX-License-Identifier: GPL-3.0-or-later
#include "AboutDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSysInfo>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace
{

// ASCII Gnu by Przemyslaw Borys, https://www.gnu.org/graphics/gnu-ascii.html
// Copyright (c) 2001 Free Software Foundation, Inc.
// Available under the GNU GPL version 2 or any later version.
// (WinMerge's About box carries the same drawing.)
const char *const kGnuAscii[] = {
	"  ,           ,",
	" /             \\",
	"((__-^^-,-^^-__))",
	" `-_---' `---_-'",
	"  `--|o` 'o|--'",
	"     \\  `  /",
	"      ): :(",
	"      :o_o:",
	"       \"-\"",
};

const QColor kTeal(0x2b, 0x6c, 0x80);
const QColor kTealLight(0x4f, 0xa8, 0xc2);
const QColor kOrange(0xd9, 0x7c, 0x00);

/** "2026.09" from the compiler's build date, WinMerge's STRYEARMONTH. */
QString buildYearMonth()
{
	static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	const QString date = QString::fromLatin1(__DATE__); // "Sep 25 2026"
	const int index = QString::fromLatin1(months).indexOf(date.left(3)) / 3 + 1;
	return QStringLiteral("%1.%2").arg(date.right(4))
		.arg(index, 2, 10, QLatin1Char('0'));
}

/** The artwork strip at the top: wordmark, a brand panel rising to the
    right with the application icon, the GNU head, and the version on a
    plate cut into the bottom-right corner, after WinMerge's splash. */
class AboutBanner : public QWidget
{
public:
	explicit AboutBanner(QWidget *parent) : QWidget(parent)
	{
		setFixedSize(620, 300);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		const bool dark = palette().color(QPalette::Window).lightness() < 128;
		const QColor base = dark ? QColor(0x24, 0x24, 0x24) : QColor(Qt::white);
		const QColor ink = dark ? QColor(0xe6, 0xe6, 0xe6) : QColor(0x20, 0x20, 0x20);
		const qreal w = width();
		const qreal h = height();
		p.fillRect(rect(), base);

		QPainterPath panel;
		panel.moveTo(0, h * 0.36);
		panel.lineTo(w * 0.62, h * 0.36);
		panel.lineTo(w * 0.74, h * 0.12);
		panel.lineTo(w, h * 0.12);
		panel.lineTo(w, h);
		panel.lineTo(0, h);
		panel.closeSubpath();
		QLinearGradient gradient(0, h * 0.12, w, h);
		gradient.setColorAt(0, QColor(0x3d, 0x8d, 0xa6));
		gradient.setColorAt(1, QColor(0x1a, 0x45, 0x54));
		p.fillPath(panel, gradient);

		// faint rings on the panel, standing in for WinMerge's gear
		p.save();
		p.setClipPath(panel);
		QPen ring(QColor(255, 255, 255, 22));
		ring.setWidthF(9);
		p.setPen(ring);
		p.setBrush(Qt::NoBrush);
		for (int r = 38; r < 280; r += 34)
			p.drawEllipse(QPointF(w * 0.42, h * 0.74), r, r);
		p.restore();

		QFont mark = font();
		mark.setPixelSize(56);
		mark.setWeight(QFont::Black);
		p.setFont(mark);
		const QFontMetricsF markMetrics(mark);
		const QString libre = QStringLiteral("Libre");
		p.setPen(dark ? kTealLight : kTeal);
		p.drawText(QPointF(24, 72), libre);
		p.setPen(kOrange);
		p.drawText(QPointF(24 + markMetrics.horizontalAdvance(libre), 72),
			QStringLiteral("Merge"));

		QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		mono.setPixelSize(14);
		p.setFont(mono);
		p.setPen(QColor(255, 255, 255, 140));
		const QFontMetricsF monoMetrics(mono);
		qreal y = h * 0.42 + monoMetrics.ascent();
		for (const char *line : kGnuAscii)
		{
			p.drawText(QPointF(44, y), QString::fromLatin1(line));
			y += monoMetrics.lineSpacing();
		}

		const QPixmap icon(QStringLiteral(":/about/icon.png"));
		const qreal size = 184;
		p.drawPixmap(QRectF(w - size - 26, h * 0.13, size, size), icon,
			QRectF(icon.rect()));

		QPainterPath plate;
		plate.moveTo(w * 0.53, h);
		plate.lineTo(w * 0.59, h * 0.79);
		plate.lineTo(w, h * 0.79);
		plate.lineTo(w, h);
		plate.closeSubpath();
		p.fillPath(plate, base);
		QFont versionFont = font();
		versionFont.setPixelSize(16);
		p.setFont(versionFont);
		p.setPen(ink);
		p.drawText(QRectF(w * 0.61, h * 0.79, w * 0.39 - 16, h * 0.21),
			Qt::AlignLeft | Qt::AlignVCenter,
			AboutDialog::versionText() + QLatin1Char('\n')
				+ AboutDialog::platformText());
	}
};

} // namespace

AboutDialog::AboutDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("About LibreMerge"));

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(new AboutBanner(this));

	const QString link = QStringLiteral("<a href=\"%1\">%2</a>");
	auto *notice = new QLabel(this);
	notice->setWordWrap(true);
	notice->setTextFormat(Qt::RichText);
	notice->setOpenExternalLinks(true);
	notice->setContentsMargins(20, 14, 20, 6);
	notice->setMaximumWidth(620);
	notice->setText(
		QStringLiteral("<p>%1</p><p>%2<br/>%3</p>").arg(
			tr("LibreMerge comes with ABSOLUTELY NO WARRANTY. It is free "
			   "software and can be redistributed under the conditions of "
			   "the %1, version 3 or later.")
				.arg(link.arg(QStringLiteral(
					"https://github.com/iagodpassos/libremerge/blob/main/LICENSE"),
					tr("GNU General Public License"))),
			tr("(c) 2026 Iago Passos and the LibreMerge contributors."),
			tr("Based on the comparison engine of %1, (c) 1996-2026 Dean P. "
			   "Grimm / Thingamahoochie Software and the WinMerge contributors "
			   "(GPL-2.0-or-later). Not affiliated with or endorsed by the "
			   "WinMerge project.")
				.arg(link.arg(QStringLiteral("https://winmerge.org/"),
					QStringLiteral("WinMerge")))));
	layout->addWidget(notice);

	auto *buttons = new QHBoxLayout;
	buttons->setContentsMargins(16, 6, 16, 16);
	auto *contributors = new QPushButton(tr("Contributors"), this);
	contributors->setAutoDefault(false);
	connect(contributors, &QPushButton::clicked, this,
		&AboutDialog::showContributors);
	buttons->addWidget(contributors);
	buttons->addStretch(1);
	auto *homepage = new QLabel(link.arg(homepageUrl().toString(),
		tr("Visit the LibreMerge page on GitHub!")), this);
	homepage->setOpenExternalLinks(true);
	buttons->addWidget(homepage);
	buttons->addStretch(1);
	auto *ok = new QPushButton(tr("OK"), this);
	ok->setDefault(true);
	connect(ok, &QPushButton::clicked, this, &QDialog::accept);
	buttons->addWidget(ok);
	layout->addLayout(buttons);

	layout->setSizeConstraint(QLayout::SetFixedSize);
}

QString AboutDialog::versionText()
{
	return tr("Version %1 (%2)").arg(QApplication::applicationVersion(),
		buildYearMonth());
}

QString AboutDialog::platformText()
{
#if defined(Q_OS_MACOS)
	const QString platform = QStringLiteral("macOS");
#elif defined(Q_OS_LINUX)
	const QString platform = QStringLiteral("Linux");
#else
	const QString platform = QSysInfo::kernelType();
#endif
	return platform + QLatin1Char(' ') + QSysInfo::buildCpuArchitecture();
}

QUrl AboutDialog::homepageUrl()
{
	return QUrl(QStringLiteral("https://github.com/iagodpassos/libremerge"));
}

QString AboutDialog::contributorsMarkdown()
{
	QFile file(QStringLiteral(":/about/CONTRIBUTORS.md"));
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return QString::fromUtf8(file.readAll());
}

void AboutDialog::showContributors()
{
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Contributors"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *browser = new QTextBrowser(&dialog);
	browser->setOpenExternalLinks(true);
	browser->setMarkdown(contributorsMarkdown());
	browser->document()->setDocumentMargin(14);
	layout->addWidget(browser);
	auto *box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(box);
	dialog.resize(640, 600);
	dialog.exec();
}
