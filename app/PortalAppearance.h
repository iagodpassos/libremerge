// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusVariant>
#include <QObject>

namespace lm
{

/**
 * The desktop's light/dark preference on Linux, read from the XDG desktop
 * portal (org.freedesktop.appearance color-scheme, the setting GNOME and
 * KDE publish) and followed as it changes. An AppImage carries no GTK or
 * KDE platform theme, so Qt alone cannot see it.
 */
class PortalAppearance : public QObject
{
	Q_OBJECT
public:
	/** The portal's values, plus Unavailable without a portal. */
	enum Scheme
	{
		Unavailable = -1,
		NoPreference = 0,
		PreferDark = 1,
		PreferLight = 2,
	};

	explicit PortalAppearance(QObject *parent = nullptr);

	Scheme scheme() const { return m_scheme; }

	/** A portal value (possibly wrapped in D-Bus variants, as the
	    deprecated Read method returns it) as a Scheme. */
	static Scheme schemeFromValue(const QVariant &value);

signals:
	void schemeChanged();

private slots:
	void settingChanged(const QString &nameSpace, const QString &key,
		const QDBusVariant &value);

private:
	Scheme m_scheme = Unavailable;
};

} // namespace lm
