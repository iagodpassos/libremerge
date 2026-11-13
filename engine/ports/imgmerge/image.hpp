// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge port layer: Qt-backed implementation of the WinIMerge image
// wrapper (upstream src/WinIMergeLib/image.hpp wraps FreeImagePlus, which
// is Windows-oriented in WinMerge and unmaintained as a system package).
// The API surface and pixel semantics expected by the vendored
// ImgDiffBuffer.hpp / ImgMergeBuffer.hpp are preserved:
//   - scanLine(y) is top-down (y == 0 is the top row) and rows are
//     contiguous 4-byte BGRA pixels once convertTo32Bits() ran
//   - Color is a Windows-layout RGBQUAD (blue byte first)
//   - copies are value copies (Qt's implicit sharing detaches on write)

#pragma once

#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include "ImageWriters.hpp"
#include <QTransform>
#include <QFileInfo>
#include <QString>
#include <algorithm>
#include <climits>
#include <map>
#include <string>
#include <vector>

// Tells the vendored DataForDiff that row 0 is first in memory
#define LM_IMAGE_SCANLINES_TOPDOWN 1

#ifndef _WIN32
typedef unsigned char BYTE;
typedef int BOOL;
typedef struct tagRGBQUAD
{
	BYTE rgbBlue;
	BYTE rgbGreen;
	BYTE rgbRed;
	BYTE rgbReserved;
} RGBQUAD;
#endif

class MultiPageImages;

class Image
{
	friend MultiPageImages;
public:
	typedef RGBQUAD Color;

	// Minimal stand-in for the fipImageEx accessor the vendored buffer
	// uses only to poke FreeImage's DIB cache; with Qt there is no cache
	// to invalidate, so everything degrades to a no-op.
	struct FipShim
	{
		explicit FipShim(Image *owner) : m_owner(owner) {}
		bool isTransparent() const { return false; }
		void setModified(bool) {}
		BOOL getPixelColor(unsigned x, unsigned y, RGBQUAD *color) const
		{
			if (!m_owner || x >= m_owner->width() || y >= m_owner->height())
				return 0;
			*color = m_owner->pixel(x, y);
			return 1;
		}
		BOOL setPixelColor(unsigned x, unsigned y, RGBQUAD *color)
		{
			if (!m_owner || x >= m_owner->width() || y >= m_owner->height())
				return 0;
			BYTE *line = m_owner->scanLine(y);
			line[x * 4 + 0] = color->rgbBlue;
			line[x * 4 + 1] = color->rgbGreen;
			line[x * 4 + 2] = color->rgbRed;
			line[x * 4 + 3] = color->rgbReserved;
			return 1;
		}
	private:
		Image *m_owner;
	};

	Image() : shim_(this) {}
	Image(int w, int h) : image_(w, h, QImage::Format_ARGB32), shim_(this)
	{
		image_.fill(Qt::transparent);
	}
	Image(const Image& other)
		: image_(other.image_), metadata_(other.metadata_),
		  formatHint_(other.formatHint_), shim_(this) {}
	explicit Image(const QImage& image) : image_(image), shim_(this) {}
	Image& operator=(const Image& other)
	{
		if (this != &other)
		{
			image_ = other.image_;
			metadata_ = other.metadata_;
			formatHint_ = other.formatHint_;
		}
		return *this;
	}

	BYTE *scanLine(int y) { return image_.scanLine(y); }
	const BYTE *scanLine(int y) const { return image_.constScanLine(y); }

	bool convertTo32Bits()
	{
		if (image_.isNull())
			return false;
		if (image_.format() != QImage::Format_ARGB32)
			image_ = image_.convertToFormat(QImage::Format_ARGB32);
		return !image_.isNull();
	}

	bool load(const std::wstring& filename)
	{
		// an untitled pane has no file: nothing to read (and no warning)
		if (filename.empty())
			return false;
		// vector files are the ImgConverter's, as upstream's FreeImage
		// reads neither: drawn there at the vector image zoom
		const QString suffix = QFileInfo(QString::fromStdWString(filename)).suffix().toLower();
		if (suffix == QLatin1String("svg") || suffix == QLatin1String("pdf"))
			return false;
		QImageReader reader(QString::fromStdWString(filename));
		reader.setAutoTransform(false);
		QImage img = reader.read();
		if (img.isNull())
			return false;
		image_ = img;
		formatHint_ = reader.format();
		// a plugin may not name the format it read (SVG's does not): the
		// file's extension says it then, so that the file is not taken for
		// one made in memory
		if (formatHint_.isEmpty())
			formatHint_ = QFileInfo(QString::fromStdWString(filename)).suffix().toLower().toLatin1();
		metadata_.clear();
		const char *orientation = exifOrientationString(reader.transformation());
		if (orientation)
			metadata_["EXIF_MAIN/Orientation"] = orientation;
		return true;
	}

