// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilepathEdit.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHelpEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QTemporaryFile>
#include <QTime>
#include <QToolTip>

#include "NewComparisonView.h"
#include "Theme.h"

namespace
{

// CEditorFilePathBar's MAX_HISTORY_ITEMS: the recent files listed
constexpr int kMaxRecentItems = 15;
// OnClipboardItemSelected's MAX_TEMP_FILES
constexpr std::size_t kMaxTempFiles = 50;

std::function<void(QMenu *)> s_menuPresenter;
std::function<QString(const QString &)> s_openDialog;

/** paths::EndsWithSlash */
bool endsWithSlash(const QString &text)
{
	return text.endsWith(QLatin1Char('/')) || text.endsWith(QLatin1Char('\\'));
}

/** Upstream's sizes are in points of its status font; these follow the
    field's font instead, a 9-point Segoe UI being 16 pixels high. */
qreal points(const QFontMetrics &metrics, qreal pt)
{
	return pt * 4.0 / 3.0 * metrics.height() / 16.0;
}

/** The recent files of File > Open's path history, the folders left out
    (WinMerge's GetRecentFiles with FilesOnly). */
QStringList recentFiles()
{
	QStringList files;
	for (const QString &entry : NewComparisonView::savedHistory())
	{
		if (endsWithSlash(entry) || QFileInfo(entry).isDir())
			continue;
		files << entry;
		if (files.size() >= kMaxRecentItems)
			break;
	}
	return files;
}

/** locality::TimeString: the time in the user's format, with seconds. */
QString timeString(const QTime &time)
{
	QString format = QLocale().timeFormat(QLocale::LongFormat);
	format.remove(QRegularExpression(QStringLiteral("\\s*t+")));
	return QLocale().toString(time, format.trimmed());
}

} // namespace

FilepathEdit::FilepathEdit(QWidget *parent)
	: QLineEdit(parent)
	, m_recentItems(recentFiles)
{
	setReadOnly(true);
	setFrame(false);
	setFocusPolicy(Qt::ClickFocus);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	setMouseTracking(true);
	// upstream's margins: none on the left, the font's height on the
	// right for the menu's lines (and a little on the left here, like the
	// headers of the other comparisons)
	setTextMargins(4, 3, fontMetrics().height(), 3);
	connect(lm::Theme::instance(), &lm::Theme::changed, this, &FilepathEdit::applyColors);
	applyColors();
}

FilepathEdit::~FilepathEdit() = default;

void FilepathEdit::setOriginalText(const QString &text)
{
	m_originalText = text;
	refreshDisplayText();
}

void FilepathEdit::setPath(const QString &path)
{
	m_path = path;
}

void FilepathEdit::setActive(bool active)
{
	if (m_active == active)
		return;
	m_active = active;
	applyColors();
}

void FilepathEdit::setRecentItemsProvider(std::function<QStringList()> provider)
{
	m_recentItems = std::move(provider);
}

QAction *FilepathEdit::addEditPathShortcut(QWidget *view, std::function<void()> editActivePanePath)
{
	auto *action = new QAction(view);
	action->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_L), QKeySequence(Qt::ALT | Qt::Key_D) });
	action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	view->addAction(action);
	QObject::connect(action, &QAction::triggered, view, std::move(editActivePanePath));
	return action;
}

void FilepathEdit::setMenuPresenterForTest(std::function<void(QMenu *)> presenter)
{
	s_menuPresenter = std::move(presenter);
}

void FilepathEdit::setOpenDialogForTest(std::function<QString(const QString &)> dialog)
{
	s_openDialog = std::move(dialog);
}

