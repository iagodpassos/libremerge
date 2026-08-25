// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the image-compare core: vendored WinIMerge ImgDiffBuffer /
// ImgMergeBuffer running on the Qt-backed image wrapper. All fixtures are
// synthetic QImages written to a temporary directory.

#include <gtest/gtest.h>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include "ImgMergeBuffer.hpp"

namespace
{

class ImgMergeBufferTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE(m_dir.isValid());
	}

	std::wstring writeImage(const char *name, const QImage& img)
	{
		const QString path = m_dir.filePath(QString::fromUtf8(name));
		EXPECT_TRUE(img.save(path));
		return path.toStdWString();
	}

	static QImage solidImage(int w, int h, QColor color)
	{
		QImage img(w, h, QImage::Format_ARGB32);
		img.fill(color);
		return img;
	}

	bool open2(CImgMergeBuffer& buf, const std::wstring& l, const std::wstring& r)
	{
		const wchar_t *files[3] = { l.c_str(), r.c_str(), nullptr };
		if (!buf.OpenImages(2, files))
			return false;
		buf.CompareImages();
		return true;
	}

	QTemporaryDir m_dir;
};

TEST_F(ImgMergeBufferTest, IdenticalImagesHaveNoDiff)
{
	QImage img = solidImage(64, 48, Qt::white);
	auto left = writeImage("a.png", img);
	auto right = writeImage("b.png", img);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	EXPECT_EQ(0, buf.GetDiffCount());
	EXPECT_EQ(64, buf.GetImageWidth(0));
	EXPECT_EQ(48, buf.GetImageHeight(0));
}

TEST_F(ImgMergeBufferTest, SingleChangedRegionIsOneDiff)
{
	QImage a = solidImage(64, 48, Qt::white);
	QImage b = a;
	{
		QPainter p(&b);
		p.fillRect(10, 10, 12, 12, Qt::red);
	}
	auto left = writeImage("a.png", a);
	auto right = writeImage("b.png", b);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	ASSERT_EQ(1, buf.GetDiffCount());

	// The diff rect is in block units (default block size 8):
	// pixels [10,22) touch blocks 1..2 in both axes
	const DiffInfo *info = buf.GetDiffInfo(0);
	ASSERT_NE(nullptr, info);
	EXPECT_EQ(8, buf.GetDiffBlockSize());
	EXPECT_EQ(1, info->rc.left);
	EXPECT_EQ(1, info->rc.top);
	EXPECT_EQ(3, info->rc.right);
	EXPECT_EQ(3, info->rc.bottom);
}

TEST_F(ImgMergeBufferTest, DistantRegionsAreSeparateDiffs)
{
	QImage a = solidImage(128, 96, Qt::white);
	QImage b = a;
	{
		QPainter p(&b);
		p.fillRect(2, 2, 4, 4, Qt::blue);
		p.fillRect(100, 80, 6, 6, Qt::green);
	}
	auto left = writeImage("a.png", a);
	auto right = writeImage("b.png", b);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	EXPECT_EQ(2, buf.GetDiffCount());
}

TEST_F(ImgMergeBufferTest, AdjacentBlocksMergeIntoOneDiff)
{
	// Two touching changed blocks (8-connectivity) count as one diff
	QImage a = solidImage(64, 64, Qt::white);
	QImage b = a;
	{
		QPainter p(&b);
		p.fillRect(0, 0, 8, 8, Qt::black);
		p.fillRect(8, 8, 8, 8, Qt::black); // diagonal neighbor
	}
	auto left = writeImage("a.png", a);
	auto right = writeImage("b.png", b);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	EXPECT_EQ(1, buf.GetDiffCount());
}

TEST_F(ImgMergeBufferTest, ColorDistanceThresholdSuppressesSmallDeltas)
{
	QImage a = solidImage(32, 32, QColor(100, 100, 100));
	QImage b = solidImage(32, 32, QColor(110, 100, 100)); // distance 10

	auto left = writeImage("a.png", a);
	auto right = writeImage("b.png", b);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	EXPECT_GE(buf.GetDiffCount(), 1);

	buf.SetColorDistanceThreshold(16.0);
	EXPECT_EQ(0, buf.GetDiffCount());

	buf.SetColorDistanceThreshold(5.0);
	EXPECT_GE(buf.GetDiffCount(), 1);
}

