// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "PortalAppearance.h"

#include <QDBusConnection>
#include <QDBusMessage>

namespace lm
{

namespace
{
const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kInterface = QStringLiteral("org.freedesktop.portal.Settings");
const QString kNamespace = QStringLiteral("org.freedesktop.appearance");
const QString kKey = QStringLiteral("color-scheme");
// the portal starts on demand; a missing one answers at once, a stuck
// one must not hold the window back for long
constexpr int kTimeoutMs = 1000;
} // namespace

PortalAppearance::PortalAppearance(QObject *parent)
	: QObject(parent)
{
	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected())
		return;
	// ReadOne is the portal's current method; Read, its deprecated
	// predecessor, answers on portals older than 1.15
	for (const QString &method : { QStringLiteral("ReadOne"), QStringLiteral("Read") })
	{
		QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath,
			kInterface, method);
		call << kNamespace << kKey;
		const QDBusMessage reply = bus.call(call, QDBus::Block, kTimeoutMs);
		if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
		{
			m_scheme = schemeFromValue(reply.arguments().constFirst());
			break;
		}
	}
	// a portal that came up later still reports its changes
	bus.connect(kService, kPath, kInterface, QStringLiteral("SettingChanged"),
		this, SLOT(settingChanged(QString,QString,QDBusVariant)));
}

PortalAppearance::Scheme PortalAppearance::schemeFromValue(const QVariant &value)
{
	QVariant inner = value;
	while (inner.metaType() == QMetaType::fromType<QDBusVariant>())
		inner = qvariant_cast<QDBusVariant>(inner).variant();
	bool ok = false;
	const uint scheme = inner.toUInt(&ok);
	if (!ok || scheme > PreferLight)
		return Unavailable;
	return static_cast<Scheme>(scheme);
}

void PortalAppearance::settingChanged(const QString &nameSpace,
	const QString &key, const QDBusVariant &value)
{
	if (nameSpace != kNamespace || key != kKey)
		return;
	const Scheme scheme = schemeFromValue(value.variant());
	if (scheme == m_scheme)
		return;
	m_scheme = scheme;
	emit schemeChanged();
}

} // namespace lm
