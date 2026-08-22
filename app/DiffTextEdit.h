// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QPlainTextEdit>

/**
 * QPlainTextEdit with a line-number gutter. The gutter background can be
 * tinted per line to mirror the diff highlighting.
 */
class DiffTextEdit : public QPlainTextEdit
{
	Q_OBJECT
public:
	explicit DiffTextEdit(QWidget *parent = nullptr);

	void setGutterLineColors(const QHash<int, QColor> &colors);

	int gutterWidth() const;
	void paintGutter(QPaintEvent *event);

	int firstVisibleLine() const;
	int visibleLineCount() const;

protected:
	void resizeEvent(QResizeEvent *event) override;

private slots:
	void updateGutterWidth();
	void updateGutter(const QRect &rect, int dy);

private:
	QWidget *m_gutter;
	QHash<int, QColor> m_lineColors;
};