TEST_F(ImgMergeBufferTest, DifferentSizesMarkTheExtraAreaAsDiff)
{
	QImage a = solidImage(32, 32, Qt::white);
	QImage b = solidImage(32, 48, Qt::white); // 16 extra rows

	auto left = writeImage("a.png", a);
	auto right = writeImage("b.png", b);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	EXPECT_GE(buf.GetDiffCount(), 1);
}

TEST_F(ImgMergeBufferTest, VerticalInsertionDetectionAlignsInsertedRows)
{
	// b equals a with a 16-row band inserted in the middle. Without
	// insertion detection everything below the band differs; with it the
	// band is isolated as the only difference.
	QImage a(64, 64, QImage::Format_ARGB32);
	for (int y = 0; y < 64; ++y)
		for (int x = 0; x < 64; ++x)
			a.setPixel(x, y, qRgb(x * 3 % 251, y * 7 % 251, (x + y) % 251));
	QImage b(64, 80, QImage::Format_ARGB32);
	{
		QPainter p(&b);
		p.drawImage(0, 0, a, 0, 0, 64, 32);
		p.fillRect(0, 32, 64, 16, Qt::magenta);
		p.drawImage(0, 48, a, 0, 32, 64, 32);
	}
	auto left = writeImage("a.png", a);
	auto right = writeImage("b.png", b);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	const int noDetection = buf.GetDiffCount();
	ASSERT_GE(noDetection, 1);

	buf.SetInsertionDeletionDetectionMode(
		CImgDiffBuffer::INSERTION_DELETION_DETECTION_VERTICAL);
	EXPECT_EQ(1, buf.GetDiffCount());

	// The preprocessed (aligned) images gained ghost lines: both panes
	// report the same aligned height, larger than the left original
	EXPECT_EQ(buf.GetPreprocessedImageHeight(0), buf.GetPreprocessedImageHeight(1));
	EXPECT_GT(buf.GetPreprocessedImageHeight(0), 64);
}

TEST_F(ImgMergeBufferTest, CopyDiffUndoRedoRoundTrip)
{
	QImage a = solidImage(64, 48, Qt::white);
	QImage b = a;
	{
		QPainter p(&b);
		p.fillRect(16, 16, 8, 8, Qt::red);
	}
	auto left = writeImage("a.png", a);
	auto right = writeImage("b.png", b);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	ASSERT_EQ(1, buf.GetDiffCount());
	EXPECT_FALSE(buf.IsModified(0));

	buf.CopyDiff(0, 1, 0); // right -> left
	EXPECT_EQ(0, buf.GetDiffCount());
	EXPECT_TRUE(buf.IsModified(0));
	EXPECT_TRUE(buf.IsUndoable());

	ASSERT_TRUE(buf.Undo());
	EXPECT_EQ(1, buf.GetDiffCount());
	EXPECT_FALSE(buf.IsModified(0));
	EXPECT_TRUE(buf.IsRedoable());

	ASSERT_TRUE(buf.Redo());
	EXPECT_EQ(0, buf.GetDiffCount());
	EXPECT_TRUE(buf.IsModified(0));
}

TEST_F(ImgMergeBufferTest, SaveModifiedPaneRoundTrips)
{
	QImage a = solidImage(64, 48, Qt::white);
	QImage b = a;
	{
		QPainter p(&b);
		p.fillRect(0, 0, 8, 8, Qt::red);
	}
	auto left = writeImage("a.png", a);
	auto right = writeImage("b.png", b);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	buf.CopyDiff(0, 1, 0);
	ASSERT_TRUE(buf.IsModified(0));
	ASSERT_TRUE(buf.SaveImage(0));
	EXPECT_FALSE(buf.IsModified(0));
	buf.CloseImages();

	CImgMergeBuffer buf2;
	ASSERT_TRUE(open2(buf2, left, right));
	EXPECT_EQ(0, buf2.GetDiffCount());
}