	bool isSaveSupported() const
	{
		if (formatHint_.isEmpty())
			return true; // in-memory image, can be written as PNG etc.
		// a vector file is drawn into pixels as it reads (SVG, PDF): those
		// pixels do not go back into it, as upstream's WIC cannot write
		// them either; Save As offers PNG
		if (formatHint_ == "svg" || formatHint_ == "svgz" || formatHint_ == "pdf")
			return false;
		if (formatHint_ == "gif")
			return true; // (ImageWriters.hpp)
		return QImageWriter::supportedImageFormats().contains(formatHint_);
	}

	bool save(const std::wstring& filename)
	{
		const QString path = QString::fromStdWString(filename);
		// GIF has no Qt encoder: LibreMerge's own, as FreeImage gives
		// upstream one
		if (QFileInfo(path).suffix().compare(QLatin1String("gif"), Qt::CaseInsensitive) == 0)
			return lm_imgwrite::writeGif(path, { image_ }, { 0 }, 0);
		QImageWriter writer(path);
		return writer.write(image_);
	}

	// Reports the number of significant bits per pixel, matching what
	// FreeImage reports for the file's native depth (24 for opaque RGB)
	int depth() const { return image_.isNull() ? 0 : image_.bitPlaneCount(); }
	unsigned width() const  { return image_.isNull() ? 0 : image_.width(); }
	unsigned height() const { return image_.isNull() ? 0 : image_.height(); }
	void clear()
	{
		image_ = QImage();
		metadata_.clear();
		formatHint_.clear();
	}
	void setSize(int w, int h)
	{
		image_ = QImage(w, h, QImage::Format_ARGB32);
		image_.fill(Qt::transparent);
	}
	const QImage& qimage() const { return image_; }
	FipShim *getFipImage() { return &shim_; }

	Color pixel(int x, int y) const
	{
		Color color = {0, 0, 0, 0xFF};
		if (image_.isNull() || x < 0 || y < 0 ||
		    x >= image_.width() || y >= image_.height())
			return color;
		const QRgb rgb = image_.pixel(x, y);
		color.rgbRed = static_cast<BYTE>(qRed(rgb));
		color.rgbGreen = static_cast<BYTE>(qGreen(rgb));
		color.rgbBlue = static_cast<BYTE>(qBlue(rgb));
		color.rgbReserved = static_cast<BYTE>(qAlpha(rgb));
		return color;
	}

	bool copySubImage(Image& image, int x, int y, int x2, int y2) const
	{
		if (image_.isNull() || x2 <= x || y2 <= y)
			return false;
		image.image_ = image_.copy(QRect(x, y, x2 - x, y2 - y));
		return !image.image_.isNull();
	}

	bool pasteSubImage(const Image& image, int x, int y)
	{
		if (image_.isNull() || image.image_.isNull())
			return false;
		QPainter painter(&image_);
		painter.setCompositionMode(QPainter::CompositionMode_Source);
		painter.drawImage(x, y, image.image_);
		return true;
	}

	// FreeImage convention: positive angle rotates counterclockwise on
	// screen; Qt's y-down device coordinates rotate clockwise, hence -angle
	bool rotate(double angle)
	{
		if (image_.isNull())
			return false;
		const bool rightAngle =
			std::abs(angle - 360.0 * std::round(angle / 360.0)) < 1e-9 ||
			std::abs(std::remainder(angle, 90.0)) < 1e-9;
		image_ = image_.transformed(QTransform().rotate(-angle),
			rightAngle ? Qt::FastTransformation : Qt::SmoothTransformation);
		return !image_.isNull();
	}

	bool flipHorizontal()
	{
		if (image_.isNull())
			return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
		image_ = image_.flipped(Qt::Horizontal);
#else
		image_ = image_.mirrored(true, false);
#endif
		return true;
	}

	bool flipVertical()
	{
		if (image_.isNull())
			return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
		image_ = image_.flipped(Qt::Vertical);
#else
		image_ = image_.mirrored(false, true);
#endif
		return true;
	}

	// Adopts the pixels of `other` while keeping this image's storage
	// format (FreeImage re-quantizes to the original bpp on save)
	bool pullImageKeepingBPP(const Image& other)
	{
		const QImage::Format format =
			image_.isNull() ? QImage::Format_ARGB32 : image_.format();
		image_ = other.image_.convertToFormat(format);
		return !image_.isNull();
	}

	std::map<std::string, std::string> getMetadata() const { return metadata_; }

