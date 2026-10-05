// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <QString>
#include <QWidget>

class FileFilterCombo;
class FileFilterHelper;
class LineFilterHelper;
class QMenu;
class QPushButton;
class QToolButton;

/**
 * WinMerge's display filter bar, the strip a window shows above its
 * content for a filter that only hides what the comparison already has:
 * items of a folder window (CDirFilterBar) or lines of a file window
 * (CLineFilterBar). A field for the filter, checked as it is typed and
 * with a history of its own ("Files\DisplayExt", "Files\DisplayLine"),
 * the "=" menu that puts one together, Apply and Close. The window owns
 * the filter itself and applies it.
 */
class DisplayFilterBar : public QWidget
{
	Q_OBJECT
public:
	enum Kind
	{
		Items, ///< a folder window: a file mask or filter expression
		Lines, ///< a file window: a text to find or a line expression
	};
	explicit DisplayFilterBar(Kind kind, QWidget *parent = nullptr);
	~DisplayFilterBar() override;

	/** What the field holds (GetFilterText). */
	QString filterText() const;
	/** Put a filter in the field (SetDlgItemText on its combo). */
	void setFilterText(const QString &text);
	/** SaveFilterText: the field's text becomes the latest of the list,
	    unless the field is empty. */
	void saveFilterText();
	/** SetFilterApplied: the field shows that its filter is in use. */
	void setFilterApplied(bool applied);
	void focusField();

	FileFilterCombo *field() const { return m_field; }
	/** The "=" button's menu: a FileFilterMenu or a LineFilterMenu. */
	QMenu *menuForTest() const { return m_menu; }

signals:
	/** The Apply button, or Enter in the field. */
	void applyRequested();
	/** The Close button, or Esc. */
	void closeRequested();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	void showMenu();
	void takeFromMenu(const QString &filter);

	Kind m_kind;
	// a filter of its own to parse what is typed with
	std::unique_ptr<FileFilterHelper> m_checker;
	std::unique_ptr<LineFilterHelper> m_lineChecker;
	FileFilterCombo *m_field;
	QToolButton *m_menuButton;
	QMenu *m_menu;
	QPushButton *m_apply;
	QPushButton *m_close;
};
