// SPDX-License-Identifier: GPL-3.0-or-later
#include "NewComparisonView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace
{

const QString kHistoryKey = QStringLiteral("NewComparison/History");
constexpr int kHistoryMax = 12;

} // namespace

NewComparisonView::NewComparisonView(QWidget *parent)
	: QWidget(parent)
{
	setAcceptDrops(true);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	auto *content = new QWidget(this);
	auto *layout = new QVBoxLayout(content);
	layout->setContentsMargins(40, 24, 40, 16);
	layout->setSpacing(6);

	// wordmark + top-right buttons (WinMerge-style header)
	auto *header = new QGridLayout;
	auto *wordmark = new QLabel(QStringLiteral(
		"<span style=\"font-size:34px;font-weight:800;color:#2b6c80\">Libre</span>"
		"<span style=\"font-size:34px;font-weight:800;color:#d97c00\">Merge</span>"), content);
	header->addWidget(wordmark, 0, 0, 2, 1, Qt::AlignLeft | Qt::AlignTop);
	header->setColumnStretch(1, 1);
	auto *optionsButton = new QPushButton(tr("Options..."), content);
	connect(optionsButton, &QPushButton::clicked, this, [this]() {
		QMetaObject::invokeMethod(window(), "showOptions");
	});
	header->addWidget(optionsButton, 0, 2);
	layout->addLayout(header);
	layout->addSpacing(10);

	const QString slotTitles[3] = {
		tr("1st File or Folder"),
		tr("2nd File or Folder"),
		tr("3rd File or Folder (Optional)"),
	};
	const QStringList pathHistory = history();
	for (int i = 0; i < 3; ++i)
	{
		auto *title = new QLabel(slotTitles[i], content);
		QFont bold = title->font();
		bold.setBold(true);
		title->setFont(bold);
		layout->addWidget(title);

		m_slots[i].path = new QComboBox(content);
		m_slots[i].path->setEditable(true);
		m_slots[i].path->setInsertPolicy(QComboBox::NoInsert);
		m_slots[i].path->addItems(pathHistory);
		m_slots[i].path->setCurrentText(QString());
		m_slots[i].path->lineEdit()->setPlaceholderText(
			tr("Type a path, pick a recent one, drop a file here or browse\xE2\x80\xA6"));
		// the editable combo's line edit must not swallow drops: it would
		// paste the raw "file://" URI (evpix's report on Linux, #2); with
		// drops off the event reaches our dropEvent, which resolves the
		// local path and targets the slot under the cursor
		m_slots[i].path->lineEdit()->setAcceptDrops(false);
		layout->addWidget(m_slots[i].path);

		auto *row = new QGridLayout;
		m_slots[i].readOnly = new QCheckBox(tr("Read-only"), content);
		row->addWidget(m_slots[i].readOnly, 0, 0);
		auto *swapButton = new QPushButton(content);
		const int a = (i == 2) ? 0 : i;
		const int b = (i == 0) ? 1 : 2;
		swapButton->setText(tr("\xE2\x87\x85 Swap %1 | %2").arg(a + 1).arg(b + 1));
		connect(swapButton, &QPushButton::clicked, this, [this, a, b]() { swapSlots(a, b); });
		row->addWidget(swapButton, 0, 1);
		row->setColumnStretch(2, 1);
		auto *browseButton = new QToolButton(content);
		browseButton->setText(tr("Browse..."));
		browseButton->setPopupMode(QToolButton::InstantPopup);
		auto *browseMenu = new QMenu(browseButton);
		browseMenu->addAction(tr("File..."), this, [this, i]() { browse(i, false); });
		browseMenu->addAction(tr("Folder..."), this, [this, i]() { browse(i, true); });
		browseButton->setMenu(browseMenu);
		row->addWidget(browseButton, 0, 3);
		layout->addLayout(row);
		layout->addSpacing(8);
	}

	layout->addStretch(1);

	auto *buttons = new QGridLayout;
	buttons->setColumnStretch(0, 1);
	auto *compareButton = new QPushButton(tr("Compare"), content);
	compareButton->setDefault(true);
	connect(compareButton, &QPushButton::clicked, this, &NewComparisonView::compare);
	buttons->addWidget(compareButton, 0, 1);
	auto *cancelButton = new QPushButton(tr("Cancel"), content);
	connect(cancelButton, &QPushButton::clicked, this, &NewComparisonView::cancelled);
	buttons->addWidget(cancelButton, 0, 2);
	layout->addLayout(buttons);

	outer->addWidget(content, 1);

	auto *line = new QFrame(this);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	outer->addWidget(line);
	m_hint = new QLabel(this);
	m_hint->setContentsMargins(8, 4, 8, 4);
	outer->addWidget(m_hint);
	setHint(tr("Select two (or three) folders/files to compare."), false);
}