QString FilepathEdit::compactPath(const QString &text, const QFontMetrics &metrics, int width)
{
	if (metrics.horizontalAdvance(text) <= width)
		return text;
	// PathCompactPath keeps as much as fits from the left of what is
	// between the first and the last part; upstream reverses that middle
	// around it so that the right of it stays, next to the file name
	const QString dots = QStringLiteral("...");
	const int intro = static_cast<int>(text.indexOf(QLatin1Char('/')));
	const int last = static_cast<int>(text.lastIndexOf(QLatin1Char('/')));
	if (intro >= 0 && intro != last)
	{
		const QString head = text.left(intro + 1);
		const QString middle = text.mid(intro + 1, last - intro - 1);
		const QString tail = text.mid(last);
		const auto fits = [&](int kept) {
			return metrics.horizontalAdvance(head + dots + middle.right(kept) + tail) <= width;
		};
		if (fits(0))
		{
			int low = 0;
			int high = static_cast<int>(middle.size());
			while (low < high)
			{
				const int mid = (low + high + 1) / 2;
				if (fits(mid))
					low = mid;
				else
					high = mid - 1;
			}
			return head + dots + middle.right(low) + tail;
		}
		// then only the last part stays, cut on the right
		return metrics.elidedText(dots + tail, Qt::ElideRight, width);
	}
	return metrics.elidedText(text, Qt::ElideRight, width);
}

QString FilepathEdit::wrapPath(const QString &text, const QFontMetrics &metrics, int width)
{
	QString lines = text;
	int begin = 0;
	while (true)
	{
		// the most characters from begin that fit
		int low = 0;
		int high = static_cast<int>(lines.size()) - begin;
		while (low < high)
		{
			const int mid = (low + high + 1) / 2;
			if (metrics.horizontalAdvance(lines.mid(begin, mid)) <= width)
				low = mid;
			else
				high = mid - 1;
		}
		if (begin + low >= lines.size())
			break;
		int end = qMax(1, low);
		// the line ends after its last separator, when it has one
		const int slash = static_cast<int>(lines.mid(begin, end).lastIndexOf(QLatin1Char('/')));
		if (slash >= 0)
			end = slash + 1;
		lines.insert(begin + end, QLatin1Char('\n'));
		begin += end + 1;
	}
	return lines;
}

QRect FilepathEdit::menuRect() const
{
	// upstream's GetMenuCharRect: 6 points at the right end
	const int width = qMax(8, qRound(points(fontMetrics(), 6.0)));
	return QRect(rect().right() - 3 - width, 0, width, height());
}

QString FilepathEdit::captionWithoutMark() const
{
	// the "* " of a modified file is not part of the caption
	return m_originalText.startsWith(QLatin1Char('*')) ? m_originalText.mid(2) : m_originalText;
}

void FilepathEdit::refreshDisplayText()
{
	if (m_editing)
		return;
	const QMargins margins = textMargins();
	// QLineEdit keeps 2 pixels on each side of the text
	const int width = contentsRect().width() - margins.left() - margins.right() - 4;
	setText(compactPath(m_originalText, fontMetrics(), width));
	setCursorPosition(0);
}

void FilepathEdit::applyColors()
{
	const bool dark = lm::Theme::instance()->dark();
	QString background;
	QString foreground;
	QString border;
	if (m_editing)
	{
		// upstream edits on the window's colors
		background = dark ? QStringLiteral("#1e1e1e") : QStringLiteral("#ffffff");
		foreground = dark ? QStringLiteral("#e0e0e0") : QStringLiteral("#000000");
		border = dark ? QStringLiteral("#46689a") : QStringLiteral("#a0b0c8");
	}
	else if (m_active)
	{
		// the active pane's bar highlighted
		background = dark ? QStringLiteral("#2d4a6b") : QStringLiteral("#d6e4f5");
		foreground = dark ? QStringLiteral("#e8e8e8") : QStringLiteral("#101010");
		border = dark ? QStringLiteral("#46689a") : QStringLiteral("#a0b0c8");
	}
	else
	{
		background = dark ? QStringLiteral("#303030") : QStringLiteral("#ececec");
		foreground = dark ? QStringLiteral("#b0b0b0") : QStringLiteral("#404040");
		border = dark ? QStringLiteral("#454545") : QStringLiteral("#c0c0c0");
	}
	m_textColor = QColor(foreground);
	setStyleSheet(QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3; }")
		.arg(background, foreground, border));
	QFont weighted = font();
	weighted.setWeight(m_active ? QFont::DemiBold : QFont::Normal);
	if (weighted != font())
		setFont(weighted);
	refreshDisplayText();
	update();
}

