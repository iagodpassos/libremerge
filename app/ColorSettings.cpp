// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "ColorSettings.h"
#include "EngineOptions.h"

#include "Theme.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>
#include <QStandardPaths>

// engine
#include <windows.h> // ports: COLOR_* and RGB
#include "OptionsCustomColors.h"
#include "OptionsDef.h"
#include "OptionsDiffColors.h"
#include "OptionsDirColors.h"
#include "OptionsMgr.h"
#include "OptionsSyntaxColors.h"
#include "SyntaxColors.h"
#include "UnicodeString.h"

namespace lm
{

namespace
{
constexpr unsigned kClrNone = 0xFFFFFFFFu;

struct Cache
{
	DiffColors diff;
	DirColors dir;
	bool defaultTextColoring = true;
	bool hookEnabled = false;
	std::map<int, QColor> hookColors;
	// while a scheme is imported its system colors are the system's own
	// (UpdateColorScheme unhooks SysColorHook around ImportOptions)
	bool unhooked = false;
	// the palette before a scheme's system colors went into it: the
	// system's own, which an import asks for
	QPalette basePalette;
	bool hasBasePalette = false;
};

Cache &cache()
{
	static Cache instance;
	return instance;
}

/** theApp.GetMainSyntaxColors(): made once the system colors answer, as
    its defaults ask for them. */
::SyntaxColors &mainSyntaxColors()
{
	static ::SyntaxColors colors;
	return colors;
}

unsigned long sysColorProvider(int index)
{
	return static_cast<unsigned long>(static_cast<unsigned>(colorRefFromColor(systemColor(index))));
}

/** The system colors of a scheme (SysColorHook::Deserialize):
    "index:color" pairs separated by commas, a color in hex after 0x. */
std::map<int, QColor> parseSystemColors(const String &text)
{
	std::map<int, QColor> colors;
	for (const QString &entry : QString::fromStdString(text).split(QLatin1Char(','), Qt::SkipEmptyParts))
	{
		const QStringList pair = entry.split(QLatin1Char(':'));
		if (pair.size() != 2)
			continue;
		bool ok = false;
		const int index = pair.at(0).trimmed().toInt(&ok);
		if (!ok)
			continue;
		const QString value = pair.at(1).trimmed();
		const unsigned color = value.size() >= 2 && value.at(1) == QLatin1Char('x')
			? value.mid(2).toUInt(&ok, 16) : value.toUInt(&ok, 10);
		if (ok && index >= 0 && index <= COLOR_MENUBAR)
			colors[index] = QColor(color & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff);
	}
	return colors;
}

/** What a system color is in the application palette (GetSysColor). */
QColor paletteColor(int index, bool system = false)
{
	const Cache &c = cache();
	const QPalette palette = (c.unhooked || system) && c.hasBasePalette
		? c.basePalette : QGuiApplication::palette();
	switch (index)
	{
	case COLOR_WINDOW: return palette.color(QPalette::Base);
	case COLOR_WINDOWTEXT: return palette.color(QPalette::Text);
	case COLOR_BTNFACE:
	case COLOR_MENU:
	case COLOR_MENUBAR:
	case COLOR_SCROLLBAR:
	case COLOR_BACKGROUND:
	case COLOR_INACTIVECAPTION:
	case COLOR_GRADIENTINACTIVECAPTION:
	case COLOR_INACTIVEBORDER:
		return palette.color(QPalette::Window);
	case COLOR_BTNTEXT: return palette.color(QPalette::ButtonText);
	case COLOR_MENUTEXT: return palette.color(QPalette::WindowText);
	case COLOR_HIGHLIGHT:
	case COLOR_MENUHILIGHT:
	case COLOR_ACTIVECAPTION:
	case COLOR_GRADIENTACTIVECAPTION:
	case COLOR_ACTIVEBORDER:
		return palette.color(QPalette::Highlight);
	case COLOR_HIGHLIGHTTEXT:
	case COLOR_CAPTIONTEXT:
		return palette.color(QPalette::HighlightedText);
	case COLOR_GRAYTEXT: return palette.color(QPalette::Disabled, QPalette::Text);
	case COLOR_INACTIVECAPTIONTEXT: return palette.color(QPalette::Disabled, QPalette::WindowText);
	case COLOR_BTNSHADOW: return palette.color(QPalette::Dark);
	case COLOR_3DDKSHADOW:
	case COLOR_WINDOWFRAME:
		return palette.color(QPalette::Shadow);
	case COLOR_BTNHIGHLIGHT: return palette.color(QPalette::Light);
	case COLOR_3DLIGHT: return palette.color(QPalette::Midlight);
	case COLOR_APPWORKSPACE: return palette.color(QPalette::Mid);
	case COLOR_INFOBK: return palette.color(QPalette::ToolTipBase);
	case COLOR_INFOTEXT: return palette.color(QPalette::ToolTipText);
	case COLOR_HOTLIGHT: return palette.color(QPalette::Link);
	default: return palette.color(QPalette::Text);
	}
}

/** A scheme file being written (OnSaveCurrentScheme's CIniOptionsMgr):
    options in memory, the ones saved into it written out in the order
    they were, as "[WinMerge]" lines of key=value. */
class SchemeFileOptions : public COptionsMgr
{
public:
	using COptionsMgr::InitOption;
	using COptionsMgr::SaveOption;

