// SPDX-License-Identifier: GPL-3.0-or-later
#include "HexDialogs.h"

#include <algorithm>
#include <cstring>
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "HexView.h"
#include "MessageBoxes.h"

namespace
{

template <typename Dialog>
std::function<bool(Dialog *)> &presenterOf()
{
	static std::function<bool(Dialog *)> presenter;
	return presenter;
}

/** One token of the special syntax at text[i]: "<", the size letter
    (b, w, l, f, d), the kind (d decimal, h hex, l float, o double), ":",
    the value and ">". Its byte count, or 0 when it is none. */
int tokenLength(const QString &text, qsizetype i, qsizetype *end)
{
	if (text.size() - i < 5 || text.at(i) != QLatin1Char('<'))
		return 0;
	const QChar size = text.at(i + 1);
	const QChar kind = text.at(i + 2);
	if (QStringLiteral("bwlfd").indexOf(size) < 0 || QStringLiteral("dhlo").indexOf(kind) < 0
		|| text.at(i + 3) != QLatin1Char(':'))
		return 0;
	qsizetype close = i + 4;
	while (close < text.size() && text.at(close) != QLatin1Char('>'))
		++close;
	if (close == i + 4 || close == text.size())
		return 0;
	for (qsizetype k = i + 4; k < close; ++k)
	{
		const QChar c = text.at(k);
		bool good = false;
		switch (kind.toLatin1())
		{
		case 'd': good = c.isDigit() || c == QLatin1Char('-'); break;
		case 'h': good = c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f')); break;
		default:
			good = c.isDigit() || c == QLatin1Char('-') || c == QLatin1Char('.')
				|| c == QLatin1Char('e') || c == QLatin1Char('E');
			break;
		}
		if (!good)
			return 0;
	}
	*end = close;
	switch (size.toLatin1())
	{
	case 'w': return 2;
	case 'l': return 4;
	case 'f': return 4;
	case 'd': return 8;
	default: return 1;
	}
}

/** Code page 437, the OEM character set of Frhed's US Windows: what its
    bytes 1 to 31, 127 and 128 to 255 show. */
const char16_t kCodePage437[] = {
	0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
	0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
	0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
	0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
	0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
	0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
	0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
	0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
	0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
	0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
	0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
	0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
	0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
	0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
	0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
	0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

char16_t codePage437(unsigned char byte)
{
	if (byte < 32)
		return kCodePage437[byte];
	if (byte == 127)
		return 0x2302;
	if (byte < 128)
		return byte;
	return kCodePage437[32 + (byte - 128)];
}

/** A character's byte in code page 437, "?" when it has none. */
char oemByte(QChar ch)
{
	const char16_t unicode = ch.unicode();
	if (unicode < 128)
		return static_cast<char>(unicode);
	for (int byte = 128; byte < 256; ++byte)
		if (codePage437(static_cast<unsigned char>(byte)) == unicode)
			return static_cast<char>(byte);
	return '?';
}

QByteArray tokenBytes(const QString &text, qsizetype i, qsizetype end, int length,
	bool bigEndian)
{
	const QChar kind = text.at(i + 2);
	const QString value = text.mid(i + 4, end - i - 4);
	QByteArray bytes(length, '\0');
	if (text.at(i + 1) == QLatin1Char('f') || text.at(i + 1) == QLatin1Char('d'))
	{
		const double number = value.toDouble();
		if (length == 4)
		{
			const float single = static_cast<float>(number);
			std::memcpy(bytes.data(), &single, 4);
		}
		else
		{
			std::memcpy(bytes.data(), &number, 8);
		}
		// (the host is little-endian, as Frhed's)
		if (bigEndian)
			std::reverse(bytes.begin(), bytes.end());
		return bytes;
	}
	qint64 number = 0;
	if (kind == QLatin1Char('h'))
		number = static_cast<qint64>(value.toULongLong(nullptr, 16));
	else if (kind == QLatin1Char('d'))
		number = value.toLongLong();
	else
		number = static_cast<qint64>(value.toDouble());
	for (int k = 0; k < length; ++k)
		bytes[bigEndian ? length - 1 - k : k] = static_cast<char>((number >> (8 * k)) & 0xff);
	return bytes;
}

} // namespace

namespace lm::hex
{

QString bytesToSpecialSyntax(const QByteArray &bytes)
{
	QString text;
	for (const char raw : bytes)
	{
		const auto byte = static_cast<unsigned char>(raw);
		if (byte == '<')
			text += QStringLiteral("\\<");
		else if (byte == '\\')
			text += QStringLiteral("\\\\");
		else if ((byte >= 32 && byte < 127) || byte == 10 || byte == 13)
			text += QLatin1Char(static_cast<char>(byte));
		else
			text += QStringLiteral("<bh:%1>").arg(byte, 2, 16, QLatin1Char('0'));
	}
	return text;
}

QByteArray ansiBytes(const QString &text)
{
	QByteArray bytes;
	bytes.reserve(text.size());
	for (const QChar ch : text)
	{
		const ushort unicode = ch.unicode();
		if (unicode < 256)
			bytes.append(static_cast<char>(unicode));
		else if (unicode == 0x2018)
			bytes.append(static_cast<char>(145));
		else if (unicode == 0x2019)
			bytes.append(static_cast<char>(146));
		else
			bytes.append('?');
	}
	return bytes;
}

QByteArray charsetBytes(const QString &text, bool oem)
{
	if (!oem)
		return ansiBytes(text);
	QByteArray bytes;
	bytes.reserve(text.size());
	for (const QChar ch : text)
		bytes.append(oemByte(ch));
	return bytes;
}

QChar displayChar(unsigned char byte, bool oem)
{
	if (oem)
		return byte == 0 || byte == '\r' || byte == '\n' ? QLatin1Char('.') : QChar(codePage437(byte));
	if ((byte >= 32 && byte <= 126) || byte >= 160)
		return QChar(byte);
	if (byte == 145)
		return QChar(0x2018);
	if (byte == 146)
		return QChar(0x2019);
	return QLatin1Char('.');
}

QByteArray specialSyntaxToBytes(const QString &text, bool bigEndian, bool oem)
{
	QByteArray bytes;
	for (qsizetype i = 0; i < text.size(); ++i)
	{
		qsizetype end = 0;
		if (const int length = tokenLength(text, i, &end))
		{
			bytes += tokenBytes(text, i, end, length, bigEndian);
			i = end;
			continue;
		}
		if (text.at(i) == QLatin1Char('\\') && i + 1 < text.size()
			&& (text.at(i + 1) == QLatin1Char('<') || text.at(i + 1) == QLatin1Char('\\')))
		{
			bytes += static_cast<char>(text.at(i + 1).toLatin1());
			++i;
			continue;
		}
		bytes += charsetBytes(QString(text.at(i)), oem);
	}
	return bytes;
}

bool parseOffset(const QString &text, qsizetype *value)
{
	const QString trimmed = text.trimmed();
	static const QRegularExpression hex(QStringLiteral("^(?:0?[xX])([0-9a-fA-F]+)"));
	static const QRegularExpression decimal(QStringLiteral("^([0-9]+)"));
	bool ok = false;
	qulonglong number = 0;
	if (const QRegularExpressionMatch match = hex.match(trimmed); match.hasMatch())
		number = match.captured(1).toULongLong(&ok, 16);
	else if (const QRegularExpressionMatch match = decimal.match(trimmed); match.hasMatch())
		number = match.captured(1).toULongLong(&ok, 10);
	if (!ok)
		return false;
	*value = static_cast<qsizetype>(number);
	return true;
}

qsizetype findBytes(const QByteArray &data, qsizetype from, qsizetype length,
	const QByteArray &pattern, int direction, bool caseSensitive)
{
	const qsizetype count = pattern.size();
	if (count == 0 || length < count || from < 0 || from + length > data.size())
		return -1;
	const auto fold = [caseSensitive](char c) {
		return !caseSensitive && c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
	};
	const auto matches = [&](qsizetype at) {
		for (qsizetype k = 0; k < count; ++k)
			if (fold(data.at(from + at + k)) != fold(pattern.at(k)))
				return false;
		return true;
	};
	if (direction > 0)
	{
		for (qsizetype at = 0; at <= length - count; ++at)
			if (matches(at))
				return at;
	}
	else
	{
		for (qsizetype at = length - count; at >= 0; --at)
			if (matches(at))
				return at;
	}
	return -1;
}

} // namespace lm::hex

// --- Copy, Cut, Delete ------------------------------------------------------

HexRangeDialog::HexRangeDialog(Mode mode, qsizetype start, qsizetype end, qsizetype size,
	QWidget *parent)
	: QDialog(parent), m_mode(mode), m_size(size)
{
	QString title, startText, group, byEnd, byCount;
	switch (mode)
	{
	case Copy:
		title = tr("Copy");
		startText = tr("Start copying at offset:");
		group = tr("Copy how many bytes");
		byEnd = tr("Copy up to and including offset:");
		byCount = tr("Number of bytes to copy:");
		break;
	case Cut:
		title = tr("Cut");
		startText = tr("Start cutting at offset:");
		group = tr("Cut how many bytes");
		byEnd = tr("Cut up to and including offset:");
		byCount = tr("Number of bytes to cut:");
		break;
	case Delete:
		title = tr("Delete");
		startText = tr("Start deleting at offset:");
		group = tr("Delete how many bytes");
		byEnd = tr("Delete up to and including offset:");
		byCount = tr("Number of bytes to delete:");
		break;
	}
	setWindowTitle(title);
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(startText, this));
	m_start = new QLineEdit(QStringLiteral("0x%1").arg(QString::number(start, 16)), this);
	layout->addWidget(m_start);
	auto *box = new QGroupBox(group, this);
	auto *boxLayout = new QVBoxLayout(box);
	m_byEnd = new QRadioButton(byEnd, box);
	m_end = new QLineEdit(QStringLiteral("0x%1").arg(QString::number(end, 16)), box);
	m_byCount = new QRadioButton(byCount, box);
	m_countField = new QLineEdit(QString::number(end - start + 1), box);
	m_countField->setEnabled(false);
	m_byEnd->setChecked(true);
	boxLayout->addWidget(m_byEnd);
	boxLayout->addWidget(m_end);
	boxLayout->addWidget(m_byCount);
	boxLayout->addWidget(m_countField);
	layout->addWidget(box);
	connect(m_byEnd, &QRadioButton::toggled, this, [this](bool on) {
		m_end->setEnabled(on);
		m_countField->setEnabled(!on);
	});
	auto *buttons = new QHBoxLayout;
	buttons->addStretch(1);
	auto *ok = new QPushButton(tr("OK"), this);
	ok->setDefault(true);
	auto *cancel = new QPushButton(tr("Cancel"), this);
	buttons->addWidget(ok);
	buttons->addWidget(cancel);
	layout->addLayout(buttons);
	connect(ok, &QPushButton::clicked, this, &HexRangeDialog::accept);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

/** Upstream's Apply: the offsets checked, then whether the data has that
    many bytes from the start. The count field of Copy and Cut takes a
    decimal number, Delete's an offset's notation too, as in Frhed. */
void HexRangeDialog::accept()
{
	qsizetype offset = 0;
	if (!m_start->text().isEmpty() && !lm::hex::parseOffset(m_start->text(), &offset))
	{
		lm::showError(this, tr("Start offset not recognized."));
		return;
	}
	qsizetype count = 0;
	if (m_byEnd->isChecked())
	{
		qsizetype end = 0;
		if (!m_end->text().isEmpty() && !lm::hex::parseOffset(m_end->text(), &end))
		{
			lm::showError(this, tr("End offset not recognized."));
			return;
		}
		count = end - offset + 1;
	}
	else
	{
		const QString text = m_countField->text().trimmed();
		bool ok = true;
		if (m_mode == Delete)
			ok = text.isEmpty() || lm::hex::parseOffset(text, &count);
		else if (!text.isEmpty())
		{
			static const QRegularExpression digits(QStringLiteral("^([0-9]+)"));
			const QRegularExpressionMatch match = digits.match(text);
			ok = match.hasMatch();
			if (ok)
				count = match.captured(1).toLongLong();
		}
		if (!ok)
		{
			lm::showError(this, tr("Number of bytes not recognized."));
			return;
		}
	}
	if (offset > m_size || count < 0 || m_size - offset < count)
	{
		lm::showError(this, m_mode == Copy ? tr("Can't copy more bytes than are present.")
			: m_mode == Cut ? tr("Can't cut more bytes than are present.")
			: tr("Can't delete more bytes than are present."));
		return;
	}
	m_offset = offset;
	m_count = count;
	QDialog::accept();
}

bool HexRangeDialog::run(HexRangeDialog *dialog)
{
	if (presenterOf<HexRangeDialog>())
		return presenterOf<HexRangeDialog>()(dialog);
	return dialog->exec() == QDialog::Accepted;
}

void HexRangeDialog::setPresenterForTest(std::function<bool(HexRangeDialog *)> presenter)
{
	presenterOf<HexRangeDialog>() = std::move(presenter);
}

// --- Paste ------------------------------------------------------------------

int HexPasteDialog::s_times = 1;
int HexPasteDialog::s_skip = 0;
bool HexPasteDialog::s_asText = false;

HexPasteDialog::HexPasteDialog(bool selected, bool insertMode, bool bigEndian, bool oem,
	QWidget *parent)
	: QDialog(parent), m_bigEndian(bigEndian), m_oem(oem)
{
	setWindowTitle(tr("Paste with dialogue"));
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("Clipboard content (text):"), this));
	m_text = new QPlainTextEdit(this);
	m_text->paste();
	layout->addWidget(m_text, 1);

	auto *row = new QHBoxLayout;
	auto *mode = new QGroupBox(tr("Paste mode"), this);
	auto *modeLayout = new QVBoxLayout(mode);
	m_overwrite = new QRadioButton(tr("Overwrite"), mode);
	m_insert = new QRadioButton(tr("Insert"), mode);
	modeLayout->addWidget(m_overwrite);
	modeLayout->addWidget(m_insert);
	// over a selection the bytes replace it: no mode to choose
	if (selected)
		mode->setEnabled(false);
	else if (insertMode)
		m_insert->setChecked(true);
	else
		m_overwrite->setChecked(true);
	row->addWidget(mode);
	auto *options = new QVBoxLayout;
	options->addWidget(new QLabel(tr("Paste data how many times:"), this));
	m_timesField = new QLineEdit(QString::number(s_times), this);
	options->addWidget(m_timesField);
	m_asText = new QCheckBox(tr("Paste coded binary values as text"), this);
	m_asText->setChecked(s_asText);
	options->addWidget(m_asText);
	row->addLayout(options, 1);
	auto *buttons = new QVBoxLayout;
	auto *ok = new QPushButton(tr("OK"), this);
	ok->setDefault(true);
	auto *cancel = new QPushButton(tr("Cancel"), this);
	buttons->addWidget(ok);
	buttons->addWidget(cancel);
	buttons->addStretch(1);
	row->addLayout(buttons);
	layout->addLayout(row);
	auto *skipRow = new QHBoxLayout;
	skipRow->addWidget(new QLabel(tr("Skip how many bytes between inserts/overwrites"), this));
	m_skipField = new QLineEdit(QString::number(s_skip), this);
	skipRow->addWidget(m_skipField);
	layout->addLayout(skipRow);
	connect(ok, &QPushButton::clicked, this, &HexPasteDialog::accept);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