void FilepathEdit::showMenu(const QPoint &globalPos)
{
	// IDR_POPUP_EDITOR_HEADERBAR, then what the bar adds
	QMenu menu(this);
	QAction *copyAction = menu.addAction(tr("Copy"), this, [this]() { copy(); });
	copyAction->setEnabled(hasSelectedText());
	menu.addAction(tr("&Copy Full Path"), this, [this]() {
		QApplication::clipboard()->setText(captionWithoutMark());
	});
	QAction *copyName = menu.addAction(tr("Copy &Filename"), this, [this]() {
		qsizetype slash = m_originalText.lastIndexOf(QLatin1Char('\\'));
		if (slash < 0)
			slash = m_originalText.lastIndexOf(QLatin1Char('/'));
		QApplication::clipboard()->setText(m_originalText.mid(slash + 1));
	});
	// no file name in a folder's path
	copyName->setEnabled(!endsWithSlash(m_originalText));
	QAction *caption = menu.addAction(tr("&Edit Caption"), this, &FilepathEdit::editCaption);
	caption->setShortcut(QKeySequence(Qt::Key_F2));
	caption->setShortcutContext(Qt::WidgetShortcut);
	menu.addSeparator();
	QAction *pathAction = menu.addAction(tr("Edit &Path"), this, &FilepathEdit::editPath);
	pathAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
	pathAction->setShortcutContext(Qt::WidgetShortcut);
	menu.addAction(tr("Open C&lipboard"), this, &FilepathEdit::openClipboard);
	menu.addAction(tr("&Open\xE2\x80\xA6"), this, &FilepathEdit::selectFile);

	const QStringList recent = m_recentItems ? m_recentItems() : QStringList();
	if (!recent.isEmpty())
	{
		menu.addSeparator();
		QMenu *recentMenu = menu.addMenu(tr("Recent F&iles or Folders"));
		static const char keys[] = "123456789abcdef";
		for (int i = 0; i < recent.size() && i < kMaxRecentItems; ++i)
		{
			const QString entry = recent.at(i);
			QString name = QFileInfo(entry).fileName();
			name.replace(QLatin1Char('&'), QStringLiteral("&&"));
			recentMenu->addAction(QStringLiteral("&%1 %2").arg(QLatin1Char(keys[i]), name),
				this, [this, entry]() { openRecent(entry); });
		}
	}

	if (s_menuPresenter)
		s_menuPresenter(&menu);
	else
		menu.exec(globalPos);
}

void FilepathEdit::editCaption()
{
	startEditing(false);
}

void FilepathEdit::editPath()
{
	startEditing(true);
}

void FilepathEdit::startEditing(bool path)
{
	m_editing = true;
	m_pathEditing = path;
	setReadOnly(false);
	applyColors();
	setText(path ? m_path : captionWithoutMark());
	if (path)
	{
		// SHAutoComplete over the file system
		auto *completer = new QCompleter(this);
		auto *model = new QFileSystemModel(completer);
		model->setOption(QFileSystemModel::DontWatchForChanges);
		model->setFilter(QDir::AllDirs | QDir::Files | QDir::Drives | QDir::NoDotAndDotDot);
		model->setRootPath(QString());
		completer->setModel(model);
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
		completer->setCaseSensitivity(Qt::CaseInsensitive);
#endif
		setCompleter(completer);
	}
	selectAll();
	setFocus(Qt::OtherFocusReason);
}

void FilepathEdit::endEditing()
{
	m_editing = false;
	m_pathEditing = false;
	// the line edit deletes the completer it parents
	setCompleter(nullptr);
	setReadOnly(true);
	applyColors();
}