	int InitOption(const String &name, const varprop::VariantValue &defaultValue) override
	{
		return AddOption(name, defaultValue);
	}
	int InitOption(const String &name, const String &defaultValue) override
	{
		varprop::VariantValue value;
		value.SetString(defaultValue);
		return AddOption(name, value);
	}
	int InitOption(const String &name, const tchar_t *defaultValue) override
	{
		return InitOption(name, String(defaultValue));
	}
	int InitOption(const String &name, int defaultValue, bool) override
	{
		varprop::VariantValue value;
		value.SetInt(defaultValue);
		return AddOption(name, value);
	}
	int InitOption(const String &name, bool defaultValue) override
	{
		varprop::VariantValue value;
		value.SetBool(defaultValue);
		return AddOption(name, value);
	}
	int SaveOption(const String &name) override
	{
		if (std::find(m_saved.begin(), m_saved.end(), name) == m_saved.end())
			m_saved.push_back(name);
		return COption::OPT_OK;
	}
	int SaveOption(const String &name, const varprop::VariantValue &value) override
	{
		const int result = Set(name, value);
		return result == COption::OPT_OK ? SaveOption(name) : result;
	}
	int SaveOption(const String &name, const String &value) override
	{
		varprop::VariantValue variant;
		variant.SetString(value);
		return SaveOption(name, variant);
	}
	int SaveOption(const String &name, const tchar_t *value) override
	{
		return SaveOption(name, String(value));
	}
	int SaveOption(const String &name, int value) override
	{
		varprop::VariantValue variant;
		variant.SetInt(value);
		return SaveOption(name, variant);
	}
	int SaveOption(const String &name, bool value) override
	{
		varprop::VariantValue variant;
		variant.SetBool(value);
		return SaveOption(name, variant);
	}
	int FlushOptions() override { return COption::OPT_OK; }
	std::map<String, String> ReadStoredSection(const String &) override { return {}; }
	void SetSerializing(bool) override {}

	/** The "[WinMerge]" values of a scheme's file (ReadIniFile). */
	static std::map<String, String> read(const QString &path)
	{
		return ReadIniFile(path.toStdString(), _T("WinMerge"));
	}

