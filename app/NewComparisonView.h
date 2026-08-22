// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>
#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;

/**
 * The "Select Files or Folders" page, mirroring WinMerge's opening
 * screen: three path slots with history, read-only toggles, swap and
 * browse buttons. Accepts files/folders dropped anywhere on it.
 */
class NewComparisonView : public QWidget
{
	Q_OBJECT
public:
	explicit NewComparisonView(QWidget *parent = nullptr);

	/** Fill the first empty slots with the given paths. */
	void addPaths(const QStringList &paths);

signals:
	void compareRequested(const QStringList &paths, const QList<bool> &readOnly,
		bool folders);
	void cancelled();

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;

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
	QStringList history() const;
	void rememberPaths(const QStringList &paths);

	Slot m_slots[3];
	QLabel *m_hint;
};
