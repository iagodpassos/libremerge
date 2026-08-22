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

/** One row of a 2-way folder comparison result. */
struct FolderCompareItem
{
	enum Category
	{
		Identical,
		Different,
		LeftOnly,
		RightOnly,
		Skipped,
		Error,
	};

	QString name;         ///< item filename
	QString folder;       ///< relative folder inside the compared roots
	QString leftPath;     ///< full path on the left side (empty if missing)
	QString rightPath;    ///< full path on the right side (empty if missing)
	qint64 size[2] = {-1, -1};
	QDateTime mtime[2];
	Category category = Identical;
	bool isDir = false;
};

struct FolderCompareResult
{
	QList<FolderCompareItem> items;
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
	FolderCompareJob();
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

} // namespace lm
