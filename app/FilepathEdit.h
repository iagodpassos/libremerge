// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <QColor>
#include <QLineEdit>

class QAction;
class QMenu;
class QTemporaryFile;

/**
 * WinMerge's path bar over a pane (CFilepathEdit, with what its
 * CEditorFilePathBar adds): the pane's caption, the path cut in the
 * middle to fit, in a read-only field whose text can be selected and
 * copied. Its menu, from the ≡ at the right end or a right click, copies
 * the path or the file name, edits the caption (F2) or the path (Ctrl+L)
 * in the field itself, and opens the clipboard, another file or a recent
 * one in the pane.
 */
class FilepathEdit : public QLineEdit
{
	Q_OBJECT
public:
	explicit FilepathEdit(QWidget *parent = nullptr);
	~FilepathEdit() override;

	/** The caption: the path or the description, "* " ahead when the
	    pane is modified. */
	void setOriginalText(const QString &text);
	QString originalText() const { return m_originalText; }
	/** The pane's file, which Edit Path and Open start from. */
	void setPath(const QString &path);
	QString path() const { return m_path; }
	bool isActive() const { return m_active; }
	/** Look active: the pane the commands go to. */
	void setActive(bool active);
	bool isEditing() const { return m_editing; }

	/** The menu's Edit Caption and Edit Path: the field turns editable
	    with the caption or the path, Enter takes it, Esc leaves it. */
	void editCaption();
	void editPath();
	/** The menu, at a point on the screen. */
	void showMenu(const QPoint &globalPos);

	/** Where Recent Files or Folders come from: the path history of
	    File > Open, as WinMerge's MruHelper (replaced for tests). */
	void setRecentItemsProvider(std::function<QStringList()> provider);
	/** Open Clipboard takes an image, for the image comparison, instead
	    of the text. */
	void setClipboardImages(bool images) { m_clipboardImages = images; }

	/** The main window's Ctrl+L and Alt+D (ID_EDITOR_EDIT_PATH) on a
	    comparison: edit the active pane's path. */
	static QAction *addEditPathShortcut(QWidget *view, std::function<void()> editActivePanePath);

	/** The text shown for a path in a width, as WinMerge's
	    RefreshDisplayText: the first and the last part kept, as much as
	    fits from the right of what is between. */
	static QString compactPath(const QString &text, const QFontMetrics &metrics, int width);
	/** The tooltip's lines, as FormatFilePathForDisplayWidth: broken
	    after the last separator that fits. */
	static QString wrapPath(const QString &text, const QFontMetrics &metrics, int width);

	/** For tests: show the menu through this instead of on the screen,
	    and pick the file Open asks for. */
	static void setMenuPresenterForTest(std::function<void(QMenu *)> presenter);
	static void setOpenDialogForTest(std::function<QString(const QString &folder)> dialog);
	QRect menuRectForTest() const { return menuRect(); }

signals:
	/** The field took the focus (its pane becomes the active one). */
	void focusedIn();
	/** Enter took a changed caption. */
	void captionChanged(const QString &text);
	/** A file to load in the pane: from Edit Path, Open, a recent file or
	    the clipboard (with its description). */
	void fileSelected(const QString &path, const QString &description);
	/** Enter or Esc ended the editing. */
	void editEnded();

protected:
	bool event(QEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void focusInEvent(QFocusEvent *event) override;
	void focusOutEvent(QFocusEvent *event) override;

private:
	QRect menuRect() const;
	void refreshDisplayText();
	void startEditing(bool path);
	void endEditing();
	void acceptEditing();
	void openRecent(const QString &path);
	void openClipboard();
	void selectFile();
	void applyColors();
	QString captionWithoutMark() const;

	QString m_originalText;
	QString m_path;
	QColor m_textColor;
	bool m_active = false;
	bool m_editing = false;
	bool m_pathEditing = false;
	bool m_clipboardImages = false;
	std::function<QStringList()> m_recentItems;
	/** The clipboard's temporary files, kept while panes may show them. */
	std::vector<std::shared_ptr<QTemporaryFile>> m_tempFiles;
};