bool HexPasteDialog::insert() const
{
	return m_insert->isChecked();
}

void HexPasteDialog::accept()
{
	s_asText = m_asText->isChecked();
	const int times = m_timesField->text().trimmed().toInt();
	if (times <= 0)
	{
		lm::showError(this, tr("Number of times to paste must be at least 1."));
		return;
	}
	s_times = times;
	s_skip = qMax(0, m_skipField->text().trimmed().toInt());
	const QString text = m_text->toPlainText();
	m_bytes = s_asText ? lm::hex::ansiBytes(text)
		: lm::hex::specialSyntaxToBytes(text, m_bigEndian, m_oem);
	if (m_bytes.isEmpty())
	{
		lm::showError(this, tr("Tried to paste zero-length array."));
		return;
	}
	QDialog::accept();
}

bool HexPasteDialog::run(HexPasteDialog *dialog)
{
	if (presenterOf<HexPasteDialog>())
		return presenterOf<HexPasteDialog>()(dialog);
	return dialog->exec() == QDialog::Accepted;
}

void HexPasteDialog::setPresenterForTest(std::function<bool(HexPasteDialog *)> presenter)
{
	presenterOf<HexPasteDialog>() = std::move(presenter);
}

// --- Find -------------------------------------------------------------------

HexFindDialog::HexFindDialog(const QString &text, int direction, bool matchCase, bool unicode,
	QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Find"));
	auto *layout = new QVBoxLayout(this);
	auto *label = new QLabel(tr("&Find what:"), this);
	m_text = new QPlainTextEdit(text, this);
	label->setBuddy(m_text);
	layout->addWidget(label);
	layout->addWidget(m_text, 1);
	// Frhed points to its online help for the special syntax, which
	// LibreMerge has none of: the syntax in a line instead
	layout->addWidget(new QLabel(tr("Special syntax: <bh:ff> is the byte ff in hex, \\< and \\\\ "
		"are < and \\."), this));
	auto *row = new QHBoxLayout;
	auto *box = new QGroupBox(tr("Direction"), this);
	auto *boxLayout = new QVBoxLayout(box);
	m_up = new QRadioButton(tr("&Up"), box);
	m_down = new QRadioButton(tr("&Down"), box);
	(direction < 0 ? m_up : m_down)->setChecked(true);
	boxLayout->addWidget(m_up);
	boxLayout->addWidget(m_down);
	row->addWidget(box);
	auto *checks = new QVBoxLayout;
	m_matchCase = new QCheckBox(tr("Match &case"), this);
	m_matchCase->setChecked(matchCase);
	m_unicode = new QCheckBox(tr("U&nicode"), this);
	m_unicode->setChecked(unicode);
	checks->addWidget(m_matchCase);
	checks->addWidget(m_unicode);
	checks->addStretch(1);
	row->addLayout(checks, 1);
	auto *buttons = new QVBoxLayout;
	buttons->addStretch(1);
	auto *find = new QPushButton(tr("Find"), this);
	find->setDefault(true);
	auto *cancel = new QPushButton(tr("Cancel"), this);
	auto *buttonRow = new QHBoxLayout;
	buttonRow->addWidget(find);
	buttonRow->addWidget(cancel);
	buttons->addLayout(buttonRow);
	row->addLayout(buttons);
	layout->addLayout(row);
	connect(find, &QPushButton::clicked, this, &QDialog::accept);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

QString HexFindDialog::text() const
{
	return m_text->toPlainText();
}

int HexFindDialog::direction() const
{
	return m_up->isChecked() ? -1 : 1;
}

bool HexFindDialog::matchCase() const
{
	return m_matchCase->isChecked();
}

bool HexFindDialog::unicode() const
{
	return m_unicode->isChecked();
}

bool HexFindDialog::run(HexFindDialog *dialog)
{
	if (presenterOf<HexFindDialog>())
		return presenterOf<HexFindDialog>()(dialog);
	return dialog->exec() == QDialog::Accepted;
}

void HexFindDialog::setPresenterForTest(std::function<bool(HexFindDialog *)> presenter)
{
	presenterOf<HexFindDialog>() = std::move(presenter);
}

// --- Replace ----------------------------------------------------------------

QString HexReplaceDialog::s_find;
QString HexReplaceDialog::s_replace;
bool HexReplaceDialog::s_asText = false;

HexReplaceDialog::HexReplaceDialog(HexView *view, QWidget *parent)
	: QDialog(parent), m_view(view)
{
	setWindowTitle(tr("Replace"));
	// the selection, when there is one, is what to replace
	if (view->hasSelection())
		s_find = lm::hex::bytesToSpecialSyntax(view->data().mid(view->selectionLower(),
			view->selectionUpper() - view->selectionLower() + 1));
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("Find what:"), this));
	m_find = new QPlainTextEdit(s_find, this);
	layout->addWidget(m_find, 1);
	auto *findRow = new QHBoxLayout;
	auto *next = new QPushButton(tr("Find next"), this);
	auto *previous = new QPushButton(tr("Find previous"), this);
	m_matchCase = new QCheckBox(tr("Match case"), this);
	findRow->addWidget(next);
	findRow->addWidget(previous);
	findRow->addWidget(m_matchCase, 1);
	layout->addLayout(findRow);
	layout->addWidget(new QLabel(tr("Replace with:"), this));
	m_replace = new QPlainTextEdit(s_replace, this);
	layout->addWidget(m_replace, 1);
	auto *bottom = new QHBoxLayout;
	auto *all = new QGroupBox(tr("Replace all..."), this);
	auto *allLayout = new QVBoxLayout(all);
	auto *following = new QPushButton(tr("... following occurrences"), all);
	auto *previousAll = new QPushButton(tr("... previous occurrences"), all);
	allLayout->addWidget(following);
	allLayout->addWidget(previousAll);
	bottom->addWidget(all);
	auto *right = new QVBoxLayout;
	m_special = new QCheckBox(tr("Use special syntax"), this);
	m_special->setChecked(!s_asText);
	right->addWidget(m_special);
	right->addStretch(1);
	m_replaceButton = new QPushButton(tr("Replace"), this);
	auto *cancel = new QPushButton(tr("Cancel"), this);
	right->addWidget(m_replaceButton, 0, Qt::AlignRight);
	right->addWidget(cancel, 0, Qt::AlignRight);
	bottom->addLayout(right, 1);
	layout->addLayout(bottom);
	for (QPushButton *button : { next, previous, following, previousAll, m_replaceButton, cancel })
		button->setAutoDefault(false);
	connect(next, &QPushButton::clicked, this, &HexReplaceDialog::findNext);
	connect(previous, &QPushButton::clicked, this, &HexReplaceDialog::findPrevious);
	connect(following, &QPushButton::clicked, this, &HexReplaceDialog::replaceFollowing);
	connect(previousAll, &QPushButton::clicked, this, &HexReplaceDialog::replacePrevious);
	connect(m_replaceButton, &QPushButton::clicked, this, &HexReplaceDialog::replace);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void HexReplaceDialog::reject()
{
	s_asText = !m_special->isChecked();
	s_find = m_find->toPlainText();
	s_replace = m_replace->toPlainText();
	QDialog::reject();
}

