// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <QString>
#include <QWidget>

class FileFilterCombo;
class FileFilterHelper;
class FileFilterMenu;
class QPushButton;
class QToolButton;

/**
 * WinMerge's display filter bar (CDirFilterBar): the strip a folder
 * window shows above its list for a filter that only hides items of the
 * comparison it already has. A field for the mask or filter expression,
 * checked as it is typed and with a history of its own
 * ("Files\DisplayExt"), the "=" menu that puts one together, Apply and
 * Close. The folder window owns the filter itself and applies it.
 */
class DisplayFilterBar : public QWidget
{
	Q_OBJECT
public:
	explicit DisplayFilterBar(QWidget *parent = nullptr);
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
	FileFilterMenu *menuForTest() const { return m_menu; }

signals:
	/** The Apply button, or Enter in the field. */
	void applyRequested();
	/** The Close button, or Esc. */
	void closeRequested();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	// a filter of its own to parse what is typed with
	std::unique_ptr<FileFilterHelper> m_checker;
	FileFilterCombo *m_field;
	QToolButton *m_menuButton;
	FileFilterMenu *m_menu;
	QPushButton *m_apply;
	QPushButton *m_close;
};