	QByteArray text() const
	{
		QByteArray text("[WinMerge]\n");
		for (const String &name : m_saved)
		{
			const varprop::VariantValue value = Get(name);
			String line = name + _T("=");
			switch (value.GetType())
			{
			case varprop::VT_BOOL: line += value.GetBool() ? _T("1") : _T("0"); break;
			case varprop::VT_INT: line += strutils::to_str(value.GetInt()); break;
			case varprop::VT_STRING: line += EscapeValue(value.GetString()); break;
			default: continue;
			}
			text += QByteArray::fromStdString(line) + '\n';
		}
		return text;
	}

private:
	std::vector<String> m_saved;
};
} // namespace

QColor colorFromColorRef(int colorRef)
{
	const unsigned value = static_cast<unsigned>(colorRef);
	if (value == kClrNone)
		return QColor();
	if ((value & 0xffffff00u) == 0x80000000u)
		return systemColor(static_cast<int>(value & 0xffu));
	return QColor(value & 0xff, (value >> 8) & 0xff, (value >> 16) & 0xff);
}

int colorRefFromColor(const QColor &color)
{
	if (!color.isValid())
		return static_cast<int>(kClrNone);
	return static_cast<int>(RGB(color.red(), color.green(), color.blue()));
}

QColor systemColor(int index)
{
	const Cache &c = cache();
	if (c.hookEnabled && !c.unhooked)
	{
		const auto it = c.hookColors.find(index);
		if (it != c.hookColors.end())
			return it->second;
	}
	return paletteColor(index);
}

void setBasePalette(const QPalette &palette)
{
	cache().basePalette = palette;
	cache().hasBasePalette = true;
}

bool hasSystemColors()
{
	return cache().hookEnabled && !cache().hookColors.empty();
}

QColor originalSystemColor(int index)
{
	return paletteColor(index, true);
}

std::map<int, int> systemColorRefs(const QString &text)
{
	std::map<int, int> colors;
	for (const auto &[index, color] : parseSystemColors(text.toStdString()))
		colors[index] = colorRefFromColor(color);
	return colors;
}

bool readSchemeSystemColors(const QString &path, bool *enabled, QString *colors)
{
	const std::map<String, String> values = SchemeFileOptions::read(path);
	if (values.empty())
		return false;
	const auto hook = values.find(OPT_SYSCOLOR_HOOK_ENABLED);
	*enabled = hook != values.end() && QString::fromStdString(hook->second).toInt() != 0;
	const auto list = values.find(OPT_SYSCOLOR_HOOK_COLORS);
	*colors = list != values.end() ? QString::fromStdString(list->second) : QString();
	return true;
}

bool applySystemColors(QPalette *palette)
{
	const Cache &c = cache();
	if (!c.hookEnabled || c.hookColors.empty() || palette == nullptr)
		return false;
	// the system colors of a scheme as the palette's roles: WinMerge's
	// SysColorHook hands them to every window of the process
	const auto set = [&c, palette](int index, std::initializer_list<QPalette::ColorRole> roles,
		QPalette::ColorGroup group = QPalette::All) {
		const auto it = c.hookColors.find(index);
		if (it == c.hookColors.end())
			return;
		for (const QPalette::ColorRole role : roles)
			palette->setColor(group, role, it->second);
	};
	set(COLOR_WINDOW, { QPalette::Base });
	set(COLOR_WINDOWTEXT, { QPalette::Text, QPalette::WindowText });
	set(COLOR_BTNFACE, { QPalette::Window, QPalette::Button });
	set(COLOR_BTNTEXT, { QPalette::ButtonText });
	set(COLOR_HIGHLIGHT, { QPalette::Highlight });
	set(COLOR_HIGHLIGHTTEXT, { QPalette::HighlightedText });
	set(COLOR_BTNHIGHLIGHT, { QPalette::Light });
	set(COLOR_3DLIGHT, { QPalette::Midlight });
	set(COLOR_BTNSHADOW, { QPalette::Dark, QPalette::Mid });
	set(COLOR_3DDKSHADOW, { QPalette::Shadow });
	set(COLOR_INFOBK, { QPalette::ToolTipBase });
	set(COLOR_INFOTEXT, { QPalette::ToolTipText });
	set(COLOR_HOTLIGHT, { QPalette::Link });
	set(COLOR_GRAYTEXT, { QPalette::PlaceholderText });
	set(COLOR_GRAYTEXT, { QPalette::Text, QPalette::WindowText, QPalette::ButtonText },
		QPalette::Disabled);
	// an alternate row a shade off the window color
	const QColor base = palette->color(QPalette::Base);
	palette->setColor(QPalette::AlternateBase, base.lightness() < 128 ? base.lighter(112) : base.darker(104));
	return true;
}

void initColorOptions()
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr)
		return;
	lm_sysColorProvider = &sysColorProvider;
	// OptionsInit: the color mode's schemes and the colors in effect
	mgr->InitOption(OPT_COLOR_MODE_EFFECTIVE, 0, 0, 1);
	mgr->InitOption(OPT_COLOR_SCHEME, _T("Default"));
	mgr->InitOption(OPT_COLOR_SCHEME_DARK, _T("VS Dark"));
	mgr->InitOption(OPT_SYSCOLOR_HOOK_ENABLED, false);
	mgr->InitOption(OPT_SYSCOLOR_HOOK_COLORS, _T(""));
	Options::DiffColors::Init(mgr);
	Options::DirColors::Init(mgr);
	Options::CustomColors::Init(mgr);
	Options::SyntaxColors::Init(mgr, &mainSyntaxColors());
	reloadColors();
	// CMergeApp::InitInstance: the colors of another mode than the one in
	// effect give way to its scheme; then the palette takes the scheme's
	// system colors
	Theme::instance()->colorsChanged();
}

