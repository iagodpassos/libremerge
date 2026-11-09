// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QComboBox>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace lm
{

class WildcardDropListPopup;

/**
 * WinMerge's WildcardDropList (Src/WildcardDropList.cpp): a field of
 * wildcard patterns whose drop-down is a grid of buttons, one for each
 * default pattern and, unless switched off, one for each pattern of the
 * field's own; a button is lit when the field's patterns take it in. A
 * click lights a button or puts it out, and as the drop-down closes the
 * field becomes the lit patterns, in the grid's order, once any was
 * clicked. Its close button and Esc close it leaving the field alone; a
 * right click or Enter closes it as a click elsewhere does. What is typed
 * while it is open goes to the field.
 */
class WildcardDropList : public QComboBox
{
	Q_OBJECT
public:
	explicit WildcardDropList(QWidget *parent = nullptr);
	~WildcardDropList() override;

	/** The default patterns, separated by ';' or spaces. */
	void setFixedPatterns(const QString &patterns);
	QString fixedPatterns() const { return m_fixedPatterns; }
	/** How many buttons a row of the grid holds (OnDropDown's columns). */
	void setColumns(int columns);
	/** Whether the field's own patterns get buttons too (upstream's
	    allowUserAddedPatterns, on by default). */
	void setAllowUserAddedPatterns(bool allow);
	/** The field's text, shown from its start. */
	void setPatterns(const QString &patterns);

	void showPopup() override;
	void hidePopup() override;

	/** The buttons of the open drop-down, whether each is lit, a click
	    on one, and on the close button (for tests). */
	bool isPopupVisibleForTest() const;
	QStringList popupPatternsForTest() const;
	QList<bool> popupLitForTest() const;
	void clickPopupPatternForTest(int index);
	void clickPopupCloseForTest();
	/** The open drop-down itself (for tests' screenshots). */
	QWidget *popupForTest() const;

signals:
	/** The drop-down closed (CBN_CLOSEUP), the field as it then is. */
	void closedUp();

private:
	friend class WildcardDropListPopup;
	void closeUp(bool apply);

	QString m_fixedPatterns;
	int m_columns = 6;
	bool m_allowUserAddedPatterns = true;
	QPointer<WildcardDropListPopup> m_popup;
};

} // namespace lm
