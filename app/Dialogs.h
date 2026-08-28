// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMessageBox>

namespace lm
{

/** Window-modal message boxes. The static QMessageBox helpers are
    application-modal: on Wayland/GNOME only window-modal transient
    dialogs are centered over the application window (anything else lands
    in a screen corner), and on macOS window-modal boxes become native
    sheets attached to the window. */
inline QMessageBox::StandardButton question(QWidget *parent,
	const QString &title, const QString &text,
	QMessageBox::StandardButtons buttons =
		QMessageBox::Yes | QMessageBox::No)
{
	QMessageBox box(QMessageBox::Question, title, text, buttons, parent);
	box.setWindowModality(Qt::WindowModal);
	return static_cast<QMessageBox::StandardButton>(box.exec());
}

inline void warning(QWidget *parent, const QString &title,
	const QString &text)
{
	QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok,
		parent);
	box.setWindowModality(Qt::WindowModal);
	box.exec();
}

} // namespace lm