QByteArray HexReplaceDialog::replacement() const
{
	return s_asText ? lm::hex::ansiBytes(s_replace) : lm::hex::specialSyntaxToBytes(s_replace,
		m_view->settings().bigEndian, m_view->settings().oemCharacterSet);
}

/** Upstream's find_and_select_data: down, from past the selection (from
    the caret's byte without one); up, from before the caret, plus the
    length less one so that a match the caret stands in counts. */
bool HexReplaceDialog::findAndSelect(int direction, bool caseSensitive)
{
	const QByteArray pattern = lm::hex::specialSyntaxToBytes(s_find,
		m_view->settings().bigEndian, m_view->settings().oemCharacterSet);
	const QByteArray &data = m_view->data();
	if (pattern.isEmpty())
		return false;
	qsizetype start = m_view->hasSelection() ? m_view->selectionLower() : m_view->caretByte();
	const qsizetype length = m_view->hasSelection()
		? m_view->selectionUpper() - start + 1 : 1;
	qsizetype found = -1;
	if (direction >= 0)
	{
		start += direction * length;
		if (start > data.size())
			return false;
		const qsizetype at = lm::hex::findBytes(data, start, data.size() - start, pattern, 1,
			caseSensitive);
		if (at >= 0)
			found = start + at;
	}
	else
	{
		const qsizetype end = qMin(m_view->caretByte() + pattern.size() - 1, data.size());
		found = lm::hex::findBytes(data, 0, end, pattern, -1, caseSensitive);
	}
	if (found < 0)
		return false;
	m_view->selectFound(found, pattern.size());
	return true;
}