void reloadColors()
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr)
		return;
	Cache &c = cache();
	// the system colors first: a color the options give as one asks them
	c.hookEnabled = mgr->GetBool(OPT_SYSCOLOR_HOOK_ENABLED);
	c.hookColors = parseSystemColors(mgr->GetString(OPT_SYSCOLOR_HOOK_COLORS));
	c.defaultTextColoring = mgr->GetBool(OPT_CLR_DEFAULT_TEXT_COLORING);

	COLORSETTINGS colors;
	Options::DiffColors::Load(mgr, colors);
	const auto of = [](const CEColor &color) {
		return colorFromColorRef(static_cast<int>(static_cast<uint32_t>(color)));
	};
	DiffColors &d = c.diff;
	d.diff = of(colors.clrDiff);
	d.diffDeleted = of(colors.clrDiffDeleted);
	d.diffText = of(colors.clrDiffText);
	d.selDiff = of(colors.clrSelDiff);
	d.selDiffDeleted = of(colors.clrSelDiffDeleted);
	d.selDiffText = of(colors.clrSelDiffText);
	d.trivial = of(colors.clrTrivial);
	d.trivialDeleted = of(colors.clrTrivialDeleted);
	d.trivialText = of(colors.clrTrivialText);
	d.moved = of(colors.clrMoved);
	d.movedDeleted = of(colors.clrMovedDeleted);
	d.movedText = of(colors.clrMovedText);
	d.selMoved = of(colors.clrSelMoved);
	d.selMovedDeleted = of(colors.clrSelMovedDeleted);
	d.selMovedText = of(colors.clrSelMovedText);
	d.snp = of(colors.clrSNP);
	d.snpDeleted = of(colors.clrSNPDeleted);
	d.snpText = of(colors.clrSNPText);
	d.selSnp = of(colors.clrSelSNP);
	d.selSnpDeleted = of(colors.clrSelSNPDeleted);
	d.selSnpText = of(colors.clrSelSNPText);
	d.wordDiff = of(colors.clrWordDiff);
	d.wordDiffDeleted = of(colors.clrWordDiffDeleted);
	d.wordDiffText = of(colors.clrWordDiffText);
	d.selWordDiff = of(colors.clrSelWordDiff);
	d.selWordDiffDeleted = of(colors.clrSelWordDiffDeleted);
	d.selWordDiffText = of(colors.clrSelWordDiffText);

	DIRCOLORSETTINGS dirColors;
	Options::DirColors::Load(mgr, dirColors);
	DirColors &r = c.dir;
	r.use = mgr->GetBool(OPT_DIRCLR_USE_COLORS);
	r.equal = of(dirColors.clrDirItemEqual);
	r.equalText = of(dirColors.clrDirItemEqualText);
	r.diff = of(dirColors.clrDirItemDiff);
	r.diffText = of(dirColors.clrDirItemDiffText);
	r.notExistAll = of(dirColors.clrDirItemNotExistAll);
	r.notExistAllText = of(dirColors.clrDirItemNotExistAllText);
	r.filtered = of(dirColors.clrDirItemFiltered);
	r.filteredText = of(dirColors.clrDirItemFilteredText);
	r.margin = of(dirColors.clrDirMargin);

	Options::SyntaxColors::Load(mgr, &mainSyntaxColors());
}

const DiffColors &diffColors()
{
	return cache().diff;
}

const DirColors &dirColors()
{
	return cache().dir;
}

QColor syntaxColor(int colorIndex)
{
	const ::SyntaxColors &colors = mainSyntaxColors();
	const int index = colorIndex & ~COLORINDEX_MASK;
	int systemIndex = 0;
	if (cache().defaultTextColoring && colors.GetSystemColorIndex(index, &systemIndex))
		return systemColor(systemIndex);
	return colorFromColorRef(static_cast<int>(static_cast<uint32_t>(colors.GetColor(index))));
}

bool syntaxBold(int colorIndex)
{
	return mainSyntaxColors().GetBold(colorIndex & ~COLORINDEX_MASK);
}

QColor markerColor(int color)
{
	return syntaxColor(COLORINDEX_MARKERBKGND0 + qBound(0, color, 3));
}

