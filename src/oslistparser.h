/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Pure transformations over the OS-list JSON served by the repository.
 *
 * These decide what the user actually sees in the OS chooser: which entries
 * survive validation, which language's list is used, what order entries
 * appear in for the attached hardware, and which icon URLs are safe to hand
 * to the image provider. They used to live in an anonymous namespace inside
 * oslistmodel.cpp, where nothing could reach them without a QML engine and
 * an ImageWriter; they depend on neither, so they live here instead and are
 * covered by src/test/oslist_parser_test.cpp.
 */

#ifndef OSLISTPARSER_H
#define OSLISTPARSER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace oslist {

// True for an init_format the customisation code knows how to honour.
// The empty string is valid and means "this entry supports no customisation".
bool isValidInitFormat(const QString &initFormat);

// Recursively drop entries whose init_format is not one we understand,
// descending into "subitems".
QJsonArray filterInvalidInitFormats(const QJsonArray &list);

// Pick the list for `localeName` ("os_list_<locale>", then the bare language,
// then plain "os_list"). The locale is a parameter rather than read from
// QLocale so the choice can be exercised without changing the environment.
QJsonArray getListForLocale(const QJsonObject &root, const QString &localeName);

// Same, for the system locale.
QJsonArray getListForLocale(const QJsonObject &root);

// Full parse: locale selection, init_format filtering, and shuffling of any
// group flagged "random".
QJsonArray parseOSJson(const QJsonObject &root);

// Stable partition of each entry's subitems so those matching
// `preferredArchitecture` come first.
void applyArchitectureSorting(QJsonArray &list, const QString &preferredArchitecture);

// Allow known-good icon forms and drop malformed ones, so a bad repository
// entry cannot turn into a runtime fetch of something unexpected. Returns an
// empty string for anything rejected.
QString sanitizeIconSource(const QString &raw);

} // namespace oslist

#endif // OSLISTPARSER_H
