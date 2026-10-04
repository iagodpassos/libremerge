// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>
#include <QWidget>

class FileFilterCombo;
class FileFilterMenu;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;
class QToolButton;

/**
 * The "Select Files or Folders" page, mirroring WinMerge's opening
 * screen: three path slots with history, read-only toggles, swap and
 * browse buttons, and the "Folder: Filter" field that sets the file
 * filter of folder comparisons. Accepts files/folders dropped anywhere
 * on it.
 */
class NewComparisonView : public QWidget
{
	Q_OBJECT
public:
	explicit NewComparisonView(QWidget *parent = nullptr);

	/** Fill the first empty slots with the given paths. */
	void addPaths(const QStringList &paths);

	/** Put the keyboard focus on the first path field. */
	void focusFirstField();

	/** The path fields' history (WinMerge's Files\Left, Files\Right...),
	    which also feeds the "recent list" auto completion. */
	static QStringList savedHistory();
	/** Forget it, part of WinMerge's "Clear all recent items". */
	static void clearSavedHistory();
	/** Refill the path fields' dropdowns from the saved history. */
	void reloadHistory();

	/** Run the path check now, and read what it showed (for tests). */
	void verifyPathsForTest() { verifyPaths(); }
	/** The "Folder: Filter" field (for tests). */
	FileFilterCombo *filterFieldForTest() const { return m_filterCombo; }
	QString hintForTest() const;
	bool compareEnabledForTest() const;

signals:
	void compareRequested(const QStringList &paths, const QList<bool> &readOnly,
		bool folders);
	void cancelled();

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	/** Shift+Delete on an open path dropdown forgets the highlighted
	    path, like WinMerge's CSuperComboBox. */
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void compare();

private:
	struct Slot
	{
		QComboBox *path;
		QCheckBox *readOnly;
	};

	void swapSlots(int a, int b);
	void browse(int slot, bool folder);
	void setHint(const QString &text, bool error);
	/** WinMerge's "Automatically verify paths" check of the fields. */
	void verifyPaths();
	void rememberPaths(const QStringList &paths);
	/** WinMerge's filter group follows the paths' kind. */
	void showFilterGroup(bool visible, bool enabled);
	void applyFileFilter();
	/** Drop one path from the history and from every field's dropdown. */
	void forgetPath(const QString &path);

	Slot m_slots[3];
	QLabel *m_filterTitle = nullptr;
	FileFilterCombo *m_filterCombo = nullptr;
	QToolButton *m_selectFilterButton = nullptr;
	FileFilterMenu *m_filterMenu = nullptr;
	QLabel *m_hint;
	QPushButton *m_compareButton = nullptr;
	QTimer *m_verifyTimer = nullptr; // coalesces typing into one check
	bool m_compareArmed = false; // a compare already ran this cycle
};
