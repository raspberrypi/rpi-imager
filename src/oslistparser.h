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
#include <QJsonValue>
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

// Drop the icon forms that would turn a bad repository entry into a fetch of
// something unexpected -- a remote URL with no host, and a file URL naming
// one, whose local path is a UNC path and on Windows an SMB authentication
// attempt. Returns an empty string for those. It is not an allow-list: an
// unrecognised scheme is passed through on purpose, since QML may know one
// this does not and the alternative is dropping icons that would have worked.
// A test pins that choice.
QString sanitizeIconSource(const QString &raw);

// sanitizeIconSource(), then the routing every caller used to repeat: a
// remote icon becomes an "image://icons/" source so it is fetched by
// IconMultiFetcher rather than by Qt Quick. Returns an empty string for
// anything the sanitiser rejects.
QString iconSourceFor(const QString &raw);

// A byte count out of the OS list, or nothing.
//
// JSON numbers are doubles, and the repository is a setting -- one the
// bootloader's own flash can name -- so "extract_size": -1 and 1e30 are both
// things that can arrive. Converting either to quint64 is undefined, and on
// this architecture it saturates silently: -1 becomes 0, which is the answer
// that makes the capacity check pass on any card at all.
//
// Anything that is not a whole number in range comes back as 0, which the
// callers already read as "size unknown".
quint64 byteCountFromJson(const QJsonValue &value);

} // namespace oslist

#endif // OSLISTPARSER_H
