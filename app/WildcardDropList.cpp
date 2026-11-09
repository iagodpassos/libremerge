// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "WildcardDropList.h"

#include "TableFiles.h"

#include <QCoreApplication>
#include <QFrame>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolButton>

namespace lm
{

namespace
{
/** The patterns of a list as upstream walks it: the runs between ';' and
    spaces. */
QStringList splitPatterns(const QString &patterns)
{
	static const QRegularExpression separators(QStringLiteral("[; ]+"));
	return patterns.split(separators, Qt::SkipEmptyParts);
}

/** A button of the grid, the tab control's (TCS_BUTTONS with
    TCS_FORCELABELLEFT): its label on the left, and lit in the selection
    colors (TCIS_HIGHLIGHTED). */
class PatternButton : public QToolButton
{
public:
	PatternButton(const QString &pattern, bool lit, QWidget *parent)
		: QToolButton(parent)
	{
		setText(pattern);
		setCheckable(true);
		setChecked(lit);
		setFocusPolicy(Qt::NoFocus);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QStylePainter painter(this);
		QStyleOptionToolButton option;
		initStyleOption(&option);
		option.text.clear();
		const bool lit = isChecked();
		if (lit)
		{
			painter.fillRect(rect(), palette().brush(QPalette::Highlight));
		}
		else
		{
			option.state &= ~(QStyle::State_On | QStyle::State_Sunken);
			painter.drawComplexControl(QStyle::CC_ToolButton, option);
		}
		const int margin = fontMetrics().horizontalAdvance(QLatin1Char(' '));
		const QRect area = rect().adjusted(margin, 0, -margin / 2, 0);
		painter.setPen(palette().color(lit ? QPalette::HighlightedText : QPalette::ButtonText));
		painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter,
			fontMetrics().elidedText(text(), Qt::ElideRight, area.width()));
	}
};

/** The drop-down's close button, drawn as upstream's (DrawFrameControl
    with DFCS_CAPTIONCLOSE, flat and monochrome): a cross. */
class CloseButton : public QToolButton
{
public:
	explicit CloseButton(QWidget *parent)
		: QToolButton(parent)
	{
		setFocusPolicy(Qt::NoFocus);
		setAutoRaise(true);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		if (underMouse())
			painter.fillRect(rect(), palette().brush(QPalette::Midlight));
		QPen pen(palette().color(QPalette::ButtonText));
		pen.setWidthF(1.5);
		painter.setPen(pen);
		const qreal inset = width() * 0.3;
		const QRectF cross = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
		painter.drawLine(cross.topLeft(), cross.bottomRight());
		painter.drawLine(cross.topRight(), cross.bottomLeft());
	}
};
} // namespace

/** The drop-down: the grid of pattern buttons upstream puts in the combo
    box's list, with the close button at its top right corner. */
class WildcardDropListPopup : public QFrame
{
public:
	explicit WildcardDropListPopup(WildcardDropList *combo)
		: QFrame(combo, Qt::Popup)
		, m_combo(combo)
	{
		setFrameStyle(QFrame::Box | QFrame::Plain);
		setLineWidth(1);
		setBackgroundRole(QPalette::Base);
		setAutoFillBackground(true);
		setFocusPolicy(Qt::StrongFocus);
		// the click that closes it is not handed on, as a combo box's list
		// keeps it: the field's arrow closes it without opening it again
		setAttribute(Qt::WA_NoMouseReplay);
	}

	/** OnDropDown: a button for each default pattern, lit when the field
	    takes it in, then, when allowed, one for each of the field's own,
	    lit; columns of them in a row, as wide as the field allows. */
	void build(const QString &fixedPatterns, const QString &patterns, int columns,
		bool allowUserAddedPatterns, int width, int buttonHeight)
	{
		const int frame = frameWidth();
		const int cxCross = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
		const int client = width - 2 * frame;
		const int gap = 3;
		const int buttonWidth = qMax(1, (client - cxCross) / qMax(1, columns) - gap);
		auto *grid = new QGridLayout(this);
		grid->setContentsMargins(frame, frame, frame, frame);
		grid->setHorizontalSpacing(gap);
		grid->setVerticalSpacing(gap);
		grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
		const auto add = [&](const QString &pattern, bool lit) {
			auto *button = new PatternButton(pattern, lit, this);
			button->setFixedSize(buttonWidth, buttonHeight);
			// the tab control comes alive with a click: only then does the
			// field take the lit patterns
			QObject::connect(button, &QToolButton::clicked, this, [this]() { m_changed = true; });
			const int index = static_cast<int>(m_buttons.size());
			grid->addWidget(button, index / qMax(1, columns), index % qMax(1, columns));
			m_buttons.append(button);
		};
		for (const QString &pattern : splitPatterns(fixedPatterns))
			add(pattern, !patterns.isEmpty() && pathMatchSpec(pattern, patterns));
		if (allowUserAddedPatterns)
			for (const QString &pattern : splitPatterns(patterns))
				if (fixedPatterns.isEmpty() || !pathMatchSpec(pattern, fixedPatterns))
					add(pattern, true);

		m_close = new CloseButton(this);
		m_close->setFixedSize(cxCross, cxCross);
		QObject::connect(m_close, &QToolButton::clicked, this, [this]() { cancel(); });
		const int height = m_buttons.isEmpty() ? cxCross + 2 * frame
			: qMax(cxCross + 2 * frame, grid->sizeHint().height());
		setFixedSize(width, height);
		m_close->move(width - frame - cxCross, frame);
		m_close->raise();
	}

