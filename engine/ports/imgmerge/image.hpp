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
		QImageReader reader(QString::fromStdWString(filename));
		reader.setAutoTransform(false);
		QImage img = reader.read();
		if (img.isNull())
			return false;
		image_ = img;
		formatHint_ = reader.format();
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
		return QImageWriter::supportedImageFormats().contains(formatHint_);
	}

	bool save(const std::wstring& filename)
	{
		QImageWriter writer(QString::fromStdWString(filename));
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

// Multi-page sources (animated GIF, multi-page TIFF) are readable and
// comparable page by page. Writing multi-page files back is not supported
// by Qt's encoders, so save() reports failure and the UI keeps those
// panes read-only, unlike WinMerge/FreeImage which can re-encode them.
class MultiPageImages
{
public:
	MultiPageImages() {}
	~MultiPageImages() {}

	bool close()
	{
		filename_.clear();
		pages_.clear();
		pageCount_ = 0;
		return true;
	}

	bool isValid() const { return pageCount_ > 0; }
	int getPageCount() const { return pageCount_; }

	bool load(const std::wstring& filename)
	{
		close();
		QImageReader reader(QString::fromStdWString(filename));
		if (!reader.canRead())
			return false;
		int count = reader.imageCount();
		if (count <= 1)
			return false;
		filename_ = QString::fromStdWString(filename);
		pageCount_ = count;
		return true;
	}

	bool save(const std::wstring&) { return false; }

	Image getImage(int page)
	{
		if (!filename_.isEmpty())
		{
			QImageReader reader(filename_);
			reader.setAutoTransform(false);
			if (reader.jumpToImage(page))
			{
				QImage img = reader.read();
				if (!img.isNull())
					return Image(img);
			}
			return Image();
		}
		if (page >= 0 && page < pages_.size())
			return Image(pages_[page]);
		return Image();
	}

	void insertPage(int page, const Image& image)
	{
		pages_.insert(std::min<qsizetype>(page, pages_.size()), image.qimage());
		pageCount_ = pages_.size();
	}

	void replacePage(int page, const Image& image)
	{
		if (page >= 0 && page < pages_.size())
			pages_[page] = image.qimage();
	}

private:
	QString filename_;
	QList<QImage> pages_;
	int pageCount_ = 0;
};
