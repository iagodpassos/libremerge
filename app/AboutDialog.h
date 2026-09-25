// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QUrl>

/**
 * The About box, laid out like WinMerge's CAboutDlg: a banner with the
 * wordmark, artwork and version, the warranty and license notice with
 * the copyright lines, and a bottom row with the Contributors button,
 * the project link and OK.
 */
class AboutDialog : public QDialog
{
	Q_OBJECT
public:
	explicit AboutDialog(QWidget *parent = nullptr);

	/** "Version 0.9.4 (2026.09)", WinMerge's version line. */
	static QString versionText();
	/** Platform and build architecture, e.g. "macOS arm64". */
	static QString platformText();
	static QUrl homepageUrl();
	/** CONTRIBUTORS.md as bundled in the application resources. */
	static QString contributorsMarkdown();

public slots:
	/** WinMerge opens Contributors.txt in an editor; the list is shown
	    here in a read-only viewer with clickable links. */
	void showContributors();
};
