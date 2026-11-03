// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "PopupList.h"

#include <QCheckBox>
#include <QDialog>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QListWidget>
#include <QScreen>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>

namespace lm::PopupList
{

namespace
{
Presenter &presenter()
{
	static Presenter instance;
	return instance;
}
} // namespace

void setPresenterForTest(Presenter value)
{
	presenter() = std::move(value);
}

Selection show(QWidget *owner, const QFont &font, const QStringList &items, int selectedIndex,
	bool enabled, const QString &checkBoxText, bool checkBoxChecked, const QPoint &globalPos)
{
	Selection result;
	result.checkBoxChecked = checkBoxChecked;
	if (presenter())
		return presenter()(items, selectedIndex, enabled, checkBoxText);

	// borderless, closed by a click elsewhere, as WinMerge's closes when
	// another window becomes active
	QDialog popup(owner, Qt::Popup);
	auto *layout = new QVBoxLayout(&popup);
	layout->setContentsMargins(1, 1, 1, 1);
	layout->setSpacing(0);
	const QFontMetrics metrics(font);
	const int space = metrics.horizontalAdvance(QLatin1Char(' '));
	QCheckBox *check = nullptr;
	if (!checkBoxText.isEmpty())
	{
		check = new QCheckBox(checkBoxText, &popup);
		check->setFont(font);
		check->setChecked(checkBoxChecked);
		check->setContentsMargins(space, space / 2, space, space / 2);
		layout->addWidget(check);
	}
	auto *list = new QListWidget(&popup);
	list->setFont(font);
	list->addItems(items);
	list->setMouseTracking(true);
	list->setEnabled(enabled);
	list->setCurrentRow(selectedIndex);
	layout->addWidget(list);
	// the item under the mouse highlighted, as a combo box's drop-down
	QObject::connect(list, &QListWidget::itemEntered, list,
		[list](QListWidgetItem *item) { list->setCurrentItem(item); });
	QObject::connect(list, &QListWidget::itemClicked, &popup, [&popup]() { popup.accept(); });
	QObject::connect(list, &QListWidget::itemActivated, &popup, [&popup]() { popup.accept(); });

	// the widest item, rows up to half the screen, a scroll bar when they
	// do not all fit
	int textWidth = 0;
	for (const QString &item : items)
		textWidth = qMax(textWidth, metrics.horizontalAdvance(item));
	QScreen *screen = QGuiApplication::screenAt(globalPos);
	if (screen == nullptr)
		screen = QGuiApplication::primaryScreen();
	const QRect work = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 1024, 768);
	const int rowHeight = qMax(1, list->sizeHintForRow(0));
	const int count = static_cast<int>(items.size());
	const int rows = qMax(1, qMin(count, work.height() / 2 / rowHeight));
	const int scrollWidth = rows < count ? list->style()->pixelMetric(QStyle::PM_ScrollBarExtent) : 0;
	int width = textWidth + 4 * space + scrollWidth + 2 * list->frameWidth();
	if (check != nullptr)
		width = qMax(width, check->sizeHint().width() + 2 * space);
	list->setFixedSize(width, rows * rowHeight + 2 * list->frameWidth());
	if (selectedIndex >= 0)
		list->scrollToItem(list->item(selectedIndex), QAbstractItemView::PositionAtCenter);
	popup.adjustSize();

	// placed like a popup menu: above the point, below it where there is
	// no room above, inside the screen
	const QSize size = popup.sizeHint();
	QPoint at(globalPos.x(), globalPos.y() - size.height());
	if (at.y() < work.top())
		at.setY(globalPos.y());
	at.setX(qMax(work.left(), qMin(at.x(), work.right() - size.width())));
	at.setY(qMax(work.top(), qMin(at.y(), work.bottom() - size.height())));
	popup.move(at);
	if (enabled)
		list->setFocus();

	if (popup.exec() != QDialog::Accepted || !enabled || list->currentRow() < 0)
		return result;
	result.index = list->currentRow();
	result.checkBoxChecked = check != nullptr && check->isChecked();
	return result;
}

} // namespace lm::PopupList
