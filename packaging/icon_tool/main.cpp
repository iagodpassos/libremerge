// SPDX-License-Identifier: GPL-3.0-or-later
// Generates the LibreMerge app icon (original artwork, no WinMerge assets):
// two document panes with diff lines and merge arrows on a teal squircle.
// Usage: libremerge-icon-tool <output-iconset-dir>
#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

namespace
{

QImage renderIcon(int size)
{
	QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	const qreal s = size / 1024.0;
	p.scale(s, s);

	// macOS-style rounded square with margin
	QPainterPath squircle;
	squircle.addRoundedRect(QRectF(100, 100, 824, 824), 185, 185);
	QLinearGradient bg(100, 100, 100, 924);
	bg.setColorAt(0.0, QColor(0x2b, 0x6c, 0x80));
	bg.setColorAt(1.0, QColor(0x1a, 0x3f, 0x52));
	p.fillPath(squircle, bg);

	// two document panes
	auto drawPane = [&p](qreal x, const QColor &accent, bool accentSecond) {
		QPainterPath pane;
		pane.addRoundedRect(QRectF(x, 262, 264, 420), 28, 28);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0xf7, 0xf9, 0xfa));
		p.drawPath(pane);
		// text lines
		const qreal lineX = x + 36;
		const qreal lineW = 264 - 72;
		for (int i = 0; i < 6; ++i)
		{
			const qreal y = 262 + 52 + i * 58;
			QColor color(0xb9, 0xc4, 0xcc);
			if ((accentSecond && i == 2) || (!accentSecond && i == 1))
				color = accent;
			if (i == 4)
				color = QColor(0xff, 0xd1, 0x66); // shared "changed" line
			p.setBrush(color);
			p.drawRoundedRect(QRectF(lineX, y, i == 5 ? lineW * 0.6 : lineW, 26), 13, 13);
		}
	};
	drawPane(198, QColor(0x8f, 0xc7, 0xff), false);  // left pane, blue accent
	drawPane(562, QColor(0x7d, 0xd8, 0x9a), true);   // right pane, green accent

	// merge arrows between the panes
	p.setBrush(QColor(0xff, 0xb3, 0x3c));
	p.setPen(Qt::NoPen);
	auto drawArrow = [&p](qreal y, bool toRight) {
		QPainterPath arrow;
		const qreal x0 = toRight ? 448 : 576;
		const qreal dir = toRight ? 1.0 : -1.0;
		arrow.moveTo(x0, y - 18);
		arrow.lineTo(x0 + dir * 74, y - 18);
		arrow.lineTo(x0 + dir * 74, y - 44);
		arrow.lineTo(x0 + dir * 128, y);
		arrow.lineTo(x0 + dir * 74, y + 44);
		arrow.lineTo(x0 + dir * 74, y + 18);
		arrow.lineTo(x0, y + 18);
		arrow.closeSubpath();
		p.drawPath(arrow);
	};
	drawArrow(430, true);
	drawArrow(556, false);

	p.end();
	return image;
}

} // namespace

int main(int argc, char *argv[])
{
	QGuiApplication app(argc, argv);
	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <output-iconset-dir>\n", argv[0]);
		return 2;
	}
	const QString outDir = QString::fromLocal8Bit(argv[1]);
	QDir().mkpath(outDir);

	const QImage master = renderIcon(1024);
	struct Entry { const char *name; int size; };
	const Entry entries[] = {
		{ "icon_16x16.png", 16 }, { "icon_16x16@2x.png", 32 },
		{ "icon_32x32.png", 32 }, { "icon_32x32@2x.png", 64 },
		{ "icon_128x128.png", 128 }, { "icon_128x128@2x.png", 256 },
		{ "icon_256x256.png", 256 }, { "icon_256x256@2x.png", 512 },
		{ "icon_512x512.png", 512 }, { "icon_512x512@2x.png", 1024 },
	};
	for (const Entry &entry : entries)
	{
		const QImage scaled = entry.size == 1024 ? master
			: master.scaled(entry.size, entry.size,
				Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		if (!scaled.save(outDir + QLatin1Char('/') + QLatin1String(entry.name)))
		{
			fprintf(stderr, "failed to save %s\n", entry.name);
			return 1;
		}
	}
	return 0;
}
