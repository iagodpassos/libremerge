// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "Theme.h"

#include <QApplication>
#include <QSettings>
#include <QStyleHints>

namespace lm
{

namespace
{
const QString kThemeKey = QStringLiteral("Appearance/Theme");
}

Theme *Theme::instance()
{
	static Theme theme;
	return &theme;
}

Theme::Theme()
{
	const QString value =
		QSettings().value(kThemeKey, QStringLiteral("system")).toString();
	m_mode = value == QStringLiteral("light") ? ThemeMode::Light
		: value == QStringLiteral("dark") ? ThemeMode::Dark
		: ThemeMode::System;
	connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged,
		this, [this]() {
			if (m_mode == ThemeMode::System)
				emit changed();
		});
}

void Theme::setMode(ThemeMode mode)
{
	if (mode == m_mode)
		return;
	m_mode = mode;
	QSettings().setValue(kThemeKey,
		mode == ThemeMode::Light ? QStringLiteral("light")
		: mode == ThemeMode::Dark ? QStringLiteral("dark")
		: QStringLiteral("system"));
	emit changed();
}

bool Theme::dark() const
{
	switch (m_mode)
	{
	case ThemeMode::Light: return false;
	case ThemeMode::Dark: return true;
	case ThemeMode::System:
		break;
	}
	return qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

const DiffColors &diffColors()
{
	// WinMerge's defaults (Src/OptionsDiffColors.cpp)
	static const DiffColors light = {
		QColor(239, 203, 5), QColor(192, 192, 192),
		QColor(239, 119, 116), QColor(240, 192, 192),
		QColor(251, 242, 191), QColor(233, 233, 233),
		QColor(241, 226, 173), QColor(255, 170, 130),
		QColor(255, 160, 160), QColor(200, 129, 108),
		QColor(228, 155, 82), QColor(248, 112, 78),
	};
	// same roles, tuned for light text on a #1e1e1e editor
	static const DiffColors dark = {
		QColor(90, 78, 10), QColor(58, 58, 58),
		QColor(122, 52, 50), QColor(84, 62, 62),
		QColor(72, 68, 40), QColor(50, 50, 50),
		QColor(130, 108, 36), QColor(160, 84, 48),
		QColor(168, 88, 88), QColor(150, 78, 52),
		QColor(140, 90, 45), QColor(150, 70, 45),
	};
	return Theme::instance()->dark() ? dark : light;
}

} // namespace lm
