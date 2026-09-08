// SPDX-License-Identifier: GPL-3.0-or-later
// Driver over the engine's folder compare: DirScan_GetItems collects the
// item tree into CDiffContext, DirScan_CompareItems runs the content
// compare. Upstream drives the same functions from CDiffThread; here the
// caller supplies the thread (FolderCompareView runs it via QtConcurrent)
// and polls progress through FolderCompareJob.
#include "pch.h"

#include "FolderCompareDriver.h"
#include "EngineOptions.h"
#include "ImageFormats.h"

#include <climits>
#include <QSettings>
#include <Poco/Semaphore.h>

#include "CompareStats.h"
#include "DiffContext.h"
#include "DiffItem.h"
#include "DiffThread.h"
#include "DiffWrapper.h"
#include "DirScan.h"
#include "FileFilterHelper.h"
#include "IAbortable.h"
#include "PathContext.h"
#include "paths.h"

namespace lm
{

namespace
{

QString sideString(const String &s)
{
	return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

/** WinMerge's ColStatusGet decision ladder; the order matters (skipped
    before unique, unique before the missing-on-one-side cases). */
FolderCompareItem::Category classify(const DIFFITEM &di, int sides)
{
	if (di.diffcode.isResultError())
		return FolderCompareItem::Error;
	if (di.diffcode.isResultFiltered())
		return FolderCompareItem::Skipped;
	if (di.diffcode.isSideFirstOnly())
		return FolderCompareItem::LeftOnly;
	if (di.diffcode.isSideSecondOnly())
		return sides < 3 ? FolderCompareItem::RightOnly
		                 : FolderCompareItem::MiddleOnly;
	if (di.diffcode.isSideThirdOnly())
		return FolderCompareItem::RightOnly;
	if (sides > 2 && !di.diffcode.existsFirst())
		return FolderCompareItem::MissingLeft;
	if (sides > 2 && !di.diffcode.existsSecond())
		return FolderCompareItem::MissingMiddle;
	if (sides > 2 && !di.diffcode.existsThird())
		return FolderCompareItem::MissingRight;
	if (di.diffcode.isResultSame())
		return FolderCompareItem::Identical;
	return FolderCompareItem::Different;
}

/** Which pair stayed identical (the 3-way suffix of WinMerge's Result
    column). */
FolderCompareItem::ThreeWayInfo threeWayInfo(const DIFFITEM &di)
{
	switch (di.diffcode.diffcode & DIFFCODE::COMPAREFLAGS3WAY)
	{
	case DIFFCODE::DIFF1STONLY: return FolderCompareItem::MiddleRightIdentical;
	case DIFFCODE::DIFF2NDONLY: return FolderCompareItem::LeftRightIdentical;
	case DIFFCODE::DIFF3RDONLY: return FolderCompareItem::LeftMiddleIdentical;
	}
	return FolderCompareItem::NoInfo;
}

class JobAbortable : public IAbortable
{
public:
	explicit JobAbortable(const FolderCompareJob *job) : m_job(job) {}
	bool ShouldAbort() const override
	{
		return m_job != nullptr && m_job->abortRequested();
	}
private:
	const FolderCompareJob *m_job;
};

} // namespace

FolderCompareJob::FolderCompareJob(int sides)
	: m_stats(new CompareStats(sides))
{
	m_stats->SetCompareThreadCount(1);
}

FolderCompareJob::~FolderCompareJob() = default;

int FolderCompareJob::comparedItems() const
{
	return m_stats->GetComparedItems();
}

int FolderCompareJob::totalItems() const
{
	return m_stats->GetTotalItems();
}

FolderCompareResult compareFolders(const QString &leftDir, const QString &rightDir,
	bool recursive, const std::shared_ptr<FolderCompareJob> &jobIn,
	const QString &filterMask)
{
	return compareFolders(QStringList{ leftDir, rightDir }, recursive,
		jobIn, filterMask);
}

FolderCompareResult compareFolders(const QStringList &dirs,
	bool recursive, const std::shared_ptr<FolderCompareJob> &jobIn,
	const QString &filterMask)
{
	const int sides = dirs.size();
	FolderCompareResult result;
	result.sides = sides;
	std::shared_ptr<FolderCompareJob> job = jobIn;
	if (!job)
		job = std::make_shared<FolderCompareJob>(sides);

	PathContext paths;
	paths.SetSize(sides);
	for (int i = 0; i < sides; ++i)
		paths.SetPath(i, dirs.at(i).toStdString(), false);
	CDiffContext ctxt(paths, CMP_CONTENT);

	ctxt.m_pCompareStats = job->stats();
	ctxt.m_bRecursive = recursive;

	JobAbortable abortable(job.get());
	ctxt.SetAbortable(&abortable);

	const DIFFOPTIONS options = currentDiffOptions();
	if (!ctxt.CreateCompareOptions(CMP_CONTENT, options))
	{
		result.error = QObject::tr("could not create compare options");
		return result;
	}

	FileFilterHelper filter;
	const QString mask = filterMask.trimmed().isEmpty()
		? QStringLiteral("*.*") : filterMask.trimmed();
	filter.SetMaskOrExpression(mask.toStdString());
	ctxt.m_piFilterGlobal = &filter;

	// image compare in folder compare (WinMerge option, off by default):
	// matching pairs compare by pixels through the registered hook, with
	// the same color-distance threshold as the image window
	FileFilterHelper imgFilter;
	if (QSettings().value(QStringLiteral("ImageCompare/EnableInFolderCompare"),
			false).toBool())
	{
		ctxt.m_bEnableImageCompare = true;
		ctxt.m_dColorDistanceThreshold = QSettings().value(
			QStringLiteral("ImageCompare/ColorDistanceThreshold"), 0).toInt()
			/ 1000.0;
		imgFilter.SetMaskOrExpression(
			lm::imageFilePatterns().toStdString());
		ctxt.m_pImgfileFilter = &imgFilter;
	}

	ctxt.InitDiffItemList();

	Poco::Semaphore semaphore(0, INT_MAX);
	DiffFuncStruct myStruct;
	myStruct.context = &ctxt;
	myStruct.pSemaphore = &semaphore;
	myStruct.nThreadCount = 1;
	// DirScan_GetItems only releases the per-item semaphore when a collect
	// callback is installed (it is always set in upstream's CDiffThread)
	myStruct.m_fncCollect = [](DiffFuncStruct *) {};

	const String subdirs[3] = {};
	DirScan_GetItems(paths, subdirs, &myStruct, false /*casesensitive*/,
		recursive ? -1 : 0, nullptr, true /*bUniques*/);
	// upstream's collect thread releases the semaphore once more to signal
	// that the collect phase is complete (see DiffThreadCollect)
	semaphore.set();
	DirScan_CompareItems(&myStruct, nullptr);

	DIFFITEM *pos = ctxt.GetFirstDiffPosition();
	while (pos != nullptr)
	{
		const DIFFITEM &di = ctxt.GetNextDiffPosition(pos);

		FolderCompareItem item;
		int side = 0;
		while (side < sides - 1 && !di.diffcode.exists(side))
			++side;
		item.name = sideString(di.diffFileInfo[side].filename.get());
		item.folder = sideString(di.diffFileInfo[side].path.get());
		item.isDir = di.diffcode.isDirectory();
		item.category = classify(di, sides);
		if (sides > 2 && (item.category == FolderCompareItem::Different
			|| item.category == FolderCompareItem::MissingLeft
			|| item.category == FolderCompareItem::MissingMiddle
			|| item.category == FolderCompareItem::MissingRight))
			item.threeWay = threeWayInfo(di);

		PathContext files;
		ctxt.GetComparePaths(di, files);
		for (int i = 0; i < sides; ++i)
		{
			if (!di.diffcode.exists(i))
				continue;
			item.path[i] = sideString(files[i]);
			item.size[i] = di.diffFileInfo[i].size == DirItem::FILE_SIZE_NONE
				? -1 : static_cast<qint64>(di.diffFileInfo[i].size);
			item.mtime[i] = QDateTime::fromSecsSinceEpoch(
				static_cast<qint64>(di.diffFileInfo[i].mtime.epochTime()));
		}

		switch (item.category)
		{
		case FolderCompareItem::Identical: ++result.identical; break;
		case FolderCompareItem::Different:
		case FolderCompareItem::MissingLeft:
		case FolderCompareItem::MissingMiddle:
		case FolderCompareItem::MissingRight: ++result.different; break;
		case FolderCompareItem::LeftOnly:
		case FolderCompareItem::MiddleOnly:
		case FolderCompareItem::RightOnly: ++result.unique; break;
		default: break;
		}
		result.items.append(item);
	}

	// context holds pointers to objects owned here/by the job; detach them
	ctxt.SetAbortable(nullptr);
	ctxt.m_piFilterGlobal = nullptr;
	ctxt.m_pCompareStats = nullptr;

	result.aborted = job->abortRequested();
	result.ok = true;
	return result;
}

} // namespace lm
