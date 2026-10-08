// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

/**
 * WinMerge's Go To dialog (WMGotoDlg, IDD_WMGOTO): a line of one of the
 * files, or a difference by its number. The range the number may take
 * shows next to it, and Go to is enabled only for a number within it.
 * The middle file is offered in a 3-way comparison only, the
 * differences only when there are some.
 */
class GoToDialog : public QDialog
{
	Q_OBJECT
public:
	explicit GoToDialog(QWidget *parent = nullptr);

	/** What upstream's caller fills in before DoModal: the number, the
	    file (0 left, 1 middle, 2 right), how many files, the last line of
	    each (1-based, -1 where there is no such file) and how many
	    differences there are. */
	void init(const QString &number, int file, int files,
		const std::array<int, 3> &lastLines, int lastDifference);

	int number() const;
	int file() const;
	bool goesToLine() const;

	/** Show the dialog modally, or hand it to the test's presenter. */
	static bool run(GoToDialog *dialog);
	static void setPresenterForTest(std::function<bool(GoToDialog *dialog)> presenter);

	/** The dialog's parts (for tests). */
	QLineEdit *numberFieldForTest() const { return m_number; }
	QLabel *rangeLabelForTest() const { return m_range; }
	QPushButton *goButtonForTest() const { return m_go; }
	QRadioButton *fileButtonForTest(int file) const { return m_files[file]; }
	QRadioButton *lineButtonForTest() const { return m_toLine; }
	QRadioButton *differenceButtonForTest() const { return m_toDifference; }

private:
	int rangeMax() const;
	void updateRange();
	void updateGoButton();

	QLineEdit *m_number;
	QLabel *m_range;
	QRadioButton *m_files[3];
	QRadioButton *m_toLine;
	QRadioButton *m_toDifference;
	QPushButton *m_go;
	std::array<int, 3> m_lastLines{ { -1, -1, -1 } };
	int m_lastDifference = -1;
};
