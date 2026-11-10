// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "ColorButton.h"

#include "ColorSettings.h"

#include <QColorDialog>
#include <QPainter>
#include <QStyleOptionFrame>

// engine
#include <windows.h> // ports: COLORREF
#include "OptionsCustomColors.h"
#include "OptionsMgr.h"

namespace lm
{

namespace
{
std::function<QColor(const QColor &)> &chooserForTest()
{
	static std::function<QColor(const QColor &)> chooser;
	return chooser;
}
} // namespace

ColorButton::ColorButton(QWidget *parent)
	: QPushButton(parent)
{
	// 17 by 14 dialog units, as upstream's pages lay the buttons out
	const QFontMetrics metrics(font());
	const qreal baseX = metrics.horizontalAdvance(
		QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")) / 52.0;
	setFixedSize(qRound(17 * baseX / 4), qRound(14 * metrics.height() / 8.0));
	setFocusPolicy(Qt::StrongFocus);
}

void ColorButton::setColorRef(int colorRef)
{
	if (colorRef == m_colorRef)
		return;
	m_colorRef = colorRef;
	update();
}

void ColorButton::paintEvent(QPaintEvent *)
{
	// DrawItem: the button filled with its color, in a sunken edge
	// (WS_EX_CLIENTEDGE)
	QPainter painter(this);
	const QColor color = colorFromColorRef(m_colorRef);
	if (color.isValid())
		painter.fillRect(rect().adjusted(2, 2, -2, -2), color);
	QStyleOptionFrame frame;
	frame.initFrom(this);
	frame.state |= QStyle::State_Sunken;
	frame.lineWidth = 2;
	frame.midLineWidth = 0;
	style()->drawPrimitive(QStyle::PE_Frame, &frame, &painter, this);
	if (hasFocus())
	{
		QStyleOptionFocusRect focus;
		focus.initFrom(this);
		focus.rect = rect().adjusted(1, 1, -1, -1);
		style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, this);
	}
}

QColor chooseColor(QWidget *parent, const QColor &initial, bool customColors)
{
	if (chooserForTest())
		return chooserForTest()(initial);
	COptionsMgr *mgr = customColors ? GetOptionsMgr() : nullptr;
	COLORREF custom[16] = {};
	if (mgr != nullptr)
	{
		Options::CustomColors::Load(mgr, custom);
		for (int i = 0; i < 16; ++i)
			QColorDialog::setCustomColor(i, colorFromColorRef(static_cast<int>(custom[i])));
	}
	// Qt's own dialog, which has the sixteen custom colors upstream's
	// ChooseColor has
	QColorDialog dialog(initial, parent);
	dialog.setOption(QColorDialog::DontUseNativeDialog);
	const bool chosen = dialog.exec() == QDialog::Accepted;
	if (mgr != nullptr)
	{
		for (int i = 0; i < 16; ++i)
			custom[i] = static_cast<COLORREF>(colorRefFromColor(QColorDialog::customColor(i)));
		Options::CustomColors::Save(mgr, custom);
	}
	return chosen ? dialog.selectedColor() : QColor();
}

void setColorChooserForTest(std::function<QColor(const QColor &initial)> chooser)
{
	chooserForTest() = std::move(chooser);
}

} // namespace lm