bool HexReplaceDialog::replaceSelected()
{
	if (!m_view->hasSelection())
	{
		lm::showError(this, tr("Data to replace must be selected."));
		return false;
	}
	const qsizetype start = m_view->selectionLower();
	const qsizetype count = m_view->selectionUpper() - start + 1;
	const QByteArray with = s_replace.isEmpty() ? QByteArray() : replacement();
	m_view->replaceBytes(start, count, with);
	if (with.isEmpty())
		m_view->placeCaret(start);
	else
		m_view->selectFound(start, with.size());
	return true;
}

void HexReplaceDialog::find(int direction)
{
	s_find = m_find->toPlainText();
	if (findAndSelect(direction, m_matchCase->isChecked()))
		m_replaceButton->setEnabled(true);
	else
		lm::showWarning(this, tr("Could not find data."));
}

void HexReplaceDialog::findNext()
{
	find(1);
}

void HexReplaceDialog::findPrevious()
{
	find(-1);
}

/** Upstream's replace_directed: nothing when the two are the same bytes;
    the selection first while Replace is enabled (a find made it), then
    every occurrence the way asked. */
void HexReplaceDialog::replaceDirected(int direction, bool showCount)
{
	if (m_view->isReadOnly())
		return;
	const bool caseSensitive = m_matchCase->isChecked();
	s_find = m_find->toPlainText();
	s_replace = m_replace->toPlainText();
	s_asText = !m_special->isChecked();
	const HexSettings &settings = m_view->settings();
	if (lm::hex::specialSyntaxToBytes(s_find, settings.bigEndian, settings.oemCharacterSet)
		== lm::hex::specialSyntaxToBytes(s_replace, settings.bigEndian, settings.oemCharacterSet))
	{
		lm::showError(this, tr("To-replace and replace-with data are same."));
		return;
	}
	int count = 0;
	if (m_replaceButton->isEnabled())
	{
		m_replaceButton->setEnabled(false);
		++count;
		replaceSelected();
	}
	if (direction != 0)
	{
		while (findAndSelect(direction, caseSensitive))
		{
			++count;
			replaceSelected();
		}
	}
	if (count > 0 && showCount)
		lm::showInformation(this, tr("%1 occurrences replaced.").arg(count));
}

