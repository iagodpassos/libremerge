// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "FileFilters.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include "DiffContext.h"
#include "DiffItem.h"
#include "DiffWrapper.h"
#include "FileFilter.h"
#include "FileFilterHelper.h"
#include "FileFilterMgr.h"
#include "FilterEngine/FilterError.h"
#include "FilterEngine/FilterExpression.h"
#include "FilterEngine/ILineDataProvider.h"
#include "LineFilterHelper.h"
#include "OptionsDef.h"
#include "OptionsMgr.h"
#include "options_global.h"

namespace
{

const QString kHistoryKey = QStringLiteral("Files/Ext");
const int kHistoryMax = 20; // CSuperComboBox's DEF_MAXSIZE

FileFilterHelper &globalFilter()
{
	static FileFilterHelper helper;
	return helper;
}

QString &sharedDirOverride()
{
	static QString dir;
	return dir;
}

QString &userDirOverride()
{
	static QString dir;
	return dir;
}

/** The text in the language shown. (Named so that lupdate, which takes
    any translate() for QCoreApplication's, reads the QT_TRANSLATE_NOOP
    inside the calls.) */
QString translated(const char *text)
{
	return QCoreApplication::translate("FileFilters", text);
}

/** WinMerge's GetFilterErrorMessage. */
QString filterErrorMessage(FilterErrorCode code)
{
	switch (code)
	{
	case FILTER_ERROR_NO_ERROR:
		return translated(QT_TRANSLATE_NOOP("FileFilters", "No error"));
	case FILTER_ERROR_EMPTY_EXPRESSION:
		return translated(QT_TRANSLATE_NOOP("FileFilters", "Filter expression is empty"));
	case FILTER_ERROR_UNKNOWN_CHAR:
		return translated(QT_TRANSLATE_NOOP("FileFilters",
			"Unknown character in filter expression"));
	case FILTER_ERROR_UNTERMINATED_STRING:
		return translated(QT_TRANSLATE_NOOP("FileFilters", "Unterminated string literal"));
	case FILTER_ERROR_SYNTAX_ERROR:
		return translated(QT_TRANSLATE_NOOP("FileFilters",
			"Syntax error in filter expression"));
	case FILTER_ERROR_PARSE_FAILURE:
		return translated(QT_TRANSLATE_NOOP("FileFilters",
			"Failed to parse filter expression"));
	case FILTER_ERROR_INVALID_LITERAL:
		return translated(QT_TRANSLATE_NOOP("FileFilters", "Invalid literal value"));
	case FILTER_ERROR_INVALID_ARGUMENT_COUNT:
		return translated(QT_TRANSLATE_NOOP("FileFilters", "Invalid number of arguments"));
	case FILTER_ERROR_INVALID_REGULAR_EXPRESSION:
		return translated(QT_TRANSLATE_NOOP("FileFilters", "Invalid regular expression"));
	case FILTER_ERROR_UNDEFINED_IDENTIFIER:
		return translated(QT_TRANSLATE_NOOP("FileFilters", "Undefined identifier"));
	case FILTER_ERROR_FILTER_NAME_NOT_FOUND:
		return translated(QT_TRANSLATE_NOOP("FileFilters", "Filter name not found"));
	case FILTER_ERROR_DIVIDE_BY_ZERO:
		return translated(QT_TRANSLATE_NOOP("FileFilters",
			"Division by zero in filter expression"));
	case FILTER_ERROR_INVALID_PROPERTY_NAME:
		return translated(QT_TRANSLATE_NOOP("FileFilters", "Invalid property name"));
	case FILTER_ERROR_INVALID_DIRECTIVE:
		return translated(QT_TRANSLATE_NOOP("FileFilters", "Invalid directive"));
	default:
		return translated(QT_TRANSLATE_NOOP("FileFilters", "Unknown error"));
	}
}

} // namespace

