// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge: Qt application entry point.
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include "MainWindow.h"
#include "EngineOptions.h"

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("LibreMerge"));
	QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
	QApplication::setOrganizationName(QStringLiteral("LibreMerge"));

	lm::installEngineOptions();

	QCommandLineParser parser;
	parser.setApplicationDescription(
		QStringLiteral("A free differencing and merging tool for macOS and Linux"));
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument(QStringLiteral("left"), QStringLiteral("Left file"), QStringLiteral("[left]"));
	parser.addPositionalArgument(QStringLiteral("right"), QStringLiteral("Right file"), QStringLiteral("[right]"));
	QCommandLineOption screenshotOpt(QStringLiteral("screenshot"),
		QStringLiteral("Render the comparison to <file> and exit (for testing)"),
		QStringLiteral("file"));
	parser.addOption(screenshotOpt);
	parser.process(app);

	MainWindow window;
	const QStringList args = parser.positionalArguments();
	if (args.size() == 2)
		window.openFileComparison(args.at(0), args.at(1));

	if (parser.isSet(screenshotOpt))
	{
		const QString target = parser.value(screenshotOpt);
		QTimer::singleShot(0, &window, [&window, target]() {
			window.resize(1100, 700);
			window.grab().save(target);
			QApplication::quit();
		});
		window.show();
		return app.exec();
	}

	window.show();
	return app.exec();
}
