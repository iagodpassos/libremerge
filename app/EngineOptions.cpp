// SPDX-License-Identifier: GPL-3.0-or-later
// In-memory options manager wired into the engine's GetOptionsMgr().
// Mirrors the shape used by the test harness; a persistent (INI-backed)
// implementation replaces this later in Phase 1.
#include "pch.h"
#include "EngineOptions.h"
#include "OptionsMgr.h"
#include "options_global.h"

namespace
{

class InMemoryOptionsMgr : public COptionsMgr
{
public:
	int InitOption(const String& name, const varprop::VariantValue& defaultValue) override
	{
		return AddOption(name, defaultValue);
	}
	int InitOption(const String& name, const String& defaultValue) override
	{
		varprop::VariantValue val;
		val.SetString(defaultValue);
		return AddOption(name, val);
	}
	int InitOption(const String& name, const tchar_t *defaultValue) override
	{
		return InitOption(name, String(defaultValue));
	}
	int InitOption(const String& name, int defaultValue, bool serializable = true) override
	{
		(void)serializable;
		varprop::VariantValue val;
		val.SetInt(defaultValue);
		return AddOption(name, val);
	}
	int InitOption(const String& name, bool defaultValue) override
	{
		varprop::VariantValue val;
		val.SetBool(defaultValue);
		return AddOption(name, val);
	}
	int SaveOption(const String& name) override { (void)name; return COption::OPT_OK; }
	int SaveOption(const String& name, const varprop::VariantValue& value) override
	{
		return Set(name, value);
	}
	int SaveOption(const String& name, const String& value) override
	{
		varprop::VariantValue val;
		val.SetString(value);
		return Set(name, val);
	}
	int SaveOption(const String& name, const tchar_t *value) override
	{
		return SaveOption(name, String(value));
	}
	int SaveOption(const String& name, int value) override
	{
		varprop::VariantValue val;
		val.SetInt(value);
		return Set(name, val);
	}
	int SaveOption(const String& name, bool value) override
	{
		varprop::VariantValue val;
		val.SetBool(value);
		return Set(name, val);
	}
	int FlushOptions() override { return COption::OPT_OK; }
	void SetSerializing(bool serializing = true) override { (void)serializing; }
};

} // namespace

namespace lm
{

void installEngineOptions()
{
	static InMemoryOptionsMgr options;
	SetOptionsMgr(&options);
}

} // namespace lm
