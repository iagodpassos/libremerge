// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <QByteArray>
#include <QDialog>
#include <QString>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class HexView;
struct HexSettings;
class QSpinBox;

/**
 * The dialogs of WinMerge's binary comparison: those of the hex editor it
 * embeds (Frhed's heksedit), which its Edit menu and the editor's keys and
 * context menu open. Written anew after their behavior: Frhed's code is
 * GPL-2.0-only and none of it is taken.
 */
namespace lm::hex
{

/** Frhed's special syntax for bytes as text: the printable characters, CR
    and LF as they are, "\<" and "\\" for < and \, "<bh:ff>" for any other
    byte. */
QString bytesToSpecialSyntax(const QByteArray &bytes);
/** And back: besides <bh:..>, <bd:..> (a byte in decimal), <wh:..>,
    <wd:..> (a word), <lh:..>, <ld:..> (a longword), <fl:..> (a float) and
    <do:..> (a double), in the byte order asked; the other characters in
    the character set asked. */
QByteArray specialSyntaxToBytes(const QString &text, bool bigEndian = false,
	bool oem = false);
/** A text as Frhed's ANSI characters (Latin-1, Windows-1252's curly
    single quotes, "?" for the rest). */
QByteArray ansiBytes(const QString &text);
/** The same in the character set asked: ANSI, or OEM (code page 437). */
QByteArray charsetBytes(const QString &text, bool oem);
/** What the text column shows for a byte: in the ANSI character set the
    printable ASCII and Latin-1 characters and Windows-1252's curly single
    quotes, in the OEM set every byte but NUL, CR and LF as code page
    437's; a dot for the rest. */
QChar displayChar(unsigned char byte, bool oem);
/** Frhed's offset_parse_size_t: decimal, or hex after x, X, 0x or 0X. */
bool parseOffset(const QString &text, qsizetype *value);
/** Frhed's findutils_FindBytes over data[from, from + length): the first
    match from the start (direction 1) or from the end (-1), relative to
    from, or -1. Without matching case, A to Z equal a to z. */
qsizetype findBytes(const QByteArray &data, qsizetype from, qsizetype length,
	const QByteArray &pattern, int direction, bool caseSensitive);

} // namespace lm::hex

/** Frhed's Copy, Cut and Delete dialogs: a start offset, then an end
    offset or a number of bytes, from the selection or the caret. */
class HexRangeDialog : public QDialog
{
	Q_OBJECT
public:
	enum Mode
	{
		Copy,
		Cut,
		Delete,
	};
	HexRangeDialog(Mode mode, qsizetype start, qsizetype end, qsizetype size,
		QWidget *parent = nullptr);

	qsizetype offset() const { return m_offset; }
	qsizetype count() const { return m_count; }
	QLineEdit *startField() const { return m_start; }
	QLineEdit *endField() const { return m_end; }
	QLineEdit *countField() const { return m_countField; }
	QRadioButton *countButton() const { return m_byCount; }

	void accept() override;

	static bool run(HexRangeDialog *dialog);
	static void setPresenterForTest(std::function<bool(HexRangeDialog *)> presenter);

private:
	Mode m_mode;
	qsizetype m_size;
	qsizetype m_offset = 0;
	qsizetype m_count = 0;
	QLineEdit *m_start;
	QRadioButton *m_byEnd;
	QRadioButton *m_byCount;
	QLineEdit *m_end;
	QLineEdit *m_countField;
};

/** Frhed's "Paste with dialogue": the clipboard's text, to overwrite or
    insert, a number of times, some bytes apart, read in the special
    syntax unless asked to take it as text. */
class HexPasteDialog : public QDialog
{
	Q_OBJECT
public:
	HexPasteDialog(bool selected, bool insertMode, bool bigEndian = false, bool oem = false,
		QWidget *parent = nullptr);

	QByteArray bytes() const { return m_bytes; }
	bool insert() const;
	int times() const { return s_times; }
	int skip() const { return s_skip; }
	QPlainTextEdit *textField() const { return m_text; }
	QCheckBox *asTextBox() const { return m_asText; }
	QLineEdit *timesField() const { return m_timesField; }
	QLineEdit *skipField() const { return m_skipField; }
	QRadioButton *insertButton() const { return m_insert; }

	void accept() override;

	static bool run(HexPasteDialog *dialog);
	static void setPresenterForTest(std::function<bool(HexPasteDialog *)> presenter);

private:
	// Frhed keeps these from one paste to the next
	static int s_times;
	static int s_skip;
	static bool s_asText;
	bool m_bigEndian;
	bool m_oem;
	QByteArray m_bytes;
	QPlainTextEdit *m_text;
	QRadioButton *m_overwrite;
	QRadioButton *m_insert;
	QLineEdit *m_timesField;
	QLineEdit *m_skipField;
	QCheckBox *m_asText;
};

/** Frhed's Find dialog: the bytes to find in the special syntax (or as
    UTF-16 with Unicode), up or down, matching case or not. */
class HexFindDialog : public QDialog
{
	Q_OBJECT
public:
	HexFindDialog(const QString &text, int direction, bool matchCase, bool unicode,
		QWidget *parent = nullptr);