void HexReplaceDialog::replace()
{
	replaceDirected(0, false);
}

void HexReplaceDialog::replaceFollowing()
{
	replaceDirected(1, true);
}

void HexReplaceDialog::replacePrevious()
{
	replaceDirected(-1, true);
}

void HexReplaceDialog::run(HexReplaceDialog *dialog)
{
	if (presenterOf<HexReplaceDialog>())
	{
		presenterOf<HexReplaceDialog>()(dialog);
		return;
	}
	dialog->exec();
}

void HexReplaceDialog::setPresenterForTest(std::function<void(HexReplaceDialog *)> presenter)
{
	presenterOf<HexReplaceDialog>() = [presenter](HexReplaceDialog *dialog) {
		if (presenter)
			presenter(dialog);
		return true;
	};
	if (!presenter)
		presenterOf<HexReplaceDialog>() = nullptr;
}

// --- Go To ------------------------------------------------------------------

HexGoToDialog::HexGoToDialog(qsizetype current, qsizetype size, QWidget *parent)
	: QDialog(parent), m_current(current), m_size(size)
{
	setWindowTitle(tr("Go to Offset"));
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("Start with \"+\" or \"-\" (minus) for relative jump."), this));
	layout->addWidget(new QLabel(tr("Prefix offset with \"x\" to indicate hex notation."), this));
	m_field = new QLineEdit(QString::number(current), this);
	layout->addWidget(m_field);
	auto *buttons = new QHBoxLayout;
	buttons->addStretch(1);
	auto *ok = new QPushButton(tr("OK"), this);
	ok->setDefault(true);
	auto *cancel = new QPushButton(tr("Cancel"), this);
	buttons->addWidget(ok);
	buttons->addWidget(cancel);
	layout->addLayout(buttons);
	connect(ok, &QPushButton::clicked, this, &HexGoToDialog::accept);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

