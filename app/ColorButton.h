// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QColor>
#include <QPushButton>

namespace lm
{

/**
 * WinMerge's CColorButton (Src/Common/ColorButton.cpp): a button filled
 * with the color it holds, a COLORREF of the color options, which the
 * page it sits on lets the user choose by clicking it.
 */
class ColorButton : public QPushButton
{
	Q_OBJECT
public:
	explicit ColorButton(QWidget *parent = nullptr);

	/** The COLORREF held (CLR_NONE, -1, for none). */
	int colorRef() const { return m_colorRef; }
	void setColorRef(int colorRef);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	int m_colorRef = 0;
};

/** CMyColorDialog: the color dialog, opened on a color; the color chosen,
    invalid when it was canceled. With customColors the dialog's sixteen
    custom colors are the options' (Options::CustomColors), loaded before
    and saved after, whether it was canceled or not. */
QColor chooseColor(QWidget *parent, const QColor &initial, bool customColors = true);
/** Tests answer in place of the dialog. */
void setColorChooserForTest(std::function<QColor(const QColor &initial)> chooser);

} // namespace lm