TEST_F(ImgMergeBufferTest, ReadOnlyPaneRejectsCopy)
{
	QImage a = solidImage(32, 32, Qt::white);
	QImage b = solidImage(32, 32, Qt::black);
	auto left = writeImage("a.png", a);
	auto right = writeImage("b.png", b);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	buf.SetReadOnly(0, true);
	buf.CopyDiff(0, 1, 0);
	EXPECT_GE(buf.GetDiffCount(), 1);
	EXPECT_FALSE(buf.IsModified(0));
}

TEST_F(ImgMergeBufferTest, DiffNavigationOrder)
{
	QImage a = solidImage(64, 128, Qt::white);
	QImage b = a;
	{
		QPainter p(&b);
		p.fillRect(0, 0, 4, 4, Qt::red);       // diff 0 (top)
		p.fillRect(30, 60, 4, 4, Qt::green);   // diff 1
		p.fillRect(50, 120, 4, 4, Qt::blue);   // diff 2 (bottom)
	}
	auto left = writeImage("a.png", a);
	auto right = writeImage("b.png", b);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	ASSERT_EQ(3, buf.GetDiffCount());
	EXPECT_EQ(-1, buf.GetCurrentDiffIndex());

	EXPECT_TRUE(buf.FirstDiff());
	EXPECT_EQ(0, buf.GetCurrentDiffIndex());
	EXPECT_TRUE(buf.NextDiff());
	EXPECT_EQ(1, buf.GetCurrentDiffIndex());
	EXPECT_TRUE(buf.LastDiff());
	EXPECT_EQ(2, buf.GetCurrentDiffIndex());
	EXPECT_FALSE(buf.NextDiff());
	EXPECT_TRUE(buf.PrevDiff());
	EXPECT_EQ(1, buf.GetCurrentDiffIndex());
}

TEST_F(ImgMergeBufferTest, RotateIsCounterClockwiseForPositiveAngles)
{
	// FreeImage semantics: rotate(90) turns the content counterclockwise,
	// so of a horizontal pair [A B] the right pixel B ends on top
	Image img(2, 1);
	unsigned char *line = img.scanLine(0);
	line[0] = 0x11; line[1] = 0x22; line[2] = 0x33; line[3] = 0xFF; // A
	line[4] = 0x44; line[5] = 0x55; line[6] = 0x66; line[7] = 0xFF; // B

	ASSERT_TRUE(img.rotate(90));
	ASSERT_EQ(1u, img.width());
	ASSERT_EQ(2u, img.height());
	const unsigned char *top = img.scanLine(0);
	const unsigned char *bottom = img.scanLine(1);
	EXPECT_EQ(0x44, top[0]);    // B
	EXPECT_EQ(0x11, bottom[0]); // A
}

TEST_F(ImgMergeBufferTest, DiffMapImageMarksDiffBlocks)
{
	QImage a = solidImage(64, 64, Qt::white);
	QImage b = a;
	{
		QPainter p(&b);
		p.fillRect(0, 0, 8, 8, Qt::red);
	}
	auto left = writeImage("a.png", a);
	auto right = writeImage("b.png", b);

	CImgMergeBuffer buf;
	ASSERT_TRUE(open2(buf, left, right));
	Image *map = buf.GetDiffMapImage(32, 32);
	ASSERT_NE(nullptr, map);
	EXPECT_EQ(32u, map->width());
	EXPECT_EQ(32u, map->height());
	// top-left corner carries the diff color, bottom-right does not
	const unsigned char *topLine = map->scanLine(0);
	const unsigned char *bottomLine = map->scanLine(31);
	EXPECT_NE(0, topLine[3]);
	EXPECT_EQ(0, bottomLine[31 * 4 + 3]);
}

} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QGuiApplication app(argc, argv);
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
