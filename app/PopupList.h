// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QPoint>
#include <QString>
#include <QStringList>

class QFont;
class QWidget;

/** WinMerge's PopupList (Src/Common/PopupList.cpp): a list that pops up at
    a point, like a menu, the item under the mouse highlighted and chosen
    by a click or Enter, with an optional check box above it. */
namespace lm::PopupList
{

struct Selection
{
	int index = -1; // the item chosen, -1 when the popup was closed
	bool checkBoxChecked = false;
};

/** Show the items at globalPos, selectedIndex selected, in the font given;
    a list not enabled only shows them. */
Selection show(QWidget *owner, const QFont &font, const QStringList &items, int selectedIndex,
	bool enabled, const QString &checkBoxText, bool checkBoxChecked, const QPoint &globalPos);

/** Tests answer in place of the popup: they get what it would show. */
using Presenter = std::function<Selection(const QStringList &items, int selectedIndex,
	bool enabled, const QString &checkBoxText)>;
void setPresenterForTest(Presenter presenter);

} // namespace lm::PopupList
