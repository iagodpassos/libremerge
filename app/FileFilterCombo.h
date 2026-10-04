// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QComboBox>
#include <QStringList>

class QTimer;

/**
 * The file filter field of WinMerge's open screen, of its File Filters
 * page and of the folder window's filter bar: an editable combo over the
 * masks used before (CSuperComboBox on "Files\Ext", or on the history of
 * its own the bar keeps) that parses what is typed and, while it is not a
 * valid mask or filter expression, turns red and tells why
 * (CValidatingEdit). The bar's field also shows, in pale yellow, that the
 * filter it holds is the one applied.
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

	/** The saved list this field goes by, "Files/Ext" unless told
	    otherwise. */
	void setHistoryKey(const QString &key) { m_historyKey = key; }
	/** The shared history: read it into the list, and save the field's
	    text as its latest entry. */
	void loadHistory();
	void saveHistory();
	/** CSuperComboBox::LoadState as it is: the list read again and its
	    latest entry shown, whatever the field held. */
	void loadHistoryAndShowLatest();

	/** CValidatingEdit::SetApplied: the text is the filter in use. What
	    the user types or picks from the list clears it. */
	void setApplied(bool applied);
	bool isApplied() const { return m_applied; }

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
	void showTint();
	void themeChanged();

	std::function<QStringList(const QString &)> m_checker;
	QStringList m_errors;
	QString m_historyKey;
	bool m_applied = false;
	QTimer *m_timer;
};