void FilepathEdit::acceptEditing()
{
	const QString typed = text();
	const bool pathEditing = m_pathEditing;
	endEditing();
	if (pathEditing)
	{
		QString shown;
		if (!(typed == m_path || typed.isEmpty()))
		{
			const QFileInfo info(typed);
			if (!info.exists())
			{
				// upstream shows the system's error in the bar
				QFile probe(typed);
				if (!probe.open(QIODevice::ReadOnly))
					shown = probe.errorString();
				if (shown.isEmpty())
					shown = tr("File not found: %1").arg(typed);
			}
			else if (info.isDir())
			{
				shown = tr("File not found: %1").arg(typed);
			}
			else
			{
				// posted, as upstream's EN_USER_FILE_SELECTED
				QMetaObject::invokeMethod(this, [this, typed]() {
					emit fileSelected(typed, QString());
				}, Qt::QueuedConnection);
			}
		}
		if (shown.isEmpty())
			refreshDisplayText();
		else
		{
			setText(shown);
			setCursorPosition(0);
		}
	}
	else if (typed == captionWithoutMark())
	{
		refreshDisplayText();
	}
	else
	{
		// shown as typed until the pane's caption follows
		setText(typed);
		setCursorPosition(0);
		QMetaObject::invokeMethod(this, [this, typed]() {
			emit captionChanged(typed);
		}, Qt::QueuedConnection);
	}
	emit editEnded();
}

void FilepathEdit::openRecent(const QString &path)
{
	if (!QFileInfo::exists(path))
	{
		setOriginalText(tr("File not found: %1").arg(path));
		return;
	}
	emit fileSelected(path, QString());
}

void FilepathEdit::openClipboard()
{
	// the clipboard's text in a temporary file, as WinMerge's clipboard
	// history gives it without a history (its CreateTempTextFile); an
	// image comparison takes only an image, as a PNG file
	// (CreateTempBitmapFile, and the image frame's filter)
	const QImage image = m_clipboardImages ? QApplication::clipboard()->image() : QImage();
	if (m_clipboardImages && image.isNull())
		return;
	auto file = std::make_shared<QTemporaryFile>(QDir::tempPath()
		+ (m_clipboardImages ? QStringLiteral("/CLPXXXXXX.png") : QStringLiteral("/CLPXXXXXX.txt")));
	if (!file->open())
		return;
	if (m_clipboardImages)
		image.save(file.get(), "PNG");
	else
		file->write(QApplication::clipboard()->text().toUtf8());
	file->close();
	if (m_tempFiles.size() >= kMaxTempFiles)
		m_tempFiles.erase(m_tempFiles.begin(),
			m_tempFiles.begin() + static_cast<std::ptrdiff_t>(m_tempFiles.size() - kMaxTempFiles + 1));
	m_tempFiles.push_back(file);
	emit fileSelected(file->fileName(), tr("Clipboard at %1").arg(timeString(QTime::currentTime())));
}

void FilepathEdit::selectFile()
{
	// the folder of the caption's path
	const QString caption = captionWithoutMark();
	const qsizetype slash = caption.lastIndexOf(QLatin1Char('/'));
	const QString folder = slash > 0 ? caption.left(slash)
		: slash == 0 ? QStringLiteral("/") : QString();
	const QString chosen = s_openDialog ? s_openDialog(folder)
		: QFileDialog::getOpenFileName(this, tr("Open"), folder, tr("All Files (*)"));
	if (!chosen.isEmpty())
		emit fileSelected(chosen, QString());
}

bool FilepathEdit::event(QEvent *event)
{
	if (event->type() == QEvent::ShortcutOverride && !m_editing)
	{
		// the editing keys of the read-only field stay in it, not in the
		// comparison's own (an image's Delete)
		auto *key = static_cast<QKeyEvent *>(event);
		if (key->matches(QKeySequence::Delete) || key->matches(QKeySequence::Cut)
			|| key->matches(QKeySequence::Paste) || key->key() == Qt::Key_Backspace)
		{
			event->accept();
			return true;
		}
	}
	if (event->type() == QEvent::ToolTip)
	{
		// the whole caption, in lines up to 97% of the window or 80% of
		// the screen
		auto *help = static_cast<QHelpEvent *>(event);
		if (m_originalText.isEmpty())
		{
			QToolTip::hideText();
			event->ignore();
			return true;
		}
		int width = qRound(window()->width() * 0.97);
		if (screen() != nullptr)
			width = qMax(width, qRound(screen()->availableGeometry().width() * 0.8));
		QToolTip::showText(help->globalPos(),
			wrapPath(m_originalText, QFontMetrics(QToolTip::font()), width), this);
		return true;
	}
	return QLineEdit::event(event);
}