void NewComparisonView::addPaths(const QStringList &paths)
{
	int slot = 0;
	for (const QString &path : paths)
	{
		while (slot < 3 && !m_slots[slot].path->currentText().trimmed().isEmpty())
			++slot;
		if (slot >= 3)
			break;
		m_slots[slot].path->setCurrentText(QFileInfo(path).absoluteFilePath());
	}
}

void NewComparisonView::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}

void NewComparisonView::dropEvent(QDropEvent *event)
{
	QStringList paths;
	for (const QUrl &url : event->mimeData()->urls())
	{
		if (url.isLocalFile())
			paths.append(url.toLocalFile());
	}
	if (paths.isEmpty())
		return;

	// dropping directly onto a slot's row targets that slot
	for (int i = 0; i < 3; ++i)
	{
		const QRect area = m_slots[i].path->geometry()
			.adjusted(0, -24, 0, 28)
			.translated(m_slots[i].path->parentWidget()->mapTo(this, QPoint(0, 0)));
		if (area.contains(event->position().toPoint()))
		{
			m_slots[i].path->setCurrentText(QFileInfo(paths.first()).absoluteFilePath());
			paths.removeFirst();
			break;
		}
	}
	addPaths(paths);
	event->acceptProposedAction();
}

void NewComparisonView::swapSlots(int a, int b)
{
	const QString pathA = m_slots[a].path->currentText();
	const bool roA = m_slots[a].readOnly->isChecked();
	m_slots[a].path->setCurrentText(m_slots[b].path->currentText());
	m_slots[a].readOnly->setChecked(m_slots[b].readOnly->isChecked());
	m_slots[b].path->setCurrentText(pathA);
	m_slots[b].readOnly->setChecked(roA);
}

void NewComparisonView::browse(int slot, bool folder)
{
	const QString start = QFileInfo(m_slots[slot].path->currentText()).absolutePath();
	const QString path = folder
		? QFileDialog::getExistingDirectory(this, tr("Select Folder"), start)
		: QFileDialog::getOpenFileName(this, tr("Select File"), start);
	if (!path.isEmpty())
		m_slots[slot].path->setCurrentText(path);
}

void NewComparisonView::setHint(const QString &text, bool error)
{
	m_hint->setText(text);
	m_hint->setStyleSheet(error ? QStringLiteral("color:#c03030") : QString());
}

QStringList NewComparisonView::history() const
{
	return QSettings().value(kHistoryKey).toStringList();
}

void NewComparisonView::rememberPaths(const QStringList &paths)
{
	QSettings settings;
	QStringList mru = settings.value(kHistoryKey).toStringList();
	for (const QString &path : paths)
	{
		mru.removeAll(path);
		mru.prepend(path);
	}
	while (mru.size() > kHistoryMax)
		mru.removeLast();
	settings.setValue(kHistoryKey, mru);
}

void NewComparisonView::compare()
{
	QStringList paths;
	QList<bool> readOnly;
	for (int i = 0; i < 3; ++i)
	{
		QString path = m_slots[i].path->currentText().trimmed();
		if (path.isEmpty())
			continue;
		// pasted or typed "file://" URIs still resolve (percent-encoding
		// included) instead of failing as nonexistent paths
		if (path.startsWith(QStringLiteral("file://")))
		{
			const QString local = QUrl(path).toLocalFile();
			if (!local.isEmpty())
				path = local;
		}
		paths.append(path);
		readOnly.append(m_slots[i].readOnly->isChecked());
	}
	if (paths.size() < 2)
	{
		setHint(tr("Select two (or three) folders/files to compare."), true);
		return;
	}

	int files = 0, dirs = 0;
	for (const QString &path : paths)
	{
		const QFileInfo info(path);
		if (info.isFile()) ++files;
		else if (info.isDir()) ++dirs;
		else
		{
			setHint(tr("Path does not exist: %1").arg(path), true);
			return;
		}
	}
	const bool folders = dirs == paths.size();
	if (!folders && files != paths.size())
	{
		setHint(tr("Mixing files and folders is not supported \xE2\x80\x94 "
			"select two files, three files or two folders."), true);
		return;
	}
	if (folders && paths.size() == 3)
	{
		setHint(tr("3-way folder comparison is not available yet \xE2\x80\x94 "
			"select two folders."), true);
		return;
	}

	rememberPaths(paths);
	emit compareRequested(paths, readOnly, folders);
}