/** Upstream's Apply: + or - jumps from the caret; past either end, the
    first or the last byte. */
void HexGoToDialog::accept()
{
	QString text = m_field->text().trimmed();
	const bool relative = text.startsWith(QLatin1Char('+')) || text.startsWith(QLatin1Char('-'));
	const bool back = text.startsWith(QLatin1Char('-'));
	if (relative)
		text.remove(0, 1);
	qsizetype offset = 0;
	if (!lm::hex::parseOffset(text, &offset))
	{
		lm::showError(this, tr("Offset not recognized."));
		return;
	}
	if (relative)
		offset = m_current + (back ? -offset : offset);
	m_offset = qBound<qsizetype>(0, offset, qMax<qsizetype>(0, m_size - 1));
	QDialog::accept();
}

bool HexGoToDialog::run(HexGoToDialog *dialog)
{
	if (presenterOf<HexGoToDialog>())
		return presenterOf<HexGoToDialog>()(dialog);
	return dialog->exec() == QDialog::Accepted;
}

void HexGoToDialog::setPresenterForTest(std::function<bool(HexGoToDialog *)> presenter)
{
	presenterOf<HexGoToDialog>() = std::move(presenter);
}

// --- View Settings, Binary Mode, Character Set -------------------------------

HexViewSettingsDialog::HexViewSettingsDialog(QWidget *parent)
	: QDialog(parent)
{
	const HexSettings settings = HexSettings::load();
	setWindowTitle(tr("View Settings"));
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("Number of bytes to display per line:"), this));
	m_bytesPerLine = new QLineEdit(QString::number(settings.bytesPerLine), this);
	layout->addWidget(m_bytesPerLine);
	m_automaticBytes = new QCheckBox(tr("Automatically adjust number of bytes per line "
		"(uncheck this if you want to use your own choice for bytes per line)"), this);
	m_automaticBytes->setChecked(settings.automaticBytesPerLine);
	layout->addWidget(m_automaticBytes);
	layout->addWidget(new QLabel(tr("Display length of offset in how many characters:"), this));
	auto *offsetRow = new QHBoxLayout;
	m_offsetLength = new QLineEdit(QString::number(settings.offsetLength), this);
	m_automaticOffset = new QCheckBox(tr("Adjust offset length to that of the max offset"), this);
	m_automaticOffset->setChecked(settings.automaticOffsetLength);
	offsetRow->addWidget(m_offsetLength);
	offsetRow->addWidget(m_automaticOffset, 1);
	layout->addLayout(offsetRow);
	auto *values = new QGroupBox(tr("Display values at caret position as:"), this);
	auto *valuesLayout = new QHBoxLayout(values);
	m_unsigned = new QRadioButton(tr("unsigned"), values);
	m_signed = new QRadioButton(tr("signed"), values);
	(settings.unsignedValues ? m_unsigned : m_signed)->setChecked(true);
	valuesLayout->addWidget(m_unsigned);
	valuesLayout->addWidget(m_signed);
	layout->addWidget(values);
	auto *buttons = new QHBoxLayout;
	buttons->addStretch(1);
	auto *ok = new QPushButton(tr("OK"), this);
	ok->setDefault(true);
	auto *cancel = new QPushButton(tr("Cancel"), this);
	buttons->addWidget(ok);
	buttons->addWidget(cancel);
	layout->addLayout(buttons);
	connect(ok, &QPushButton::clicked, this, &HexViewSettingsDialog::accept);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

