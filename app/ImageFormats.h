// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QStringList>

namespace lm
{

/** WinMerge's default image file patterns (OPT_CMP_IMG_FILEPATTERNS). */
inline QString defaultImageFilePatterns()
{
	return QStringLiteral(
		"*.bmp;*.cut;*.dds;*.dng;*.exr;*.g3;*.gif;*.heic;*.hdr;*.ico;*.iff;"
		"*.lbm;*.j2k;*.j2c;*.jng;*.jp2;*.jpg;*.jif;*.jpeg;*.jpe;*.jxl;*.jxr;"
		"*.wdp;*.hdp;*.koa;*.mng;*.pcd;*.pcx;*.pfm;*.pct;*.pict;*.pic;*.png;"
		"*.pbm;*.pgm;*.ppm;*.psd;*.ras;*.sgi;*.rgb;*.rgba;*.bw;*.tga;"
		"*.targa;*.tif;*.tiff;*.wap;*.wbmp;*.wbm;*.webp;*.xbm;*.xpm");
}

inline QString imageFilePatterns()
{
	return QSettings().value(QStringLiteral("ImageCompare/FilePatterns"),
		defaultImageFilePatterns()).toString();
}

/** Whether a path matches the image patterns (drives opening a pair as an
    image comparison, like WinMerge's ShowAutoMergeDoc). */
inline bool isImageFile(const QString &path)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	if (suffix.isEmpty())
		return false;
	const QStringList patterns = imageFilePatterns().split(QLatin1Char(';'),
		Qt::SkipEmptyParts);
	for (const QString &pattern : patterns)
	{
		const QString trimmed = pattern.trimmed();
		if (trimmed.startsWith(QStringLiteral("*."))
			&& trimmed.mid(2).toLower() == suffix)
			return true;
	}
	return false;
}

} // namespace lm