	static int valueR(Color color) { return color.rgbRed; }
	static int valueG(Color color) { return color.rgbGreen; }
	static int valueB(Color color) { return color.rgbBlue; }
	static int valueA(Color color) { return color.rgbReserved; }
	static Color Rgb(int r, int g, int b)
	{
		Color color = {0, 0, 0, 0};
		color.rgbRed = static_cast<BYTE>(r);
		color.rgbGreen = static_cast<BYTE>(g);
		color.rgbBlue = static_cast<BYTE>(b);
		return color;
	}

private:
	// Same wording FreeImage uses for EXIF orientation, so the vendored
	// LoadImages() orientation handling keeps working unchanged
	static const char *exifOrientationString(QImageIOHandler::Transformations t)
	{
		switch (t)
		{
		case QImageIOHandler::TransformationMirror:             return "top, right side";       // 2
		case QImageIOHandler::TransformationRotate180:          return "bottom, right side";    // 3
		case QImageIOHandler::TransformationFlip:               return "bottom, left side";     // 4
		case QImageIOHandler::TransformationFlipAndRotate90:    return "left side, top";        // 5
		case QImageIOHandler::TransformationRotate90:           return "right side, top";       // 6
		case QImageIOHandler::TransformationMirrorAndRotate90:  return "right side, bottom";    // 7
		case QImageIOHandler::TransformationRotate270:          return "left side, bottom";     // 8
		default:                                                return nullptr;                 // 1
		}
	}

	QImage image_;
	std::map<std::string, std::string> metadata_;
	QByteArray formatHint_;
	FipShim shim_;
};

// Multi-page sources (animated GIF, multi-page TIFF): every page read as the
// file opens, as FreeImage's multi-page bitmap holds them, an animation's
// delays and looping with them; saved back as upstream's saveU does, by the
// file's extension, through the encoders Qt lacks (ImageWriters.hpp).
class MultiPageImages
{
public:
	MultiPageImages() {}
	~MultiPageImages() {}

	bool close()
	{
		pages_.clear();
		delays_.clear();
		loopCount_ = 0;
		return true;
	}

	bool isValid() const { return !pages_.isEmpty(); }
	int getPageCount() const { return static_cast<int>(pages_.size()); }

	bool load(const std::wstring& filename)
	{
		close();
		if (filename.empty())
			return false;
		// a PDF's pages are the ImgConverter's, drawn at the vector zoom
		const QString suffix = QFileInfo(QString::fromStdWString(filename)).suffix().toLower();
		if (suffix == QLatin1String("svg") || suffix == QLatin1String("pdf"))
			return false;
		QImageReader reader(QString::fromStdWString(filename));
		reader.setAutoTransform(false);
		if (!reader.canRead() || reader.imageCount() <= 1)
			return false;
		// frame after frame, each with the delay before the next one (a
		// looping animation reads on from its start: the count says when to
		// stop)
		const int count = reader.imageCount();
		for (int i = 0; i < count; ++i)
		{
			// (TIFF's reader stays on its page: go to the next one)
			if (i > 0)
				reader.jumpToImage(i);
			const QImage page = reader.read();
			if (page.isNull())
				break;
			pages_.append(page);
			delays_.append(qMax(0, reader.nextImageDelay()));
		}
		loopCount_ = reader.loopCount();
		if (pages_.size() <= 1)
		{
			close();
			return false;
		}
		return true;
	}

	bool save(const std::wstring& filename)
	{
		const QString path = QString::fromStdWString(filename);
		const QString suffix = QFileInfo(path).suffix().toLower();
		if (suffix == QLatin1String("gif"))
			return lm_imgwrite::writeGif(path, pages_, delays_, loopCount_);
		if (suffix == QLatin1String("tif") || suffix == QLatin1String("tiff"))
			return lm_imgwrite::writeTiff(path, pages_);
		return false; // no other format holds pages
	}

	Image getImage(int page)
	{
		if (page >= 0 && page < pages_.size())
			return Image(pages_[page]);
		return Image();
	}

	void insertPage(int page, const Image& image)
	{
		const qsizetype at = std::min<qsizetype>(qMax(0, page), pages_.size());
		pages_.insert(at, image.qimage());
		delays_.insert(at, delays_.value(at, delays_.isEmpty() ? 0 : delays_.last()));
	}

	void replacePage(int page, const Image& image)
	{
		// (the page keeps its delay: copyAnimationMetadata upstream)
		if (page >= 0 && page < pages_.size())
			pages_[page] = image.qimage();
	}

private:
	QList<QImage> pages_;
	QList<int> delays_;   // milliseconds before the next page
	int loopCount_ = 0;   // QImageReader's: -1 for ever
};
