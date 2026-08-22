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

} // namespace lm
