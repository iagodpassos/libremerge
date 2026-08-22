// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent options manager wired into the engine's GetOptionsMgr().
// Values live in QSettings (plist on macOS, INI elsewhere); option names
// like "Settings/IgnoreSpace" map naturally onto QSettings groups.
#include "pch.h"
#include "EngineOptions.h"

#include <QSettings>
#include <QVariant>

#include "OptionsMgr.h"
#include "OptionsDef.h"
#include "options_global.h"
#include "UnicodeString.h"
#include "stringdiffs.h"

namespace
{

QString toKey(const String &name)
{
	return QString::fromUtf8(name.data(), static_cast<int>(name.size()));
}

class QSettingsOptionsMgr : public COptionsMgr
{
public:
	int InitOption(const String& name, const varprop::VariantValue& defaultValue) override
	{
		const int result = AddOption(name, defaultValue);
		if (result != COption::OPT_OK)
			return result;
		loadSaved(name, defaultValue);
		return result;
	}
	int InitOption(const String& name, const String& defaultValue) override
	{
		varprop::VariantValue val;
		val.SetString(defaultValue);
		return InitOption(name, val);
	}
	int InitOption(const String& name, const tchar_t *defaultValue) override
	{
		return InitOption(name, String(defaultValue));
	}
	int InitOption(const String& name, int defaultValue, bool serializable = true) override
	{
		varprop::VariantValue val;
		val.SetInt(defaultValue);
		if (!serializable)
			return AddOption(name, val);
		return InitOption(name, val);
	}
	int InitOption(const String& name, bool defaultValue) override
	{
		varprop::VariantValue val;
		val.SetBool(defaultValue);
		return InitOption(name, val);
	}
	int SaveOption(const String& name) override
	{
		const varprop::VariantValue value = Get(name);
		switch (value.GetType())
		{
		case varprop::VT_STRING:
			m_settings.setValue(toKey(name), QString::fromStdString(value.GetString()));
			break;
		case varprop::VT_INT:
			m_settings.setValue(toKey(name), value.GetInt());
			break;
		case varprop::VT_BOOL:
			m_settings.setValue(toKey(name), value.GetBool());
			break;
		default:
			return COption::OPT_UNKNOWN_TYPE;
		}
		return COption::OPT_OK;
	}
	int SaveOption(const String& name, const varprop::VariantValue& value) override
	{
		const int result = Set(name, value);
		return result == COption::OPT_OK ? SaveOption(name) : result;
	}
	int SaveOption(const String& name, const String& value) override
	{
		varprop::VariantValue val;
		val.SetString(value);
		return SaveOption(name, val);
	}
	int SaveOption(const String& name, const tchar_t *value) override
	{
		return SaveOption(name, String(value));
	}
	int SaveOption(const String& name, int value) override
	{
		varprop::VariantValue val;
		val.SetInt(value);
		return SaveOption(name, val);
	}
	int SaveOption(const String& name, bool value) override
	{
		varprop::VariantValue val;
		val.SetBool(value);
		return SaveOption(name, val);
	}
	int FlushOptions() override
	{
		m_settings.sync();
		return COption::OPT_OK;
	}
	void SetSerializing(bool serializing = true) override { (void)serializing; }

private:
	void loadSaved(const String &name, const varprop::VariantValue &defaultValue)
	{
		const QString key = toKey(name);
		if (!m_settings.contains(key))
			return;
		const QVariant saved = m_settings.value(key);
		varprop::VariantValue value(defaultValue);
		switch (defaultValue.GetType())
		{
		case varprop::VT_STRING:
			value.SetString(saved.toString().toStdString());
			break;
		case varprop::VT_INT:
			value.SetInt(saved.toInt());
			break;
		case varprop::VT_BOOL:
			value.SetBool(saved.toBool());
			break;
		default:
			return;
		}
		Set(name, value);
	}

	QSettings m_settings;
};

} // namespace

namespace lm
{

void installEngineOptions()
{
	static QSettingsOptionsMgr options;
	SetOptionsMgr(&options);
	strdiff::Init(); // word-diff break characters

	// comparison option defaults (persisted values override these)
	options.InitOption(OPT_CMP_IGNORE_WHITESPACE, 0);
	options.InitOption(OPT_CMP_IGNORE_BLANKLINES, false);
	options.InitOption(OPT_CMP_IGNORE_CASE, false);
	options.InitOption(OPT_CMP_IGNORE_NUMBERS, false);
	options.InitOption(OPT_CMP_IGNORE_EOL, false);
	options.InitOption(OPT_CMP_DIFF_ALGORITHM, 0);
}

DIFFOPTIONS currentDiffOptions()
{
	COptionsMgr *mgr = GetOptionsMgr();
	DIFFOPTIONS options{};
	if (mgr == nullptr)
		return options;
	options.nIgnoreWhitespace = mgr->GetInt(OPT_CMP_IGNORE_WHITESPACE);
	options.bIgnoreBlankLines = mgr->GetBool(OPT_CMP_IGNORE_BLANKLINES);
	options.bIgnoreCase = mgr->GetBool(OPT_CMP_IGNORE_CASE);
	options.bIgnoreNumbers = mgr->GetBool(OPT_CMP_IGNORE_NUMBERS);
	options.bIgnoreEol = mgr->GetBool(OPT_CMP_IGNORE_EOL);
	options.nDiffAlgorithm = mgr->GetInt(OPT_CMP_DIFF_ALGORITHM);
	return options;
}

} // namespace lm
