// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QDialog>

#include "FindText.h"

class QCheckBox;
class QComboBox;
class QListWidget;
class QListWidgetItem;
class QPushButton;

/**
 * WinMerge's Marker dialog (CTextMarkerDlg, IDD_EDIT_MARKER), for
 * Edit > Marker...: the user's markers, each a text (whole word, case,
 * regular expression) marked wherever it shows, in one of three colors.
 * A ticked marker shows; "Enable markers" turns them all off, the
 * search's too. The text the pane gives (its selection within a line,
 * or the word at the cursor) comes as a new marker. New adds one,
 * Delete takes the chosen one out; Apply hands the markers to the panes
 * and OK does so and closes; Cancel leaves the panes with what was last
 * applied.
 */
class TextMarkerDialog : public QDialog
{
	Q_OBJECT
public:
	/** The markers as they are, and, when text is given, a new one for it
	    with these flags. */
	TextMarkerDialog(const QString &text, unsigned flags, QWidget *parent = nullptr);

	/** GetLastSearchFlags: the flags the dialog was left with, which the
	    next new marker starts with (the settings keep them). */
	unsigned lastSearchFlags() const;

	/** Show the dialog modally (DoModal), or hand it to the test's
	    presenter. */
	static bool run(TextMarkerDialog *dialog);
	static void setPresenterForTest(std::function<bool(TextMarkerDialog *dialog)> presenter);

	/** OnOK: Apply, then close. */
	void accept() override;

	QCheckBox *enabledForTest() const { return m_enabledBox; }
	QListWidget *listForTest() const { return m_list; }
	QPushButton *newForTest() const { return m_new; }
	QPushButton *deleteForTest() const { return m_delete; }
	QComboBox *findWhatForTest() const { return m_findWhat; }
	QComboBox *colorForTest() const { return m_color; }
	QCheckBox *wholeWordForTest() const { return m_wholeWordBox; }
	QCheckBox *matchCaseForTest() const { return m_matchCaseBox; }
	QCheckBox *regExpForTest() const { return m_regExpBox; }
	QPushButton *applyForTest() const { return m_apply; }
	const lm::TextMarkers::Map &markersForTest() const { return m_tempMarkers; }

private:
	void addItem(const QString &key, bool select);
	QString keyOf(const QListWidgetItem *item) const;
	/** UpdateDataListView: the fields show a marker, or the marker
	    they show takes what they say. */
	void showMarker(const QString &key);
	void keepFields();
	void selectionChanged();
	void newMarker();
	void deleteMarker();
	void applyNow();
	void findWhatEdited(const QString &text);
	void regExpClicked();

	lm::TextMarkers::Map m_tempMarkers;
	QString m_shownKey; ///< the marker the fields belong to
	QCheckBox *m_enabledBox;
	QListWidget *m_list;
	QPushButton *m_new;
	QPushButton *m_delete;
	QComboBox *m_findWhat;
	QComboBox *m_color;
	QCheckBox *m_wholeWordBox;
	QCheckBox *m_matchCaseBox;
	QCheckBox *m_regExpBox;
	QPushButton *m_apply;
};
