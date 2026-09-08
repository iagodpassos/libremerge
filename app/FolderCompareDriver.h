// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <memory>
#include <QDateTime>
#include <QList>
#include <QString>

class CompareStats;

namespace lm
{

/** One row of a 2- or 3-way folder comparison result. */
struct FolderCompareItem
{
	enum Category
	{
		Identical,
		Different,
		LeftOnly,
		MiddleOnly,    ///< 3-way: exists in the middle folder only
		RightOnly,
		MissingLeft,   ///< 3-way: exists everywhere but on the left
		MissingMiddle, ///< 3-way: exists everywhere but in the middle
		MissingRight,  ///< 3-way: exists everywhere but on the right
		Skipped,
		Error,
	};

	/** WinMerge's COMPAREFLAGS3WAY: which pair stayed identical when a
	    3-way item differs. */
	enum ThreeWayInfo
	{
		NoInfo,
		MiddleRightIdentical, ///< only the left side differs
		LeftRightIdentical,   ///< only the middle differs
		LeftMiddleIdentical,  ///< only the right side differs
	};

	QString name;         ///< item filename
	QString folder;       ///< relative folder inside the compared roots
	QString path[3];      ///< full path per side (empty if missing there)
	qint64 size[3] = {-1, -1, -1};
	QDateTime mtime[3];
	Category category = Identical;
	ThreeWayInfo threeWay = NoInfo;
	bool isDir = false;
};

struct FolderCompareResult
{
	QList<FolderCompareItem> items;
	int sides = 2;
	int identical = 0;
	int different = 0;
	int unique = 0;
	bool aborted = false;
	bool ok = false;
	QString error;
};

/**
 * Shared state of a running folder comparison: progress counters the UI
 * can poll from another thread, and the abort flag. Create one per run
 * and keep it alive (shared_ptr) for the duration of the job.
 */
class FolderCompareJob
{
public:
	explicit FolderCompareJob(int sides = 2);
	~FolderCompareJob();

	void requestAbort() { m_abort.store(true); }
	bool abortRequested() const { return m_abort.load(); }

	/** Progress: items compared so far / total collected (grows during scan). */
	int comparedItems() const;
	int totalItems() const;

	CompareStats *stats() { return m_stats.get(); }

private:
	std::atomic<bool> m_abort{false};
	std::unique_ptr<CompareStats> m_stats;
};

/** Run a synchronous 2-way folder comparison (full content compare)
    with the engine's DirScan machinery. Safe to call from a worker
    thread; pass a job for progress/abort. filterMask accepts the
    engine's syntax: masks ("*.cpp;*.h"), f:/d: regexes and filter
    expressions. */
FolderCompareResult compareFolders(const QString &leftDir, const QString &rightDir,
	bool recursive, const std::shared_ptr<FolderCompareJob> &job = {},
	const QString &filterMask = QStringLiteral("*.*"));

/** Same, for two or three folders (WinMerge's 3-way folder compare).
    A three-sided run needs a job created with sides = 3. */
FolderCompareResult compareFolders(const QStringList &dirs,
	bool recursive, const std::shared_ptr<FolderCompareJob> &job = {},
	const QString &filterMask = QStringLiteral("*.*"));

} // namespace lm
