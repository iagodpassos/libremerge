// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSettings>
#include <QString>

#include "FileFilters.h"
#include "Settings.h"

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
	return setting(QStringLiteral("ImageCompare/FilePatterns"),
		defaultImageFilePatterns()).toString();
}

/** WinMerge's "Enable image compare in folder compare" (Options > Compare
    > Image, off by default): a folder comparison compares the files the
    image patterns take by their pixels. */
inline bool imageCompareInFolderCompare()
{
	return setting(QStringLiteral("ImageCompare/EnableInFolderCompare"),
		false).toBool();
}

/** Whether the image patterns (masks or an expression) take a path:
    WinMerge's ShowAutoMergeDoc opens such a file in its image
    comparison. */
inline bool isImageFile(const QString &path)
{
	return filePatternsTake(imageFilePatterns(), path);
}

} // namespace lm