void FilepathEdit::paintEvent(QPaintEvent *event)
{
	QLineEdit::paintEvent(event);
	if (m_editing)
		return;
	// the menu's three lines
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const QRect area = menuRect();
	const QFontMetrics metrics = fontMetrics();
	QPen pen(m_textColor);
	pen.setWidthF(qMax(1.0, points(metrics, 0.75)));
	painter.setPen(pen);
	const qreal spacing = points(metrics, 2.25);
	const qreal padding = points(metrics, 0.75);
	for (int i = 0; i < 3; ++i)
	{
		const qreal y = area.top() + (area.height() - 2 * spacing) / 2 + i * spacing;
		painter.drawLine(QPointF(area.left() + padding, y), QPointF(area.right() + 1 - padding, y));
	}
}

void FilepathEdit::resizeEvent(QResizeEvent *event)
{
	QLineEdit::resizeEvent(event);
	refreshDisplayText();
}

void FilepathEdit::mousePressEvent(QMouseEvent *event)
{
	if (!m_editing && event->button() == Qt::LeftButton
		&& menuRect().contains(event->position().toPoint()))
	{
		if (!hasFocus())
			setFocus(Qt::MouseFocusReason);
		showMenu(event->globalPosition().toPoint());
		event->accept();
		return;
	}
	QLineEdit::mousePressEvent(event);
}

void FilepathEdit::mouseMoveEvent(QMouseEvent *event)
{
	// the hand over the menu's lines
	setCursor(!m_editing && menuRect().contains(event->position().toPoint())
		? Qt::PointingHandCursor : Qt::IBeamCursor);
	QLineEdit::mouseMoveEvent(event);
}

void FilepathEdit::contextMenuEvent(QContextMenuEvent *event)
{
	if (m_editing)
	{
		QLineEdit::contextMenuEvent(event);
		return;
	}
	if (!hasFocus())
		setFocus(Qt::OtherFocusReason);
	// from the keyboard, at the field's top left corner
	showMenu(event->reason() == QContextMenuEvent::Keyboard
		? mapToGlobal(QPoint(5, 5)) : event->globalPos());
	event->accept();
}

void FilepathEdit::keyPressEvent(QKeyEvent *event)
{
	if (m_editing)
	{
		if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
		{
			acceptEditing();
			event->accept();
			return;
		}
		if (event->key() == Qt::Key_Escape)
		{
			endEditing();
			emit editEnded();
			event->accept();
			return;
		}
		QLineEdit::keyPressEvent(event);
		return;
	}
	// the main window's F2 (ID_DIR_ITEM_RENAME) edits the caption here
	if (event->key() == Qt::Key_F2 && event->modifiers() == Qt::NoModifier)
	{
		editCaption();
		event->accept();
		return;
	}
	// Copy without a selection copies the whole text shown
	if (event->matches(QKeySequence::Copy) && !hasSelectedText())
	{
		QApplication::clipboard()->setText(text());
		event->accept();
		return;
	}
	QLineEdit::keyPressEvent(event);
}

void FilepathEdit::focusInEvent(QFocusEvent *event)
{
	QLineEdit::focusInEvent(event);
	emit focusedIn();
}

void FilepathEdit::focusOutEvent(QFocusEvent *event)
{
	QLineEdit::focusOutEvent(event);
	// leaving the field drops the editing, as upstream's OnKillFocus
	if (m_editing && event->reason() != Qt::PopupFocusReason)
	{
		endEditing();
		refreshDisplayText();
	}
}
