// SPDX-License-Identifier: GPL-3.0-or-later
#include "HexCompareView.h"

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QToolBar>
#include <QVBoxLayout>

#include "HexView.h"
#include "Icons.h"
#include "MessageBoxes.h"
#include "Theme.h"

/** The first part of the editor's status bar: the caret's offset and
    bits on the left, the values there on the right. When they do not fit,
    the bits go first, then the offset, as upstream's set_wnd_title cuts
    them. */
class HexStatusPosition : public QWidget
{
public:
	explicit HexStatusPosition(QWidget *parent) : QWidget(parent)
	{
		setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	}

	void setTexts(const QString &position, const QString &bits, const QString &values)
	{
		m_position = position;
		m_bits = bits;
		m_values = values;
		updateGeometry();
		update();
	}

	/** The left text as shown at the current width. */
	QString shownLeft() const
	{
		const QFontMetrics metrics(font());
		const QString full = joined(m_position, m_bits);
		const int gap = metrics.horizontalAdvance(QStringLiteral("   "));
		const int values = m_values.isEmpty() ? 0 : gap + metrics.horizontalAdvance(m_values);
		if (metrics.horizontalAdvance(full) + values <= width())
			return full;
		if (width() < metrics.horizontalAdvance(full) - metrics.horizontalAdvance(m_position) + values)
			return QString();
		return m_position;
	}

	QString text() const { return joined(m_position, m_bits) + QStringLiteral(" | ") + m_values; }

	QSize sizeHint() const override
	{
		const QFontMetrics metrics(font());
		return QSize(metrics.horizontalAdvance(text()), metrics.height());
	}

	QSize minimumSizeHint() const override
	{
		return QSize(0, QFontMetrics(font()).height());
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setPen(palette().color(QPalette::WindowText));
		painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, shownLeft());
		painter.drawText(rect(), Qt::AlignRight | Qt::AlignVCenter, m_values);
	}

private:
	static QString joined(const QString &position, const QString &bits)
	{
		return bits.isEmpty() ? position : position + QStringLiteral("   ") + bits;
	}

	QString m_position;
	QString m_bits;
	QString m_values;
};

