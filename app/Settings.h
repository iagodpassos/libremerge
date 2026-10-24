// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVariant>

namespace lm
{

/** A setting of the Options that LibreMerge keeps itself rather than in
    the engine's options: what the command line set for this run (-cfg,
    by WinMerge's name of the option), or else the saved value. */
QVariant setting(const QString &key, const QVariant &defaultValue = QVariant());

/** The command line's -cfg for those settings: WinMerge's name of the
    option (OptionsDef.h), or the end of one when only one ends so, set
    for this run. False when it names none of them. */
bool useSetting(const QString &name, const QString &value);

/** What the command line set goes once the Options are saved: they were
    shown with it and saved it. */
void forgetCommandLineSettings();

} // namespace lm
