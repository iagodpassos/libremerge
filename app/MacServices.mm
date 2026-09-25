// SPDX-License-Identifier: GPL-3.0-or-later
// macOS Services provider: receives the Finder selection for the
// "Compare with LibreMerge" context-menu entry (NSServices in the
// Info.plist) and routes it into the main window.
#include "MacServices.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "MainWindow.h"

#import <AppKit/AppKit.h>

@interface LmServicesProvider : NSObject
{
@public
	QPointer<MainWindow> window;
}
- (void)compareFilesService:(NSPasteboard *)pboard
	userData:(NSString *)userData
	error:(NSString **)error;
@end

@implementation LmServicesProvider

- (void)compareFilesService:(NSPasteboard *)pboard
	userData:(NSString *)userData
	error:(NSString **)error
{
	Q_UNUSED(userData);
	Q_UNUSED(error);
	NSArray<NSURL *> *urls = [pboard
		readObjectsForClasses:@[ [NSURL class] ]
		options:@{ NSPasteboardURLReadingFileURLsOnlyKey : @YES }];

	QStringList paths;
	for (NSURL *url in urls)
		paths.append(QString::fromNSString(url.path));
	if (paths.isEmpty())
		return;

	QPointer<MainWindow> target = window;
	QMetaObject::invokeMethod(target, [target, paths]() {
		if (target.isNull())
			return;
		target->handleIncomingPaths(paths);
		target->show();
		target->raise();
		target->activateWindow();
	}, Qt::QueuedConnection);
}

@end

namespace lm
{

void installMacServices(MainWindow *window)
{
	static LmServicesProvider *provider = [[LmServicesProvider alloc] init];
	provider->window = window;
	[NSApp setServicesProvider:provider];
}

void activateAppForTest()
{
	[NSApp activateIgnoringOtherApps:YES];
}

QStringList appMenuItemsForTest(const QString &menuTitle)
{
	QStringList items;
	NSMenu *mainMenu = [NSApp mainMenu];
	if (mainMenu == nil || mainMenu.numberOfItems == 0)
		return items;
	if (menuTitle == QStringLiteral("*"))
	{
		// the top-level menu titles themselves
		for (NSMenuItem *item in mainMenu.itemArray)
			items.append(QString::fromNSString(item.title) + QLatin1Char('\t')
				+ (item.hasSubmenu ? QLatin1Char('1') : QLatin1Char('0')));
		return items;
	}
	NSMenu *appMenu = [mainMenu itemAtIndex:0].submenu;
	if (!menuTitle.isEmpty())
	{
		NSMenuItem *top = [mainMenu itemWithTitle:menuTitle.toNSString()];
		appMenu = top != nil ? top.submenu : nil;
	}
	for (NSMenuItem *item in appMenu.itemArray)
	{
		if (item.isSeparatorItem || item.isHidden)
			continue;
		items.append(QString::fromNSString(item.title)
			+ QLatin1Char('\t')
			+ (item.hasSubmenu ? QLatin1Char('1') : QLatin1Char('0')));
	}
	return items;
}

} // namespace lm