/** Upstream's Apply: one byte per line and one offset digit at least; the
    settings are kept at once. */
void HexViewSettingsDialog::accept()
{
	HexSettings settings = HexSettings::load();
	settings.bytesPerLine = qMax(1, m_bytesPerLine->text().trimmed().toInt());
	settings.offsetLength = qBound(1, m_offsetLength->text().trimmed().toInt(), 16);
	settings.automaticBytesPerLine = m_automaticBytes->isChecked();
	settings.automaticOffsetLength = m_automaticOffset->isChecked();
	settings.unsignedValues = m_unsigned->isChecked();
	settings.save();
	QDialog::accept();
}

bool HexViewSettingsDialog::run(HexViewSettingsDialog *dialog)
{
	if (presenterOf<HexViewSettingsDialog>())
		return presenterOf<HexViewSettingsDialog>()(dialog);
	return dialog->exec() == QDialog::Accepted;
}

void HexViewSettingsDialog::setPresenterForTest(
	std::function<bool(HexViewSettingsDialog *)> presenter)
{
	presenterOf<HexViewSettingsDialog>() = std::move(presenter);
}

HexBinaryModeDialog::HexBinaryModeDialog(QWidget *parent)
	: QDialog(parent)
{
	const HexSettings settings = HexSettings::load();
	setWindowTitle(tr("Binary Mode"));
	auto *layout = new QVBoxLayout(this);
	auto *box = new QGroupBox(tr("Choose binary mode"), this);
	auto *boxLayout = new QVBoxLayout(box);
	m_little = new QRadioButton(tr("Little-endian (Intel)"), box);
	m_big = new QRadioButton(tr("Big-endian (Motorola)"), box);
	(settings.bigEndian ? m_big : m_little)->setChecked(true);
	boxLayout->addWidget(m_little);
	boxLayout->addWidget(m_big);
	layout->addWidget(box);
	auto *buttons = new QHBoxLayout;
	buttons->addStretch(1);
	auto *ok = new QPushButton(tr("OK"), this);
	ok->setDefault(true);
	auto *cancel = new QPushButton(tr("Cancel"), this);
	buttons->addWidget(ok);
	buttons->addWidget(cancel);
	layout->addLayout(buttons);
	connect(ok, &QPushButton::clicked, this, &HexBinaryModeDialog::accept);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void HexBinaryModeDialog::accept()
{
	HexSettings settings = HexSettings::load();
	settings.bigEndian = m_big->isChecked();
	settings.save();
	QDialog::accept();
}

bool HexBinaryModeDialog::run(HexBinaryModeDialog *dialog)
{
	if (presenterOf<HexBinaryModeDialog>())
		return presenterOf<HexBinaryModeDialog>()(dialog);
	return dialog->exec() == QDialog::Accepted;
}

void HexBinaryModeDialog::setPresenterForTest(std::function<bool(HexBinaryModeDialog *)> presenter)
{
	presenterOf<HexBinaryModeDialog>() = std::move(presenter);
}

HexCharacterSetDialog::HexCharacterSetDialog(QWidget *parent)
	: QDialog(parent)
{
	const HexSettings settings = HexSettings::load();
	setWindowTitle(tr("Character Set"));
	auto *layout = new QHBoxLayout(this);
	auto *left = new QVBoxLayout;
	auto *box = new QGroupBox(tr("Choose character set"), this);
	auto *boxLayout = new QVBoxLayout(box);
	m_ansi = new QRadioButton(tr("ANSI"), box);
	m_oem = new QRadioButton(tr("OEM"), box);
	(settings.oemCharacterSet ? m_oem : m_ansi)->setChecked(true);
	boxLayout->addWidget(m_ansi);
	boxLayout->addWidget(m_oem);
	left->addWidget(box);
	left->addWidget(new QLabel(tr("Font size in points:"), this));
	m_fontSize = new QLineEdit(QString::number(settings.basePointSize()), this);
	left->addWidget(m_fontSize);
	layout->addLayout(left, 1);
	auto *buttons = new QVBoxLayout;
	auto *ok = new QPushButton(tr("OK"), this);
	ok->setDefault(true);
	auto *cancel = new QPushButton(tr("Cancel"), this);
	buttons->addWidget(ok);
	buttons->addWidget(cancel);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	connect(ok, &QPushButton::clicked, this, &HexCharacterSetDialog::accept);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void HexCharacterSetDialog::accept()
{
	HexSettings settings = HexSettings::load();
	settings.oemCharacterSet = m_oem->isChecked();
	const qreal size = m_fontSize->text().trimmed().toDouble();
	if (size >= 2)
		settings.fontSize = size;
	settings.save();
	QDialog::accept();
}

bool HexCharacterSetDialog::run(HexCharacterSetDialog *dialog)
{
	if (presenterOf<HexCharacterSetDialog>())
		return presenterOf<HexCharacterSetDialog>()(dialog);
	return dialog->exec() == QDialog::Accepted;
}

void HexCharacterSetDialog::setPresenterForTest(
	std::function<bool(HexCharacterSetDialog *)> presenter)
{
	presenterOf<HexCharacterSetDialog>() = std::move(presenter);
}