namespace ColorSchemes
{

QStringList folders()
{
	// the binary's own folder (see sharedFiltersDir)
	const QDir appDir(QFileInfo(QCoreApplication::applicationFilePath()).canonicalPath());
	QStringList list;
#ifdef Q_OS_MACOS
	list.append(QDir::cleanPath(appDir.filePath(QStringLiteral("../Resources/ColorSchemes"))));
#else
	// installed (or inside the AppImage), then next to a build's binary
	const QString installed = QDir::cleanPath(appDir.filePath(QStringLiteral("../share/libremerge/ColorSchemes")));
	const QString beside = QDir::cleanPath(appDir.filePath(QStringLiteral("ColorSchemes")));
	list.append(QFileInfo(installed).isDir() || !QFileInfo(beside).isDir() ? installed : beside);
#endif
	// GetColorSchemesFolders: then the application's data folder and the
	// Documents one, whichever user data goes to
	list.append(userDataFolder(QStringLiteral("ColorSchemes"), 0));
	list.append(userDataFolder(QStringLiteral("ColorSchemes"), 1));
	return list;
}

QString privateFolder()
{
	// GetPrivateColorSchemesFolder: as "Store user data in" says
	return userDataFolder(QStringLiteral("ColorSchemes"), userDataLocation());
}

bool isPrivatePath(const QString &path)
{
	const QString parent = QDir::cleanPath(QFileInfo(path).absolutePath());
	const QStringList list = folders();
	for (int i = 1; i < list.size(); ++i)
		if (parent.compare(list.at(i), Qt::CaseInsensitive) == 0)
			return true;
	return false;
}

QString path(const QString &name)
{
	for (const QString &folder : folders())
	{
		const QString file = QDir(folder).filePath(name + QStringLiteral(".ini"));
		if (QFileInfo::exists(file))
			return file;
	}
	return QString();
}

QStringList names()
{
	std::set<QString> names;
	for (const QString &folder : folders())
		for (const QFileInfo &file : QDir(folder).entryInfoList(QDir::Files, QDir::Name))
			if (file.suffix().compare(QStringLiteral("ini"), Qt::CaseInsensitive) == 0)
				names.insert(file.completeBaseName());
	return QStringList(names.begin(), names.end());
}

} // namespace ColorSchemes

QString colorSchemeName(bool dark)
{
	COptionsMgr *mgr = GetOptionsMgr();
	return mgr != nullptr
		? QString::fromStdString(mgr->GetString(dark ? OPT_COLOR_SCHEME_DARK : OPT_COLOR_SCHEME))
		: QString();
}

void setColorSchemeName(bool dark, const QString &name)
{
	if (COptionsMgr *mgr = GetOptionsMgr())
		mgr->SaveOption(dark ? OPT_COLOR_SCHEME_DARK : OPT_COLOR_SCHEME, name.toStdString());
}

bool importColorScheme(const QString &path)
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr)
		return false;
	cache().unhooked = true;
	const int result = path.isEmpty() ? COption::OPT_NOTFOUND
		: mgr->ImportOptions(path.toStdString());
	cache().unhooked = false;
	reloadColors();
	return result == COption::OPT_OK;
}

bool saveColorScheme(const QString &path)
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr)
		return false;
	SchemeFileOptions file;
	COLORREF customColors[16] = {};
	COLORSETTINGS diffColors;
	DIRCOLORSETTINGS dirColors;
	::SyntaxColors syntaxColors;
	Options::CustomColors::Init(&file);
	Options::CustomColors::Load(mgr, customColors);
	Options::CustomColors::Save(&file, customColors);
	Options::DiffColors::Init(&file);
	Options::DiffColors::Load(mgr, diffColors);
	Options::DiffColors::Save(&file, diffColors);
	Options::DirColors::Init(&file);
	Options::DirColors::Load(mgr, dirColors);
	Options::DirColors::Save(&file, dirColors);
	Options::SyntaxColors::Init(&file, &syntaxColors);
	Options::SyntaxColors::Load(mgr, &syntaxColors);
	Options::SyntaxColors::Save(&file, &syntaxColors);
	file.InitOption(OPT_SYSCOLOR_HOOK_ENABLED, false);
	file.SaveOption(OPT_SYSCOLOR_HOOK_ENABLED, mgr->GetBool(OPT_SYSCOLOR_HOOK_ENABLED));
	file.InitOption(OPT_SYSCOLOR_HOOK_COLORS, _T(""));
	file.SaveOption(OPT_SYSCOLOR_HOOK_COLORS, mgr->GetString(OPT_SYSCOLOR_HOOK_COLORS));

	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile out(path);
	if (!out.open(QIODevice::WriteOnly))
		return false;
	out.write(file.text());
	return out.commit();
}

bool colorsAreDark()
{
	COptionsMgr *mgr = GetOptionsMgr();
	return mgr != nullptr && mgr->GetInt(OPT_COLOR_MODE_EFFECTIVE) == 1;
}

void setColorsAreDark(bool dark)
{
	if (COptionsMgr *mgr = GetOptionsMgr())
		mgr->SaveOption(OPT_COLOR_MODE_EFFECTIVE, dark ? 1 : 0);
}

bool syncColorSchemeWithMode(bool dark)
{
	if (GetOptionsMgr() == nullptr || colorsAreDark() == dark)
		return false;
	// the scheme's import may fail (a scheme the folders no longer have):
	// the mode is in effect all the same, as upstream's
	importColorScheme(ColorSchemes::path(colorSchemeName(dark)));
	setColorsAreDark(dark);
	return true;
}

} // namespace lm
