// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: stub of the Windows Property System integration
// (PropertySystem.cpp drives IPropertyStore / shell property columns, plus
// content hashes). No properties are provided on POSIX yet; folder compare
// runs with the feature disabled. A native implementation (hashes, EXIF,
// file metadata) is a Phase 2 item.
#ifndef _WIN32

#include "pch.h"
#include "PropertySystem.h"
#include "FilterEngine/FilterExpressionNodes.h"
#include "FilterEngine/FilterExpression.h"
#include "FilterEngine/FilterError.h"
#include "unicoder.h"

PropertyValues::PropertyValues() = default;
PropertyValues::~PropertyValues() = default;

int PropertyValues::CompareValues(const PropertyValues& values1, const PropertyValues& values2, unsigned index)
{
	(void)values1; (void)values2; (void)index;
	return 0;
}

int64_t PropertyValues::DiffValues(const PropertyValues& values1, const PropertyValues& values2, unsigned index, bool& numeric)
{
	(void)values1; (void)values2; (void)index;
	numeric = false;
	return 0;
}

int PropertyValues::CompareAllValues(const PropertyValues& values1, const PropertyValues& values2)
{
	(void)values1; (void)values2;
	return 0;
}

bool PropertyValues::IsEmptyValue(size_t index) const
{
	(void)index;
	return true;
}

bool PropertyValues::IsHashValue(size_t index) const
{
	(void)index;
	return false;
}

std::vector<uint8_t> PropertyValues::GetHashValue(size_t index) const
{
	(void)index;
	return {};
}

PropertySystem::PropertySystem(ENUMFILTER filter)
{
	(void)filter;
}

PropertySystem::PropertySystem(const std::vector<String>& canonicalNames)
{
	(void)canonicalNames; // none are recognized on this platform yet
}

bool PropertySystem::GetPropertyValues(const String& path, PropertyValues& values)
{
	(void)path;
	values.Resize(m_canonicalNames.size());
	return false;
}

int PropertySystem::GetPropertyIndex(const String& canonicalName)
{
	(void)canonicalName;
	return -1;
}

bool PropertySystem::GetPropertyType(unsigned index, VARTYPE& vt) const
{
	(void)index;
	vt = 0;
	return false;
}

String PropertySystem::FormatPropertyValue(const PropertyValues& values, unsigned index)
{
	(void)values; (void)index;
	return String();
}

bool PropertySystem::GetDisplayNames(std::vector<String>& names)
{
	names = m_canonicalNames;
	return false;
}

bool PropertySystem::HasHashProperties() const
{
	return false;
}

// Property functions in filter expressions ("prop(...)" etc.) are backed by
// the property system; without one, referencing a property is an error the
// filter engine already knows how to report.
void FunctionNode::SetPropFunc(int side, int prefixlen, bool singlePane)
{
	(void)side; (void)prefixlen; (void)singlePane;
	if (!args || args->size() != 1)
		throw std::invalid_argument(functionName + " function requires 1 argument");
	auto strLit = dynamic_cast<StringLiteral*>((*args)[0]);
	if (!strLit)
		throw std::invalid_argument(functionName + " function requires a string literal as argument");
	throw InvalidPropertyNameError(strLit->value);
}

#endif // !_WIN32
