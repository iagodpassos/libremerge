// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge port layer: the encoders Qt does not have and FreeImage gives
// WinIMerge. Upstream saves an image comparison's pages back into their
// file through FreeImage's multi-page bitmaps (fipMultiPage::saveU): an
// animated GIF with its frames' delays and its looping, a multi-page TIFF
// page by page. Qt writes neither (no GIF encoder at all, a TIFF of one
// page only), so these write them:
//   - GIF89a, each frame on the whole canvas with its own color table: the
//     frame's colors as they are when they fit 256 (one of them kept for
//     transparency), a median cut of them otherwise; LZW as GIF has it;
//     the delays and the NETSCAPE2.0 loop count of an animation; a frame
//     followed by one with transparent pixels clears the canvas for it
//   - TIFF, a page per directory, 8-bit RGB (RGBA when a page has
//     transparency), each page one Deflate-compressed strip
// Both read back through Qt's own decoders.

#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QSaveFile>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <climits>

namespace lm_imgwrite
{

// --- GIF --------------------------------------------------------------------

/** A frame's color table and every pixel's index into it. */
struct IndexedFrame
{
	QVector<QRgb> colors;  // 256 entries, the unused ones black
	int transparent = -1;  // the index transparent pixels take, or none
	QByteArray indices;    // a byte per pixel, row after row
};

/** Median cut: the box of colors with the widest channel range is split at
    its weighted median, until there are as many boxes as colors wanted;
    each box gives the weighted mean of its colors. */
inline QVector<QRgb> medianCut(const QHash<QRgb, int> &histogram, int wanted)
{
	struct Entry { QRgb color; int count; };
	using Box = QVector<Entry>;
	QVector<Box> boxes(1);
	boxes[0].reserve(histogram.size());
	for (auto it = histogram.cbegin(); it != histogram.cend(); ++it)
		boxes[0].append({ it.key(), it.value() });
	const auto channel = [](QRgb color, int c) {
		return c == 0 ? qRed(color) : c == 1 ? qGreen(color) : qBlue(color);
	};
	while (boxes.size() < wanted)
	{
		int widest = -1, widestChannel = 0, widestRange = 0;
		for (int b = 0; b < boxes.size(); ++b)
		{
			if (boxes[b].size() < 2)
				continue;
			for (int c = 0; c < 3; ++c)
			{
				int low = 255, high = 0;
				for (const Entry &e : boxes[b])
				{
					low = std::min(low, channel(e.color, c));
					high = std::max(high, channel(e.color, c));
				}
				if (high - low > widestRange)
				{
					widestRange = high - low;
					widest = b;
					widestChannel = c;
				}
			}
		}
		if (widest < 0)
			break; // every box holds one color
		Box &box = boxes[widest];
		std::sort(box.begin(), box.end(), [&](const Entry &x, const Entry &y) {
			return channel(x.color, widestChannel) < channel(y.color, widestChannel);
		});
		qint64 total = 0;
		for (const Entry &e : box)
			total += e.count;
		qint64 running = 0;
		int split = 1;
		for (int i = 0; i < box.size() - 1; ++i)
		{
			running += box[i].count;
			split = i + 1;
			if (running * 2 >= total)
				break;
		}
		Box upper(box.begin() + split, box.end());
		box.resize(split);
		boxes.append(upper);
	}
	QVector<QRgb> palette;
	palette.reserve(boxes.size());
	for (const Box &box : boxes)
	{
		qint64 r = 0, g = 0, b = 0, n = 0;
		for (const Entry &e : box)
		{
			r += qint64(qRed(e.color)) * e.count;
			g += qint64(qGreen(e.color)) * e.count;
			b += qint64(qBlue(e.color)) * e.count;
			n += e.count;
		}
		if (n > 0)
			palette.append(qRgb(int(r / n), int(g / n), int(b / n)));
	}
	return palette;
}

/** A frame as GIF holds it: a pixel under half opaque is transparent, the
    others take their color, or the nearest one of the table. */
inline IndexedFrame indexFrame(const QImage &source)
{
	const QImage image = source.convertToFormat(QImage::Format_ARGB32);
	IndexedFrame frame;
	QHash<QRgb, int> histogram;
	bool anyTransparent = false;
	for (int y = 0; y < image.height(); ++y)
	{
		const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
		for (int x = 0; x < image.width(); ++x)
		{
			if (qAlpha(line[x]) < 128)
				anyTransparent = true;
			else
				++histogram[line[x] | 0xff000000u];
		}
	}
	const int room = anyTransparent ? 255 : 256;
	QVector<QRgb> palette;
	if (histogram.size() <= room)
		palette = histogram.keys();
	else
		palette = medianCut(histogram, room);
	std::sort(palette.begin(), palette.end());
	QHash<QRgb, uchar> index;
	for (int i = 0; i < palette.size(); ++i)
		index.insert(palette[i], uchar(i));
	if (anyTransparent)
		frame.transparent = palette.size();
	frame.colors = palette;
	while (frame.colors.size() < 256)
		frame.colors.append(qRgb(0, 0, 0));
	frame.indices.resize(qsizetype(image.width()) * image.height());
	uchar *out = reinterpret_cast<uchar *>(frame.indices.data());
	for (int y = 0; y < image.height(); ++y)
	{
		const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
		for (int x = 0; x < image.width(); ++x)
		{
			if (qAlpha(line[x]) < 128)
			{
				*out++ = uchar(frame.transparent);
				continue;
			}
			const QRgb color = line[x] | 0xff000000u;
			auto found = index.constFind(color);
			if (found == index.constEnd())
			{
				// the nearest color of the table, kept for the next pixel
				int best = 0, bestDistance = INT_MAX;
				for (int i = 0; i < palette.size(); ++i)
				{
					const int dr = qRed(color) - qRed(palette[i]);
					const int dg = qGreen(color) - qGreen(palette[i]);
					const int db = qBlue(color) - qBlue(palette[i]);
					const int distance = dr * dr + dg * dg + db * db;
					if (distance < bestDistance)
					{
						bestDistance = distance;
						best = i;
					}
				}
				found = index.insert(color, uchar(best));
			}
			*out++ = found.value();
		}
	}
	return frame;
}

/** GIF's LZW over 8-bit indices: codes from 9 to 12 bits, written low bit
    first, the table cleared when full. */
inline QByteArray lzwEncode(const QByteArray &indices)
{
	constexpr int minCodeSize = 8;
	const int clearCode = 1 << minCodeSize;
	QByteArray out;
	quint32 bits = 0;
	int bitCount = 0;
	int codeSize = minCodeSize + 1;
	const auto writeCode = [&](int code) {
		bits |= quint32(code) << bitCount;
		bitCount += codeSize;
		while (bitCount >= 8)
		{
			out.append(char(bits & 0xff));
			bits >>= 8;
			bitCount -= 8;
		}
	};
	QHash<quint32, int> table;
	int maxCode = clearCode + 1;
	writeCode(clearCode);
	if (indices.isEmpty())
	{
		writeCode(clearCode + 1);
		if (bitCount > 0)
			out.append(char(bits & 0xff));
		return out;
	}
	int current = uchar(indices.at(0));
	for (qsizetype i = 1; i < indices.size(); ++i)
	{
		const int next = uchar(indices.at(i));
		const quint32 key = (quint32(current) << 8) | quint32(next);
		const auto found = table.constFind(key);
		if (found != table.constEnd())
		{
			current = found.value();
			continue;
		}
		writeCode(current);
		table.insert(key, ++maxCode);
		if (maxCode >= (1 << codeSize))
			++codeSize;
		if (maxCode == 4095)
		{
			writeCode(clearCode);
			table.clear();
			codeSize = minCodeSize + 1;
			maxCode = clearCode + 1;
		}
		current = next;
	}
	writeCode(current);
	writeCode(clearCode + 1);
	if (bitCount > 0)
		out.append(char(bits & 0xff));
	return out;
}

inline void put16(QByteArray &out, int value)
{
	out.append(char(value & 0xff));
	out.append(char((value >> 8) & 0xff));
}

/** Frames of the same size, the delay of each in milliseconds, and how
    many times an animation plays (-1 for ever, 0 once: no loop block).
    One frame makes a still GIF. */
inline bool writeGif(const QString &path, const QList<QImage> &frames,
	const QList<int> &delaysMs, int loopCount)
{
	if (frames.isEmpty() || frames.first().isNull())
		return false;
	const int width = frames.first().width();
	const int height = frames.first().height();
	const bool animated = frames.size() > 1;
	QByteArray out("GIF89a");
	put16(out, width);
	put16(out, height);
	out.append(char(0x00)); // no global table: each frame brings its own
	out.append(char(0x00)); // background color
	out.append(char(0x00)); // aspect ratio
	if (animated && loopCount != 0)
	{
		out.append(QByteArray::fromHex("21ff0b"));
		out.append("NETSCAPE2.0");
		out.append(char(0x03));
		out.append(char(0x01));
		put16(out, loopCount < 0 ? 0 : loopCount);
		out.append(char(0x00));
	}
	// every frame indexed first: whether a frame clears the canvas once shown
	// (disposal 2) goes by the next one, whose transparent pixels must show
	// through to nothing rather than to the frame before
	QList<IndexedFrame> indexed;
	for (const QImage &source : frames)
		indexed.append(indexFrame(source.size() == QSize(width, height) ? source
			: source.copy(0, 0, width, height)));
	for (int f = 0; f < indexed.size(); ++f)
	{
		IndexedFrame &frame = indexed[f];
		const bool clears = animated && indexed.at((f + 1) % indexed.size()).transparent >= 0;
		// decoders clear to nothing only for a frame that has a transparent
		// color (Qt's fills with the background color otherwise): an unused
		// entry of the table takes the part
		if (clears && frame.transparent < 0)
		{
			// (the first entry no pixel takes)
			int used = 0;
			for (const char index : frame.indices)
				used = std::max(used, int(uchar(index)) + 1);
			if (used < 256)
				frame.transparent = used;
		}
		if (animated || frame.transparent >= 0)
		{
			const int disposal = clears ? 2 : 1;
			out.append(QByteArray::fromHex("21f904"));
			out.append(char((disposal << 2) | (frame.transparent >= 0 ? 1 : 0)));
			put16(out, (delaysMs.value(f, 0) + 5) / 10);
			out.append(char(frame.transparent >= 0 ? frame.transparent : 0));
			out.append(char(0x00));
		}
		out.append(char(0x2c));
		put16(out, 0);
		put16(out, 0);
		put16(out, width);
		put16(out, height);
		out.append(char(0x87)); // a local table of 256 colors
		for (const QRgb color : frame.colors)
		{
			out.append(char(qRed(color)));
			out.append(char(qGreen(color)));
			out.append(char(qBlue(color)));
		}
		out.append(char(8)); // LZW minimum code size
		const QByteArray data = lzwEncode(frame.indices);
		for (qsizetype at = 0; at < data.size(); at += 255)
		{
			const qsizetype length = std::min<qsizetype>(255, data.size() - at);
			out.append(char(length));
			out.append(data.constData() + at, length);
		}
		out.append(char(0x00));
	}
	out.append(char(0x3b));
	QSaveFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(out) == out.size() && file.commit();
}

// --- TIFF -------------------------------------------------------------------

inline void put32(QByteArray &out, quint32 value)
{
	for (int i = 0; i < 4; ++i)
		out.append(char((value >> (8 * i)) & 0xff));
}

inline void set32(QByteArray &out, qsizetype at, quint32 value)
{
	for (int i = 0; i < 4; ++i)
		out[at + i] = char((value >> (8 * i)) & 0xff);
}

/** The pages as a little-endian multi-page TIFF. */
inline bool writeTiff(const QString &path, const QList<QImage> &pages)
{
	if (pages.isEmpty())
		return false;
	QByteArray out("II");
	put16(out, 42);
	qsizetype nextIfdAt = out.size(); // where the next directory's offset goes
	put32(out, 0);
	for (int p = 0; p < pages.size(); ++p)
	{
		const QImage image = pages.at(p).convertToFormat(QImage::Format_ARGB32);
		if (image.isNull())
			return false;
		bool alpha = false;
		for (int y = 0; y < image.height() && !alpha; ++y)
		{
			const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
			for (int x = 0; x < image.width() && !alpha; ++x)
				alpha = qAlpha(line[x]) != 255;
		}
		const int samples = alpha ? 4 : 3;
		QByteArray raw;
		raw.reserve(qsizetype(image.width()) * image.height() * samples);
		for (int y = 0; y < image.height(); ++y)
		{
			const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
			for (int x = 0; x < image.width(); ++x)
			{
				raw.append(char(qRed(line[x])));
				raw.append(char(qGreen(line[x])));
				raw.append(char(qBlue(line[x])));
				if (alpha)
					raw.append(char(qAlpha(line[x])));
			}
		}
		// Deflate (Compression 8) takes a zlib stream: qCompress's, without
		// the length Qt puts before it
		const QByteArray strip = qCompress(raw, 6).mid(4);

		if (out.size() % 2)
			out.append(char(0));
		const quint32 stripAt = quint32(out.size());
		out.append(strip);
		if (out.size() % 2)
			out.append(char(0));
		const quint32 bitsAt = quint32(out.size());
		for (int s = 0; s < samples; ++s)
			put16(out, 8);
		const quint32 resolutionAt = quint32(out.size());
		put32(out, 72);
		put32(out, 1);

		struct Entry { quint16 tag; quint16 type; quint32 count; quint32 value; };
		const quint16 SHORT = 3, LONG = 4, RATIONAL = 5;
		QList<Entry> entries = {
			{ 254, LONG, 1, pages.size() > 1 ? 2u : 0u },  // a page of many
			{ 256, LONG, 1, quint32(image.width()) },
			{ 257, LONG, 1, quint32(image.height()) },
			{ 258, SHORT, quint32(samples), bitsAt },
			{ 259, SHORT, 1, 8 },                           // Deflate
			{ 262, SHORT, 1, 2 },                           // RGB
			{ 273, LONG, 1, stripAt },
			{ 277, SHORT, 1, quint32(samples) },
			{ 278, LONG, 1, quint32(image.height()) },
			{ 279, LONG, 1, quint32(strip.size()) },
			{ 282, RATIONAL, 1, resolutionAt },
			{ 283, RATIONAL, 1, resolutionAt },
			{ 284, SHORT, 1, 1 },                           // chunky
			{ 296, SHORT, 1, 2 },                           // inches
			{ 297, SHORT, 2, quint32(p) | (quint32(pages.size()) << 16) },
		};
		if (alpha)
			entries.append({ 338, SHORT, 1, 2 });           // unassociated alpha
		const quint32 ifdAt = quint32(out.size());
		set32(out, nextIfdAt, ifdAt);
		put16(out, int(entries.size()));
		for (const Entry &e : entries)
		{
			put16(out, e.tag);
			put16(out, e.type);
			put32(out, e.count);
			if (e.type == SHORT && e.count == 1)
			{
				put16(out, int(e.value));
				put16(out, 0);
			}
			else
				put32(out, e.value);
		}
		nextIfdAt = out.size();
		put32(out, 0);
	}
	QSaveFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(out) == out.size() && file.commit();
}

} // namespace lm_imgwrite