namespace lm
{

QString sharedFiltersDir()
{
	if (!sharedDirOverride().isEmpty())
		return sharedDirOverride();
	const QDir appDir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
	// the bundle's Contents/Resources/Filters
	const QStringList candidates = { appDir.filePath(QStringLiteral("../Resources/Filters")) };
#else
	// installed (or inside the AppImage), then next to a build's binary
	const QStringList candidates = {
		appDir.filePath(QStringLiteral("../share/libremerge/Filters")),
		appDir.filePath(QStringLiteral("Filters")),
	};
#endif
	for (const QString &candidate : candidates)
		if (QFileInfo(candidate).isDir())
			return QDir::cleanPath(candidate);
	return QDir::cleanPath(candidates.first());
}

QString userFiltersDir()
{
	if (!userDirOverride().isEmpty())
		return userDirOverride();
	// next to the settings, as WinMerge keeps them under its AppData
	// folder: ~/Library/Application Support/LibreMerge/Filters on macOS,
	// ~/.config/LibreMerge/Filters elsewhere
#ifdef Q_OS_MACOS
	const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#else
	const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
#endif
	return QDir(base).filePath(QCoreApplication::organizationName() + QStringLiteral("/Filters"));
}

void loadFilterFiles(FileFilterHelper *helper)
{
	helper->GetManager()->DeleteAllFilters();
	const String pattern = String(_T("*")) + FileFilterExt;
	// the shipped ones first, as upstream reads its program folder first
	for (const QString &dir : { sharedFiltersDir(), userFiltersDir() })
		if (!dir.isEmpty() && QFileInfo(dir).isDir())
			helper->LoadFileFilterDirPattern(QDir::toNativeSeparators(dir).toStdString(), pattern);
}

void setFiltersDirsForTest(const QString &shared, const QString &user)
{
	sharedDirOverride() = shared;
	userDirOverride() = user;
	FileFilterHelper &global = globalFilter();
	loadFilterFiles(&global);
	global.SetMaskOrExpression(global.GetMaskOrExpression());
}

void installFileFilters()
{
	COptionsMgr *mgr = GetOptionsMgr();
	if (mgr == nullptr)
		return;
	// until 0.9.7 the mask was the folder comparison's own field
	QSettings settings;
	const QString oldKey = QStringLiteral("FolderCompare/Filter");
	if (settings.contains(oldKey))
	{
		const QString old = settings.value(oldKey).toString().trimmed();
		settings.remove(oldKey);
		const QString key = QString::fromStdString(OPT_FILEFILTER_CURRENT);
		if (!old.isEmpty() && !settings.contains(key))
			settings.setValue(key, old);
	}
	mgr->InitOption(OPT_FILEFILTER_CURRENT, _T("*.*"));
	FileFilterHelper &global = globalFilter();
	loadFilterFiles(&global);
	global.SetMaskOrExpression(mgr->GetString(OPT_FILEFILTER_CURRENT));
}

QString fileFilterMask()
{
	return QString::fromStdString(globalFilter().GetMaskOrExpression());
}

void setFileFilterMask(const QString &mask)
{
	QString filter = mask.trimmed();
	if (filter.isEmpty())
		filter = QStringLiteral("*.*");
	globalFilter().SetMaskOrExpression(filter.toStdString());
	if (COptionsMgr *mgr = GetOptionsMgr())
	{
		mgr->SaveOption(OPT_FILEFILTER_CURRENT, filter.toStdString());
		mgr->FlushOptions();
	}
}

FileFilterHelper *globalFileFilter()
{
	return &globalFilter();
}

std::shared_ptr<FileFilterHelper> cloneFileFilter()
{
	FileFilterHelper &global = globalFilter();
	global.ReloadUpdatedFilters();
	// a preset's rules are copied in when the mask naming it is parsed:
	// parsing again is what makes a preset edited on disk count, as
	// upstream means its reload to ("we always compare with latest saved
	// filters")
	global.SetMaskOrExpression(global.GetMaskOrExpression());
	auto copy = std::make_shared<FileFilterHelper>();
	copy->CloneFrom(&global);
	return copy;
}

void adoptFileFilter(const FileFilterHelper &edited)
{
	FileFilterHelper &global = globalFilter();
	global.CloneFrom(&edited);
	if (COptionsMgr *mgr = GetOptionsMgr())
	{
		mgr->SaveOption(OPT_FILEFILTER_CURRENT, global.GetMaskOrExpression());
		mgr->FlushOptions();
	}
}

QString formatFilterError(const FileFilterErrorInfo &error)
{
	if (error.errorCode == FILTER_ERROR_NO_ERROR)
		return QString();
	QString message = filterErrorMessage(error.errorCode);
	if (error.errorPosition >= 0)
		message += QLatin1Char(' ') + translated(QT_TRANSLATE_NOOP("FileFilters", "at position"))
			+ QLatin1Char(' ') + QString::number(error.errorPosition + 1);
	message += QStringLiteral(": ") + QString::fromStdString(error.srcText);
	if (!error.context.empty())
		message += QStringLiteral(" (") + QString::fromStdString(error.context)
			+ QLatin1Char(')');
	return message;
}

QString formatFilterError(const FilterExpression &expression)
{
	if (expression.errorCode == FILTER_ERROR_NO_ERROR)
		return QString();
	QString message = filterErrorMessage(expression.errorCode);
	if (expression.errorPosition >= 0)
		message += QLatin1Char(' ') + translated(QT_TRANSLATE_NOOP("FileFilters", "at position"))
			+ QLatin1Char(' ') + QString::number(expression.errorPosition + 1);
	message += QStringLiteral(": ") + QString::fromStdString(expression.expression);
	if (!expression.name.empty())
		message += QStringLiteral(" (\"") + QString::fromStdString(expression.name)
			+ QStringLiteral("\")");
	return message;
}

QStringList lineFilterErrors(LineFilterHelper *helper, const QString &filter)
{
	helper->SetStringOrExpression(filter.toStdString());
	const QString error = formatFilterError(helper->GetFilterExpression());
	return error.isEmpty() ? QStringList() : QStringList{ error };
}

bool lineFilterHides(LineFilterHelper *filter)
{
	return !filter->GetStringOrExpression().empty()
		&& filter->GetFilterExpression().errorCode == FILTER_ERROR_NO_ERROR;
}

QList<bool> linesHiddenByFilter(LineFilterHelper *filter, const ILineDataProvider &lines,
	const QList<ComparedFile> &files, int significantDiffs, int ignoredDiffs)
{
	QList<bool> hidden;
	if (!lineFilterHides(filter))
		return hidden;
	FilterExpression &expression = filter->GetFilterExpression();
	// CreateDiffItem: the files, for what the expression asks about them
	const int sides = static_cast<int>(files.size());
	PathContext paths;
	paths.SetSize(sides);
	DIFFITEM item;
	item.diffcode.diffcode = (significantDiffs > 0 ? DIFFCODE::DIFF : DIFFCODE::SAME)
		| DIFFCODE::TEXT | DIFFCODE::FILE | (sides > 2 ? DIFFCODE::THREEWAY : 0);
	item.nsdiffs = significantDiffs;
	item.nidiffs = ignoredDiffs;
	for (int side = 0; side < sides; ++side)
	{
		const ComparedFile &file = files.at(side);
		if (file.path.isEmpty())
		{
			paths.SetPath(side, String(), false);
			continue; // an untitled pane
		}
		const QFileInfo info(file.path);
		paths.SetPath(side, info.absolutePath().toStdString(), false);
		item.diffcode.setSideFlag(side);
		item.diffFileInfo[side].Update(info.absoluteFilePath().toStdString());
		item.diffFileInfo[side].SetFile(info.fileName().toStdString());
		item.diffFileInfo[side].encoding.SetUnicoding(
			static_cast<ucr::UNICODESET>(file.unicoding));
		item.diffFileInfo[side].encoding.SetCodepage(file.codepage);
		item.diffFileInfo[side].encoding.m_bom = file.bom;
	}
	CDiffContext context(paths, CMP_CONTENT);
	expression.SetDiffContext(&context);
	FilterSharedContext shared;
	FilterEvalContext evaluation{ &expression, &item, &lines, &shared };
	const int count = lines.GetLineCount();
	hidden.reserve(count);
	bool any = false;
	for (int line = 0; line < count; ++line)
	{
		evaluation.lineIndex = line;
		const bool hide = !expression.Evaluate(evaluation);
		hidden.append(hide);
		any = any || hide;
	}
	expression.SetDiffContext(nullptr);
	if (!any)
		hidden.clear();
	return hidden;
}

QStringList fileFilterErrors(FileFilterHelper *helper, const QString &mask)
{
	helper->SetMaskOrExpression(mask.toStdString());
	QStringList errors;
	for (const FileFilterErrorInfo *error : helper->GetErrorList())
		errors.append(formatFilterError(*error));
	return errors;
}

QString fileFilterHistoryKey()
{
	return kHistoryKey;
}

QString displayFilterHistoryKey()
{
	return QStringLiteral("Files/DisplayExt");
}

QString lineDisplayFilterHistoryKey()
{
	return QStringLiteral("Files/DisplayLine");
}

QStringList fileFilterHistory(const QString &key)
{
	return QSettings().value(key).toStringList();
}

void rememberFileFilter(const QString &mask, const QString &key)
{
	// CSuperComboBox::SaveState: what the field holds goes first, then
	// the rest of the list without it
	const QString current = mask.trimmed();
	QStringList history;
	if (!current.isEmpty())
		history.append(current);
	for (const QString &entry : fileFilterHistory(key))
	{
		const QString trimmed = entry.trimmed();
		if (history.size() < kHistoryMax && !trimmed.isEmpty() && !history.contains(trimmed))
			history.append(trimmed);
	}
	QSettings().setValue(key, history);
}

} // namespace lm