HexCompareView::HexCompareView(QWidget *parent)
	: QWidget(parent)
	, m_history(new HexUndoHistory(this))
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto *toolbar = new QToolBar(this);
	toolbar->setIconSize(QSize(16, 16));
	toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	// the shortcuts live on the main window's menu; the toolbar only
	// mentions them in tooltips
	const auto addToolAction = [this, toolbar](lm::Icon icon, const QString &text,
		const QString &shortcutHint, auto slot) -> QAction * {
		QAction *action = toolbar->addAction(lm::icon(icon), text);
		action->setData(static_cast<int>(icon));
		action->setToolTip(shortcutHint.isEmpty() ? text
			: QStringLiteral("%1 (%2)").arg(text, shortcutHint));
		connect(action, &QAction::triggered, this, slot);
		return action;
	};
	addToolAction(lm::Icon::FirstDiff, tr("First Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x96"), [this]() { gotoFirstDiff(); });
	addToolAction(lm::Icon::PrevDiff, tr("Previous Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x91"), [this]() { gotoPrevDiff(); });
	addToolAction(lm::Icon::NextDiff, tr("Next Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x93"), [this]() { gotoNextDiff(); });
	addToolAction(lm::Icon::LastDiff, tr("Last Difference"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x98"), [this]() { gotoLastDiff(); });
	toolbar->addSeparator();
	addToolAction(lm::Icon::CopyRight, tr("Copy to Right"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x92"), [this]() { copyToRight(); });
	addToolAction(lm::Icon::CopyLeft, tr("Copy to Left"),
		QString::fromUtf8("\xE2\x8C\xA5\xE2\x86\x90"), [this]() { copyToLeft(); });
	addToolAction(lm::Icon::CopyAllRight, tr("Copy All to Right"), QString(),
		[this]() { copyAllToRight(); });
	addToolAction(lm::Icon::CopyAllLeft, tr("Copy All to Left"), QString(),
		[this]() { copyAllToLeft(); });
	toolbar->addSeparator();
	m_actUndo = addToolAction(lm::Icon::Undo, tr("Undo"),
		QString::fromUtf8("\xE2\x8C\x98Z"), [this]() { undo(); });
	m_actRedo = addToolAction(lm::Icon::Redo, tr("Redo"),
		QString::fromUtf8("\xE2\x87\xA7\xE2\x8C\x98Z"), [this]() { redo(); });
	toolbar->addSeparator();
	addToolAction(lm::Icon::Refresh, tr("Recompare"), QStringLiteral("F5"),
		[this]() { refreshByUser(); });
	m_actSave = addToolAction(lm::Icon::Save, tr("Save"),
		QString::fromUtf8("\xE2\x8C\x98S"), [this]() {
			QString error;
			if (!saveModified(&error) && !error.isEmpty())
				lm::showError(this, error);
		});
	layout->addWidget(toolbar);

	m_splitter = new QSplitter(Qt::Horizontal, this);
	m_splitter->setChildrenCollapsible(false);
	for (int i = 0; i < 3; ++i)
	{
		auto *column = new QWidget(this);
		m_columns[i] = column;
		auto *columnLayout = new QVBoxLayout(column);
		columnLayout->setContentsMargins(0, 0, 0, 0);
		columnLayout->setSpacing(0);
		m_headers[i] = new QLabel(column);
		m_headers[i]->setContentsMargins(6, 3, 6, 3);
		m_headers[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		columnLayout->addWidget(m_headers[i]);
		m_panes[i] = new HexView(column);
		m_panes[i]->setUndoHistory(m_history);
		columnLayout->addWidget(m_panes[i], 1);

		// the editor's status bar (CreateHexWndStatusBar): what is at the
		// caret, then the character set and mode, then the size
		m_statusBars[i] = new QWidget(column);
		auto *status = new QHBoxLayout(m_statusBars[i]);
		status->setContentsMargins(6, 2, 6, 2);
		status->setSpacing(12);
		m_statusPosition[i] = new HexStatusPosition(m_statusBars[i]);
		m_statusMode[i] = new QLabel(m_statusBars[i]);
		m_statusSize[i] = new QLabel(m_statusBars[i]);
		for (QLabel *label : { m_statusMode[i], m_statusSize[i] })
			label->setAlignment(Qt::AlignCenter);
		status->addWidget(m_statusPosition[i], 1);
		status->addWidget(m_statusMode[i]);
		status->addWidget(m_statusSize[i]);
		columnLayout->addWidget(m_statusBars[i]);
		m_splitter->addWidget(column);
		if (i == 2)
			column->setVisible(false);

		connect(m_panes[i], &HexView::statusChanged, this, [this, i]() {
			updateStatus(i);
			updateHeader(i);
			updateActions();
		});
		connect(m_panes[i], &HexView::dataEdited, this, [this]() {
			updateLastCompareResult();
			for (int p = 0; p < m_paneCount; ++p)
				updateHeader(p);
			updateActions();
		});
		connect(m_panes[i], &HexView::focused, this, [this, i]() { setActivePane(i); });
		connect(m_panes[i], &HexView::scrolled, this, [this, i]() { syncScroll(i); });
	}
	layout->addWidget(m_splitter, 1);
	connect(m_history, &HexUndoHistory::changed, this, &HexCompareView::updateActions);
	connect(lm::Theme::instance(), &lm::Theme::changed, this, &HexCompareView::applyTheme);
	applyTheme();
}

HexCompareView::~HexCompareView() = default;

bool HexCompareView::loadPane(int pane, const QString &path, QString *error)
{
	QByteArray data;
	if (!path.isEmpty())
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			if (error != nullptr)
				*error = tr("cannot open %1").arg(path);
			return false;
		}
		data = file.readAll();
		m_stamps[pane] = lm::fileStamp(path);
	}
	m_paths[pane] = path;
	m_history->clear(m_panes[pane]);
	m_panes[pane]->setData(data);
	return true;
}

bool HexCompareView::compare(const QStringList &paths, QString *error)
{
	if (paths.size() != 2 && paths.size() != 3)
	{
		if (error != nullptr)
			*error = tr("expected 2 or 3 files");
		return false;
	}
	m_paneCount = static_cast<int>(paths.size());
	m_columns[2]->setVisible(m_paneCount == 3);
	// upstream's frame tells the editors of each other: the middle pane
	// of three is compared with both sides, a side with the middle
	if (m_paneCount == 2)
	{
		m_panes[0]->setSiblings(m_panes[1]);
		m_panes[1]->setSiblings(m_panes[0]);
	}
	else
	{
		m_panes[0]->setSiblings(m_panes[1]);
		m_panes[1]->setSiblings(m_panes[0], m_panes[2]);
		m_panes[2]->setSiblings(m_panes[1]);
	}
	for (int i = 0; i < m_paneCount; ++i)
	{
		if (!loadPane(i, paths.at(i), error))
			return false;
	}
	m_history->clear();
	updateLastCompareResult();
	for (int i = 0; i < m_paneCount; ++i)
	{
		updateHeader(i);
		updateStatus(i);
	}
	setActivePane(0);
	updateActions();
	return true;
}

QStringList HexCompareView::paths() const
{
	QStringList list;
	for (int i = 0; i < m_paneCount; ++i)
		list << m_paths[i];
	return list;
}

QString HexCompareView::tabTitle() const
{
	const auto name = [this](int side) {
		if (!m_descriptions[side].isEmpty())
			return m_descriptions[side];
		return QFileInfo(m_paths[side]).fileName();
	};
	QStringList names;
	for (int i = 0; i < m_paneCount; ++i)
		names << name(i);
	return names.join(QString::fromUtf8(" \xE2\x86\x94 "));
}

void HexCompareView::setSideDescription(int side, const QString &description)
{
	if (side < 0 || side >= 3)
		return;
	m_descriptions[side] = description;
	updateHeader(side);
}

void HexCompareView::setReadOnlySides(const QList<bool> &readOnly)
{
	for (int i = 0; i < m_paneCount; ++i)
		m_panes[i]->setReadOnly(readOnly.value(i, false));
}

bool HexCompareView::isModified() const
{
	for (int i = 0; i < m_paneCount; ++i)
		if (m_panes[i]->isModified())
			return true;
	return false;
}

/** Upstream's UpdateLastCompareResult: whether the panes hold the same
    bytes, the length first. */
void HexCompareView::updateLastCompareResult()
{
	bool diff = false;
	for (int i = 1; i < m_paneCount && !diff; ++i)
		diff = m_panes[i]->data() != m_panes[0]->data();
	m_hasDiff = diff;
}

void HexCompareView::setActivePane(int pane)
{
	if (pane < 0 || pane >= m_paneCount)
		return;
	m_activePane = pane;
	for (int i = 0; i < m_paneCount; ++i)
		updateHeader(i);
}

HexView *HexCompareView::activeView() const
{
	return m_panes[m_activePane];
}

void HexCompareView::syncScroll(int pane)
{
	// upstream's frame keeps the panes' scroll bars together
	if (m_syncing)
		return;
	m_syncing = true;
	for (int i = 0; i < m_paneCount; ++i)
	{
		if (i == pane)
			continue;
		m_panes[i]->verticalScrollBar()->setValue(m_panes[pane]->verticalScrollBar()->value());
		m_panes[i]->horizontalScrollBar()->setValue(m_panes[pane]->horizontalScrollBar()->value());
	}
	m_syncing = false;
}

// --- the Merge menu ---------------------------------------------------------

void HexCompareView::gotoFirstDiff()
{
	activeView()->selectNextDiff(true);
}

void HexCompareView::gotoNextDiff()
{
	activeView()->selectNextDiff(false);
}

void HexCompareView::gotoPrevDiff()
{
	activeView()->selectPrevDiff(false);
}

void HexCompareView::gotoLastDiff()
{
	activeView()->selectPrevDiff(true);
}

void HexCompareView::copyToRight()
{
	const int target = qMin(m_activePane + 1, m_paneCount - 1);
	m_panes[target]->copySelectionFrom(m_panes[target - 1]);
}

void HexCompareView::copyToLeft()
{
	const int target = qMax(m_activePane - 1, 0);
	m_panes[target]->copySelectionFrom(m_panes[target + 1]);
}

void HexCompareView::copyAllToRight()
{
	const int target = qMin(m_activePane + 1, m_paneCount - 1);
	if (m_panes[target]->isReadOnly() || !lm::askCopyAllDiffs(this))
		return;
	m_panes[target]->copyAllFrom(m_panes[target - 1]);
}

void HexCompareView::copyAllToLeft()
{
	const int target = qMax(m_activePane - 1, 0);
	if (m_panes[target]->isReadOnly() || !lm::askCopyAllDiffs(this))
		return;
	m_panes[target]->copyAllFrom(m_panes[target + 1]);
}

void HexCompareView::undo()
{
	m_history->undo();
}

void HexCompareView::redo()
{
	m_history->redo();
}

bool HexCompareView::canUndo() const
{
	return m_history->canUndo();
}

bool HexCompareView::canRedo() const
{
	return m_history->canRedo();
}

void HexCompareView::selectAll()
{
	activeView()->selectAll();
}

void HexCompareView::focusNextPane()
{
	m_panes[(m_activePane + 1) % m_paneCount]->setFocus();
}

void HexCompareView::zoomIn()
{
	for (int i = 0; i < m_paneCount; ++i)
		m_panes[i]->zoom(1);
}

void HexCompareView::zoomOut()
{
	for (int i = 0; i < m_paneCount; ++i)
		m_panes[i]->zoom(-1);
}

void HexCompareView::zoomReset()
{
	for (int i = 0; i < m_paneCount; ++i)
		m_panes[i]->zoom(0);
}

// --- files ------------------------------------------------------------------

void HexCompareView::refreshByUser()
{
	updateLastCompareResult();
	emit rescanned();
}

bool HexCompareView::reload(QString *error)
{
	for (int i = 0; i < m_paneCount; ++i)
	{
		if (!m_paths[i].isEmpty() && !QFileInfo(m_paths[i]).isReadable())
		{
			if (error != nullptr)
				*error = tr("cannot open %1").arg(m_paths[i]);
			return false;
		}
	}
	const int active = m_activePane;
	for (int i = 0; i < m_paneCount; ++i)
	{
		const bool readOnly = m_panes[i]->isReadOnly();
		if (!loadPane(i, m_paths[i], error))
			return false;
		m_panes[i]->setReadOnly(readOnly);
	}
	m_history->clear();
	updateLastCompareResult();
	for (int i = 0; i < m_paneCount; ++i)
		updateHeader(i);
	m_panes[active]->setFocus();
	updateActions();
	return true;
}

QString HexCompareView::changedPathOnDisk() const
{
	for (int i = 0; i < m_paneCount; ++i)
		if (!m_paths[i].isEmpty()
			&& lm::fileChangedOnDisk(m_paths[i], m_stamps[i]) == lm::FileChange::Changed)
			return m_paths[i];
	return QString();
}

/** Upstream's DoFileSave: writing over a file another application changed
    asks first, a backup comes first when the option asks for one, and the
    data goes through an intermediate file. */
bool HexCompareView::savePane(int pane, QString *error)
{
	HexView *view = m_panes[pane];
	if (!view->isModified() || m_paths[pane].isEmpty())
		return true;
	const QString &path = m_paths[pane];
	if (lm::fileChangedOnDisk(path, m_stamps[pane]) == lm::FileChange::Changed
		&& !lm::askOverwriteChangedFile(this, path))
		return true;
	const QDateTime originalTime = lm::timeToPreserve(path);
	if (QSettings().value(QStringLiteral("Backup/FileCompare"), true).toBool()
		&& QFile::exists(path))
	{
		const QString backupPath = path + QStringLiteral(".bak");
		QFile::remove(backupPath);
		if (!QFile::copy(path, backupPath))
		{
			if (error != nullptr)
				*error = tr("could not create the backup file %1").arg(backupPath);
			return false;
		}
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(view->data()) != view->length()
		|| !file.commit())
	{
		if (error != nullptr)
			*error = tr("cannot write %1").arg(path);
		return false;
	}
	lm::restoreFileTime(path, originalTime);
	m_stamps[pane] = lm::fileStamp(path);
	view->setSavePoint();
	updateHeader(pane);
	return true;
}

bool HexCompareView::saveModified(QString *error)
{
	bool saved = false;
	for (int i = 0; i < m_paneCount; ++i)
	{
		if (!m_panes[i]->isModified())
			continue;
		if (!savePane(i, error))
			return false;
		saved = true;
	}
	if (saved)
	{
		updateLastCompareResult();
		updateActions();
		emit fileSaved(paths(), diffCount());
	}
	return true;
}

// --- the frame --------------------------------------------------------------

/** Upstream's UpdateHeaderPath: the description, or the file; "* " while
    the pane holds unsaved changes. */
void HexCompareView::updateHeader(int pane)
{
	QString text = m_descriptions[pane].isEmpty()
		? QFileInfo(m_paths[pane]).fileName() : m_descriptions[pane];
	if (text.isEmpty())
	{
		if (pane == 0)
			text = tr("Untitled Left");
		else if (pane == 1 && m_paneCount == 3)
			text = tr("Untitled Middle");
		else
			text = tr("Untitled Right");
	}
	if (m_panes[pane]->isModified())
		text.prepend(QStringLiteral("* "));
	m_headers[pane]->setText(text);
	m_headers[pane]->setToolTip(m_paths[pane]);
	const bool dark = lm::Theme::instance()->dark();
	const bool active = pane == m_activePane;
	// the active pane's header highlighted, like WinMerge's file bar
	const QString bg = dark ? (active ? QStringLiteral("#3d5a86") : QStringLiteral("#2c2c2c"))
		: (active ? QStringLiteral("#c8d8ee") : QStringLiteral("#ececec"));
	const QString fg = dark ? QStringLiteral("#d4d4d4") : QStringLiteral("#303030");
	m_headers[pane]->setStyleSheet(QStringLiteral(
		"QLabel { background: %1; color: %2; font-weight: %3; }")
		.arg(bg, fg, active ? QStringLiteral("bold") : QStringLiteral("normal")));
}

void HexCompareView::updateStatus(int pane)
{
	const HexView *view = m_panes[pane];
	m_statusPosition[pane]->setTexts(view->statusPositionText(), view->statusBitsText(),
		view->statusValuesText());
	m_statusMode[pane]->setText(view->statusModeText());
	m_statusSize[pane]->setText(view->statusSizeText());
}

QString HexCompareView::statusTextForTest(int side) const
{
	return QStringList{ m_statusPosition[side]->text(), m_statusMode[side]->text(),
		m_statusSize[side]->text() }.join(QStringLiteral(" | "));
}

void HexCompareView::updateActions()
{
	if (m_actUndo != nullptr)
		m_actUndo->setEnabled(m_history->canUndo());
	if (m_actRedo != nullptr)
		m_actRedo->setEnabled(m_history->canRedo());
	const bool modified = isModified();
	if (m_actSave != nullptr)
		m_actSave->setEnabled(modified);
	if (modified != m_wasModified)
	{
		m_wasModified = modified;
		emit modifiedChanged(modified);
	}
}

void HexCompareView::applyTheme()
{
	const bool dark = lm::Theme::instance()->dark();
	const QString style = dark
		? QStringLiteral("QWidget { background: #2c2c2c; color: #b8b8b8; }")
		: QStringLiteral("QWidget { background: #ececec; color: #303030; }");
	for (int i = 0; i < 3; ++i)
	{
		m_statusBars[i]->setStyleSheet(style);
		updateHeader(i);
		m_panes[i]->viewport()->update();
	}
	lm::applyToolbarTheme(this);
}