	QString text() const;
	int direction() const;
	bool matchCase() const;
	bool unicode() const;
	QPlainTextEdit *textField() const { return m_text; }
	QRadioButton *upButton() const { return m_up; }
	QCheckBox *matchCaseBox() const { return m_matchCase; }
	QCheckBox *unicodeBox() const { return m_unicode; }

	static bool run(HexFindDialog *dialog);
	static void setPresenterForTest(std::function<bool(HexFindDialog *)> presenter);

private:
	QPlainTextEdit *m_text;
	QRadioButton *m_up;
	QRadioButton *m_down;
	QCheckBox *m_matchCase;
	QCheckBox *m_unicode;
};

/** Frhed's Replace dialog, working on the pane while it is open: Find
    next and previous select the bytes, Replace replaces the selection,
    and Replace all goes through the following or the previous ones. */
class HexReplaceDialog : public QDialog
{
	Q_OBJECT
public:
	explicit HexReplaceDialog(HexView *view, QWidget *parent = nullptr);

	QPlainTextEdit *findField() const { return m_find; }
	QPlainTextEdit *replaceField() const { return m_replace; }
	QCheckBox *matchCaseBox() const { return m_matchCase; }
	QCheckBox *specialSyntaxBox() const { return m_special; }
	QPushButton *replaceButton() const { return m_replaceButton; }

	void findNext();
	void findPrevious();
	void replace();
	void replaceFollowing();
	void replacePrevious();

	void reject() override;

	static void run(HexReplaceDialog *dialog);
	static void setPresenterForTest(std::function<void(HexReplaceDialog *)> presenter);

private:
	void find(int direction);
	void replaceDirected(int direction, bool showCount);
	bool findAndSelect(int direction, bool caseSensitive);
	bool replaceSelected();
	QByteArray replacement() const;

	// Frhed keeps the two texts and the syntax choice between dialogs
	static QString s_find;
	static QString s_replace;
	static bool s_asText;
	HexView *m_view;
	QPlainTextEdit *m_find;
	QPlainTextEdit *m_replace;
	QCheckBox *m_matchCase;
	QCheckBox *m_special;
	QPushButton *m_replaceButton;
};

/** Frhed's "Go to Offset": an offset, relative with + or -, in hex after
    x. */
class HexGoToDialog : public QDialog
{
	Q_OBJECT
public:
	HexGoToDialog(qsizetype current, qsizetype size, QWidget *parent = nullptr);

	qsizetype offset() const { return m_offset; }
	QLineEdit *offsetField() const { return m_field; }

	void accept() override;

	static bool run(HexGoToDialog *dialog);
	static void setPresenterForTest(std::function<bool(HexGoToDialog *)> presenter);

private:
	qsizetype m_current;
	qsizetype m_size;
	qsizetype m_offset = 0;
	QLineEdit *m_field;
};

/** Frhed's View Settings, as WinMerge's Options > Compare > Binary opens
    them: the bytes per line (or as many as fit), the offset's length (or
    the size's), and the caret's values signed or not. Frhed's own rows,
    the text editor to call and its language, do nothing in WinMerge and
    are left out. */
class HexViewSettingsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit HexViewSettingsDialog(QWidget *parent = nullptr);

	QLineEdit *bytesPerLineField() const { return m_bytesPerLine; }
	QCheckBox *automaticBytesBox() const { return m_automaticBytes; }
	QLineEdit *offsetLengthField() const { return m_offsetLength; }
	QCheckBox *automaticOffsetBox() const { return m_automaticOffset; }
	QRadioButton *signedButton() const { return m_signed; }

	void accept() override;

	static bool run(HexViewSettingsDialog *dialog);
	static void setPresenterForTest(std::function<bool(HexViewSettingsDialog *)> presenter);

private:
	QLineEdit *m_bytesPerLine;
	QCheckBox *m_automaticBytes;
	QLineEdit *m_offsetLength;
	QCheckBox *m_automaticOffset;
	QRadioButton *m_unsigned;
	QRadioButton *m_signed;
};

/** Frhed's Binary Mode: the byte order of the values at the caret and of
    the special syntax's words and longwords. */
class HexBinaryModeDialog : public QDialog
{
	Q_OBJECT
public:
	explicit HexBinaryModeDialog(QWidget *parent = nullptr);

	QRadioButton *bigEndianButton() const { return m_big; }

	void accept() override;

	static bool run(HexBinaryModeDialog *dialog);
	static void setPresenterForTest(std::function<bool(HexBinaryModeDialog *)> presenter);

private:
	QRadioButton *m_little;
	QRadioButton *m_big;
};

/** Frhed's Character Set: ANSI or OEM for the text column, and the font
    size. */
class HexCharacterSetDialog : public QDialog
{
	Q_OBJECT
public:
	explicit HexCharacterSetDialog(QWidget *parent = nullptr);

	QRadioButton *oemButton() const { return m_oem; }
	QLineEdit *fontSizeField() const { return m_fontSize; }

	void accept() override;

	static bool run(HexCharacterSetDialog *dialog);
	static void setPresenterForTest(std::function<bool(HexCharacterSetDialog *)> presenter);

private:
	QRadioButton *m_ansi;
	QRadioButton *m_oem;
	QLineEdit *m_fontSize;
};
