/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Registers the application's Qt resources in a test binary.
 *
 * The application compiles its .qrc files straight into the executable, so
 * their initialisers run on their own and nothing has to ask. The test
 * binaries link the same sources as a static library instead, and there the
 * initialiser lives in an object the linker has no reason to pull in --
 * nothing references it. The resources are then simply absent at runtime.
 *
 * That failure is quiet and misleading. QFile(":/timezones.txt") does not
 * error, it just does not open, so getTimezoneList() hands back an empty
 * list and the customisation dialog offers no timezones -- which reads
 * exactly like a bug in the application rather than a missing resource in
 * the harness. Worse, a test written to tolerate an empty list would pass
 * against a binary that has no resources at all.
 */

#ifndef RPI_TEST_APP_RESOURCES_H
#define RPI_TEST_APP_RESOURCES_H

#include <QtGlobal>

// Deliberately at global scope. Q_INIT_RESOURCE emits its extern declaration
// wherever it is used, so inside a namespace it declares -- and then fails to
// find -- a namespaced symbol. The initialisers the resource compiler emits
// are at global scope.
inline void initAppResources()
{
    // Names are the .qrc basenames: qml.qrc from the source tree, and the
    // three generated at build time.
    Q_INIT_RESOURCE(qml);
    Q_INIT_RESOURCE(timezones_generated);
    Q_INIT_RESOURCE(countries_generated);
    Q_INIT_RESOURCE(translations);
}

#endif // RPI_TEST_APP_RESOURCES_H
