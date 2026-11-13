// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge port layer for the WinIMerge ImgConverter. Upstream renders
// PDF and SVG through Windows' own renderers (Windows.Data.Pdf, Direct2D),
// in its 64-bit build, and EMF/WMF through GDI+. Here the PDF and SVG
// files go through Qt's image plugins for them (qpdf, qsvg), when they
// are there: a PDF's pages are the image's pages, and both are drawn at
// the vector image zoom (Image > Vector Image Scaling). Windows metafiles
// have no reader here.

#pragma once

#include "image.hpp"

#include <QFileInfo>
#include <QImageReader>
#include <QSizeF>
#include <QString>

class ImgConverter
{
public:
	/** getImageType: .svg and .pdf, as upstream's 64-bit build takes
	    them, when Qt can read them. */
	static bool isSupportedImage(const wchar_t *filename)
	{
		const QByteArray format = QFileInfo(QString::fromWCharArray(filename))
			.suffix().toLower().toLatin1();
		return (format == "svg" || format == "pdf")
			&& QImageReader::supportedImageFormats().contains(format);
	}

	bool isValid() const { return m_pageCount > 0; }

	bool load(const wchar_t *filename)
	{
		close();
		if (!isSupportedImage(filename))
			return false;
		const QString path = QString::fromWCharArray(filename);
		QImageReader reader(path);
		if (!reader.canRead())
			return false;
		m_filename = path;
		m_pageCount = static_cast<unsigned>(qMax(1, reader.imageCount()));
		return true;
	}

	void close()
	{
		m_filename.clear();
		m_pageCount = 0;
	}

	/** A page drawn at its own size times the zoom. */
	void render(Image &img, int page, float zoom)
	{
		if (!isValid())
			return;
		QImageReader reader(m_filename);
		if (page > 0)
			reader.jumpToImage(page);
		const QSize size = reader.size();
		if (size.isValid() && zoom > 0 && zoom != 1.0f)
			reader.setScaledSize((QSizeF(size) * zoom).toSize());
		const QImage image = reader.read();
		if (!image.isNull())
			img = Image(image);
	}

	unsigned getPageCount() const { return m_pageCount; }

private:
	QString m_filename;
	unsigned m_pageCount = 0;
};
