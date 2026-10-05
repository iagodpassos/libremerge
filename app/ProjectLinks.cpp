// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectLinks.h"

#include <QDesktopServices>
#include <QString>

namespace
{

std::function<void(const QUrl &)> &openerStandIn()
{
	static std::function<void(const QUrl &)> opener;
	return opener;
}

} // namespace

namespace lm
{

QUrl projectPage()
{
	return QUrl(QStringLiteral("https://github.com/iagodpassos/libremerge"));
}

QUrl projectIssuesPage()
{
	return QUrl(QStringLiteral("https://github.com/iagodpassos/libremerge/issues"));
}

QUrl projectTranslationsPage()
{
	return QUrl(QStringLiteral("https://github.com/iagodpassos/libremerge/issues/5"));
}

void openProjectPage(const QUrl &page)
{
	if (openerStandIn())
		openerStandIn()(page);
	else
		QDesktopServices::openUrl(page);
}

void setProjectPageOpenerForTest(std::function<void(const QUrl &page)> opener)
{
	openerStandIn() = std::move(opener);
}

} // namespace lm
