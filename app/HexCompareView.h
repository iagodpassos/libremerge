// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QStringList>
#include <QWidget>

#include "FileOps.h"

class QAction;
class QLabel;
class QSplitter;
class FilepathEdit;
class HexUndoHistory;
class HexView;
class HexStatusPosition;

/**
 * WinMerge's binary comparison (CHexMergeDoc, CHexMergeView and
 * CHexMergeFrame): two or three files as hex dumps side by side, in the
 * hex editor LibreMerge writes after WinMerge's (HexView). The bytes are
 * compared at the same offsets; Next and Previous Difference select the
 * next run of differing bytes in every pane, and the copy commands copy
 * what is selected. One undo history spans the panes. Each pane has
 * WinMerge's path bar over it and the editor's own status bar.
 */
class HexCompareView : public QWidget
{
	Q_OBJECT
public:
	explicit HexCompareView(QWidget *parent = nullptr);
	~HexCompareView() override;

	/** Load 2 or 3 files (an empty path is an untitled, empty side). */
	bool compare(const QStringList &paths, QString *error);

	QStringList paths() const;
	int paneCount() const { return m_paneCount; }
	QString tabTitle() const;
	void setSideDescription(int side, const QString &description);
	void setReadOnlySides(const QList<bool> &readOnly);
	/** Where a pane is saved in place of its file (the command line's
	    -o); the pane goes on with that file once saved. */
	void setSaveAsPath(const QString &path) { m_saveAsPath = path; }
	bool isModified() const;
	/** WinMerge's UpdateLastCompareResult: 0 when the panes hold the same
	    bytes, -1 (some) otherwise. */
	int diffCount() const { return m_hasDiff ? -1 : 0; }

	void gotoFirstDiff();
	void gotoNextDiff();
	void gotoPrevDiff();
	void gotoLastDiff();
	/** WinMerge's pane-relative copies (MenuIDtoXY) of the selected bytes. */
	void copyToRight();
	void copyToLeft();
	/** Copy All, after WinMerge's "Copy all differences to other file?". */
	void copyAllToRight();
	void copyAllToLeft();
	void undo();
	void redo();
	bool canUndo() const;
	bool canRedo() const;
	void selectAll();
	/** The Edit menu's Find, Replace, Go To and F3 (with Shift, back), in
	    the active pane, with the editor's dialogs. */
	void showFind();
	void showReplace();
	void showGoTo();
	void findRepeat(bool backward);
	bool activePaneEditable() const;
	void focusNextPane();
	void zoomIn();
	void zoomOut();
	void zoomReset();

	/** WinMerge's OnRefresh: compare the bytes held again. */
	void refreshByUser();
	/** WinMerge's OnFileReload: read the files anew (the read-only sides
	    and the active pane stay). */
	bool reload(QString *error);
	/** Save the modified panes (OnFileSave). */
	bool saveModified(QString *error);
	/** The first file another application changed since it was loaded or
	    saved here; empty when none. */
	QString changedPathOnDisk() const;

	/** Upstream's ChangeFile, which the path bar calls: save what was
	    changed (after asking), then load another file in one pane. */
	bool changeFile(int pane, const QString &path, const QString &description);
	/** How the changes are saved before (PromptAndSaveIfNeeded); false
	    cancels. */
	void setSavePrompt(std::function<bool()> prompt) { m_savePrompt = std::move(prompt); }
	/** Ctrl+L: edit the active pane's path in its bar. */
	void editActivePanePath();

	HexView *paneForTest(int side) const { return m_panes[side]; }
	FilepathEdit *headerForTest(int side) const { return m_headers[side]; }
	int activePane() const { return m_activePane; }
	QString statusTextForTest(int side) const;

signals:
	void rescanned();
	void fileSaved(const QStringList &paths, int significantDiffs);
	void modifiedChanged(bool modified);
	void pathsChanged();

private:
	void setActivePane(int pane);
	HexView *activeView() const;
	bool loadPane(int pane, const QString &path, QString *error);
	bool savePane(int pane, QString *error);
	void updateLastCompareResult();
	void updateHeader(int pane);
	QString untitledName(int pane) const;
	void updateStatus(int pane);
	void updateActions();
	void applyTheme();
	void syncScroll(int pane);

	int m_paneCount = 2;
	QString m_saveAsPath; // where a pane is saved, by the command line
	int m_activePane = 0;
	bool m_hasDiff = false;
	bool m_wasModified = false;
	bool m_syncing = false;
	QString m_paths[3];
	QString m_descriptions[3];
	lm::FileStamp m_stamps[3];
	QSplitter *m_splitter;
	QWidget *m_columns[3];
	FilepathEdit *m_headers[3];
	QString m_headerTexts[3];
	HexView *m_panes[3];
	QWidget *m_statusBars[3];
	HexStatusPosition *m_statusPosition[3];
	QLabel *m_statusMode[3];
	QLabel *m_statusSize[3];
	HexUndoHistory *m_history;
	QAction *m_actUndo = nullptr;
	QAction *m_actRedo = nullptr;
	QAction *m_actSave = nullptr;
	std::function<bool()> m_savePrompt;
};
