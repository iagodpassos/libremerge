// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <memory>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

class CDiffContext;
class CompareStats;
class DIFFITEM;
class FileFilterHelper;

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
		NotCompared,   ///< result unknown after a copy/delete (WinMerge's
		               ///  NOCMP; a recompare resolves it)
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

	/** What the content compare took the files for (WinMerge's DIFFCODE
	    TEXT/BIN/IMAGE flags), which the Result column names. Unknown
	    when no content compare ran: folders, unique items, errors. */
	enum FileType
	{
		UnknownType,
		TextFiles,
		BinaryFiles,
		ImageFiles,
	};

	QString name;         ///< item filename
	QString folder;       ///< relative folder inside the compared roots
	QString path[3];      ///< full path per side (empty if missing there)
	qint64 size[3] = {-1, -1, -1};
	QDateTime mtime[3];
	Category category = Identical;
	ThreeWayInfo threeWay = NoInfo;
	FileType fileType = UnknownType;
	bool isDir = false;
	/** The folder item this one is in: its place in the result's list,
	    -1 at the top of the compared folders. */
	int parent = -1;
	/** Deleted on every side since the comparison: no longer listed. */
	bool removed = false;
	/** The engine's own item, alive as long as the result's context. */
	DIFFITEM *engineItem = nullptr;
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
	/** The engine's comparison, kept for what asks about its items once it
	    has run: the display filter (CDirDoc keeps its CDiffContext for the
	    life of the folder window the same way). */
	std::shared_ptr<CDiffContext> context;
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
    A three-sided run needs a job created with sides = 3. compareMethod
    is a COMPARE_TYPE, Full Contents through Existence; -1 takes the
    saved option. */
FolderCompareResult compareFolders(const QStringList &dirs,
	bool recursive, const std::shared_ptr<FolderCompareJob> &job = {},
	const QString &filterMask = QStringLiteral("*.*"), int compareMethod = -1);

/** Same, by a file filter made beforehand: a copy of the global one
    with its preset files, taken on the GUI thread (lm::cloneFileFilter).
    A null one compares everything. */
FolderCompareResult compareFolders(const QStringList &dirs,
	bool recursive, const std::shared_ptr<FolderCompareJob> &job,
	const std::shared_ptr<FileFilterHelper> &filter, int compareMethod = -1);

/**
 * WinMerge's display filter (DirViewFilterSettings::displayFilterHelper):
 * the mask or filter expression of the folder window's filter bar. It
 * hides items of a comparison that already ran, asking the engine about
 * each of them, and compares nothing again. Like upstream's, it knows no
 * preset filter files: "pf:" names nothing here.
 */
class FolderDisplayFilter
{
public:
	FolderDisplayFilter();
	~FolderDisplayFilter();

	/** Take a mask or expression (SetMaskOrExpression); an empty one
	    filters nothing. */
	void setMask(const QString &mask);
	QString mask() const;
	bool isEmpty() const;
	/** What is wrong with the mask, a line per error; none when it is
	    valid. */
	QStringList errors() const;

	/** The comparison whose items includes() is asked about
	    (SetDiffContext, which upstream calls on every redisplay). */
	void bind(const FolderCompareResult &result);
	/** Whether an item of that comparison passes: includeDir for a folder,
	    includeFile for a file. */
	bool includes(const FolderCompareItem &item) const;

private:
	std::unique_ptr<FileFilterHelper> m_helper;
	std::shared_ptr<CDiffContext> m_context;
};

/**
 * A copy, a delete or a save changed an item on disk: bring the engine's
 * item in line with what its row shows from then on, so that the display
 * filter goes by it (UpdateDiffAfterOperation, CDirDoc::UpdateChangedItem).
 * exists tells the sides the item is on now and category its new result;
 * differences is the number a save left, -1 when it is not known.
 */
void updateEngineItem(const FolderCompareResult &result,
	const FolderCompareItem &item, const bool exists[3],
	FolderCompareItem::Category category, int differences = -1);

} // namespace lm
