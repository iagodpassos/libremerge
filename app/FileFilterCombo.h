// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QComboBox>
#include <QStringList>

class QTimer;

/**
 * The file filter field of WinMerge's open screen and of its File Filters
 * page: an editable combo over the masks used before (CSuperComboBox on
 * "Files\Ext") that parses what is typed and, while it is not a valid
 * mask or filter expression, turns red and tells why (CValidatingEdit).
 */
class FileFilterCombo : public QComboBox
{
	Q_OBJECT
public:
	explicit FileFilterCombo(QWidget *parent = nullptr);

	/** Who parses the text: the error lines, none when it is valid. */
	void setChecker(std::function<QStringList(const QString &)> checker);

	QString mask() const { return currentText(); }
	/** Show a mask. Listed, it is picked from the list, or put on top of
	    it when it is not there (as the open screen shows the filter in
	    use); otherwise only the text changes. */
	void setMask(const QString &mask, bool listed);

	/** The shared history: read it into the list, and save the field's
	    text as its latest entry. */
	void loadHistory();
	void saveHistory();

	/** Parse the text now instead of a moment after the last key. */
	void checkNow();
	QStringList errors() const { return m_errors; }

signals:
	/** The keyboard focus left the field (CBN_KILLFOCUS). */
	void focusLeft();
	/** The text was parsed: errors() is up to date. */
	void checked();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void showState();

	std::function<QStringList(const QString &)> m_checker;
	QStringList m_errors;
	QTimer *m_timer;
};