	QList<QToolButton *> buttons() const { return m_buttons; }
	QToolButton *closeButton() const { return m_close; }
	bool changed() const { return m_changed; }
	void detach() { m_combo = nullptr; }

	/** The close button and Esc: closed with the field as it was. */
	void cancel()
	{
		m_changed = false;
		close();
	}

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		// a right click closes it as a click elsewhere does
		if (event->button() == Qt::RightButton && rect().contains(event->position().toPoint()))
		{
			close();
			return;
		}
		QFrame::mousePressEvent(event);
	}

	void keyPressEvent(QKeyEvent *event) override
	{
		switch (event->key())
		{
		case Qt::Key_Escape:
			cancel();
			return;
		case Qt::Key_Return:
		case Qt::Key_Enter:
		case Qt::Key_F4:
		case Qt::Key_Tab:
		case Qt::Key_Backtab:
			close();
			return;
		case Qt::Key_Up:
		case Qt::Key_Down:
			if (event->modifiers() & Qt::AltModifier)
			{
				close();
				return;
			}
			break;
		default:
			break;
		}
		// the rest is typed in the field, which keeps the keyboard while
		// a combo box's list is down
		if (m_combo != nullptr && m_combo->lineEdit() != nullptr)
			QCoreApplication::sendEvent(m_combo->lineEdit(), event);
	}

	void hideEvent(QHideEvent *event) override
	{
		QFrame::hideEvent(event);
		if (m_closed)
			return;
		m_closed = true;
		if (m_combo != nullptr)
			m_combo->closeUp(m_changed);
	}

private:
	QPointer<WildcardDropList> m_combo;
	QList<QToolButton *> m_buttons;
	QToolButton *m_close = nullptr;
	bool m_changed = false;
	bool m_closed = false;
};

WildcardDropList::WildcardDropList(QWidget *parent)
	: QComboBox(parent)
{
	setEditable(true);
	setInsertPolicy(QComboBox::NoInsert);
}

WildcardDropList::~WildcardDropList()
{
	if (m_popup != nullptr)
	{
		m_popup->detach();
		delete m_popup;
	}
}

void WildcardDropList::setFixedPatterns(const QString &patterns)
{
	m_fixedPatterns = patterns;
}

void WildcardDropList::setColumns(int columns)
{
	m_columns = qMax(1, columns);
}

void WildcardDropList::setAllowUserAddedPatterns(bool allow)
{
	m_allowUserAddedPatterns = allow;
}

void WildcardDropList::setPatterns(const QString &patterns)
{
	setEditText(patterns);
	// a long list shows from its first pattern, as upstream's field does
	lineEdit()->setCursorPosition(0);
}

void WildcardDropList::showPopup()
{
	if (m_popup != nullptr)
		return;
	auto *popup = new WildcardDropListPopup(this);
	m_popup = popup;
	popup->build(m_fixedPatterns, currentText(), m_columns, m_allowUserAddedPatterns,
		width(), height());

	// as wide as the field (CB_GETDROPPEDWIDTH), under it, over it where
	// the screen has no room below, as a combo box's list drops
	const QRect field(mapToGlobal(QPoint(0, 0)), size());
	QPoint at(field.left(), field.bottom() + 1);
	if (const QScreen *screen = this->screen())
	{
		const QRect work = screen->availableGeometry();
		if (at.y() + popup->height() > work.bottom() + 1 && field.top() - popup->height() >= work.top())
			at.setY(field.top() - popup->height());
		at.setX(qMax(work.left(), qMin(at.x(), work.right() + 1 - popup->width())));
	}
	popup->move(at);
	popup->show();
	popup->setFocus(Qt::PopupFocusReason);
}

void WildcardDropList::hidePopup()
{
	if (m_popup != nullptr)
		m_popup->close();
}

void WildcardDropList::closeUp(bool apply)
{
	// OnCloseUp: the lit patterns, in the grid's order, once the grid was
	// clicked; the field otherwise as it was or as typed
	WildcardDropListPopup *popup = m_popup;
	if (apply && popup != nullptr)
	{
		QStringList lit;
		for (const QToolButton *button : popup->buttons())
			if (button->isChecked())
				lit.append(button->text());
		setEditText(lit.join(QLatin1Char(';')));
		lineEdit()->selectAll();
	}
	m_popup = nullptr;
	if (popup != nullptr)
		popup->deleteLater();
	emit closedUp();
}

bool WildcardDropList::isPopupVisibleForTest() const
{
	return m_popup != nullptr && m_popup->isVisible();
}

QStringList WildcardDropList::popupPatternsForTest() const
{
	QStringList patterns;
	if (m_popup != nullptr)
		for (const QToolButton *button : m_popup->buttons())
			patterns.append(button->text());
	return patterns;
}

QList<bool> WildcardDropList::popupLitForTest() const
{
	QList<bool> lit;
	if (m_popup != nullptr)
		for (const QToolButton *button : m_popup->buttons())
			lit.append(button->isChecked());
	return lit;
}

void WildcardDropList::clickPopupPatternForTest(int index)
{
	if (m_popup != nullptr && index >= 0 && index < m_popup->buttons().size())
		m_popup->buttons().at(index)->click();
}

void WildcardDropList::clickPopupCloseForTest()
{
	if (m_popup != nullptr)
		m_popup->closeButton()->click();
}

QWidget *WildcardDropList::popupForTest() const
{
	return m_popup;
}

} // namespace lm
