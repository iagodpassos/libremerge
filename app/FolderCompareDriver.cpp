// SPDX-License-Identifier: GPL-3.0-or-later
// Driver over the engine's folder compare: DirScan_GetItems collects the
// item tree into CDiffContext, DirScan_CompareItems runs the content
// compare. Upstream drives the same functions from CDiffThread; here the
// caller supplies the thread (FolderCompareView runs it via QtConcurrent)
// and polls progress through FolderCompareJob.
#include "pch.h"

#include "FolderCompareDriver.h"
#include "EngineOptions.h"

#include <climits>
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

FolderCompareItem::Category classify(const DIFFITEM &di)
{
	if (di.diffcode.isResultError())
		return FolderCompareItem::Error;
	if (di.diffcode.isResultFiltered())
		return FolderCompareItem::Skipped;
	if (di.diffcode.isSideFirstOnly())
		return FolderCompareItem::LeftOnly;
	if (di.diffcode.isSideSecondOnly())
		return FolderCompareItem::RightOnly;
	if (di.diffcode.isResultSame())
		return FolderCompareItem::Identical;
	return FolderCompareItem::Different;
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

FolderCompareJob::FolderCompareJob()
	: m_stats(new CompareStats(2))
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
	bool recursive, const std::shared_ptr<FolderCompareJob> &jobIn)
{
	FolderCompareResult result;
	std::shared_ptr<FolderCompareJob> job = jobIn;
	if (!job)
		job = std::make_shared<FolderCompareJob>();

	PathContext paths(leftDir.toStdString(), rightDir.toStdString());
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
	filter.SetMaskOrExpression(_T("*.*"));
	ctxt.m_piFilterGlobal = &filter;

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
		const int side = di.diffcode.exists(0) ? 0 : 1;
		item.name = sideString(di.diffFileInfo[side].filename.get());
		item.folder = sideString(di.diffFileInfo[side].path.get());
		item.isDir = di.diffcode.isDirectory();
		item.category = classify(di);

		PathContext files;
		ctxt.GetComparePaths(di, files);
		if (di.diffcode.exists(0))
			item.leftPath = sideString(files[0]);
		if (di.diffcode.exists(1))
			item.rightPath = sideString(files[1]);
		for (int i = 0; i < 2; ++i)
		{
			if (!di.diffcode.exists(i))
				continue;
			item.size[i] = di.diffFileInfo[i].size == DirItem::FILE_SIZE_NONE
				? -1 : static_cast<qint64>(di.diffFileInfo[i].size);
			item.mtime[i] = QDateTime::fromSecsSinceEpoch(
				static_cast<qint64>(di.diffFileInfo[i].mtime.epochTime()));
		}

		switch (item.category)
		{
		case FolderCompareItem::Identical: ++result.identical; break;
		case FolderCompareItem::Different: ++result.different; break;
		case FolderCompareItem::LeftOnly:
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
