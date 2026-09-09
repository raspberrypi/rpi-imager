/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * "Why is my drive not in the list?"
 *
 * The storage step shows one message when it has nothing selectable to
 * offer, and that message is the only thing standing between the user and
 * a dead end. There are four quite different reasons the list can be
 * empty, and each has a different way out: fix permissions, plug something
 * in, plug in something writable, or untick the filter. Telling someone to
 * untick a filter when their card is write-protected sends them looking in
 * the wrong place.
 *
 * The branches are ordered, so which one wins when several conditions hold
 * at once is part of the behaviour.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "StorageStatusMessage"
    when: windowShown
    width: 700
    height: 500
    visible: true

    QtObject {
        id: containerStub
        property bool disableWarnings: false
        // An actual item, not null: the step parents its confirmation
        // dialogs onto this, and null makes that an undefined assignment to
        // a QQuickItem* -- the dialogs then have no parent, which nothing
        // here notices until something tries to open one.
        property var overlayRootRef: testCase
        property string networkInfoText: ""
        property string selectedStorageName: ""
        property bool targetIsFastboot: false
    }

    Component {
        id: stepComponent
        StorageSelectionStep { wizardContainer: containerStub }
    }

    property var step: null

    function initTestCase() {
        step = stepComponent.createObject(testCase)
        verify(step, "the storage step was created")
    }

    function cleanupTestCase() {
        if (step) {
            step.destroy()
            step = null
        }
    }

    // The filter checkbox is an id inside the step, so it is reached by the
    // objectName it carries rather than as a property of the root.
    property var filterBox: null

    function init() {
        if (!filterBox)
            filterBox = findChild(step, "filterSystemDrives")
        verify(filterBox, "the system-drive filter checkbox was found")

        step.enumerationErrorMessage = ""
        step.hasAnyDevices = true
        step.hasOnlyReadOnlyDevices = false
        filterBox.checked = true
    }

    // -- Nothing could be enumerated ---------------------------------------

    function test_an_enumeration_failure_says_so_and_names_the_cause() {
        // Distinct from "no devices": the list could not be read at all,
        // which on Linux is almost always permissions. Telling this user to
        // plug something in wastes their time.
        step.enumerationErrorMessage = "Permission denied"

        const msg = step.getStorageStatusMessage()
        verify(msg.indexOf("Permission denied") >= 0, "the cause is quoted")
        verify(msg.indexOf("administrator") >= 0, "and the way out is named")
    }

    function test_an_enumeration_failure_outranks_everything_else() {
        // If the list could not be read, nothing derived from it means
        // anything -- including "there are no devices".
        step.enumerationErrorMessage = "Permission denied"
        step.hasAnyDevices = false
        step.hasOnlyReadOnlyDevices = true

        verify(step.getStorageStatusMessage().indexOf("Permission denied") >= 0)
    }

    // -- Nothing is plugged in ---------------------------------------------

    function test_no_devices_asks_for_one_to_be_connected() {
        step.hasAnyDevices = false

        const msg = step.getStorageStatusMessage()
        verify(msg.indexOf("No storage devices found") >= 0)
        verify(msg.indexOf("connect") >= 0)
    }

    function test_no_devices_outranks_the_read_only_case() {
        // hasOnlyReadOnlyDevices is vacuously true of an empty list, so the
        // order here is what stops "all your devices are read-only" being
        // said to someone with no devices at all.
        step.hasAnyDevices = false
        step.hasOnlyReadOnlyDevices = true

        verify(step.getStorageStatusMessage().indexOf("No storage devices found") >= 0)
    }

    // -- Everything present is read-only -----------------------------------

    function test_read_only_devices_with_the_filter_on_offers_both_ways_out() {
        // Either the card is write-protected, or the writable one is a
        // system drive being hidden. Both are worth mentioning.
        step.hasOnlyReadOnlyDevices = true
        filterBox.checked = true

        const msg = step.getStorageStatusMessage()
        verify(msg.indexOf("read-only") >= 0)
        verify(msg.indexOf("Exclude system drives") >= 0,
               "the filter is mentioned, because it is on")
    }

    function test_read_only_devices_with_the_filter_off_does_not_mention_it() {
        // The filter is already off. Telling someone to untick a box they
        // have already unticked is the message sending them in circles.
        step.hasOnlyReadOnlyDevices = true
        filterBox.checked = false

        const msg = step.getStorageStatusMessage()
        verify(msg.indexOf("read-only") >= 0)
        verify(msg.indexOf("Exclude system drives") === -1,
               "no advice to untick what is already unticked")
        verify(msg.indexOf("writable") >= 0, "the remaining way out is named")
    }

    // -- There are writable devices, but all of them are filtered out ------

    function test_writable_devices_that_are_all_filtered_points_at_the_filter() {
        // Reachable only with the filter on: with it off, any writable
        // device is selectable and this message is not shown at all.
        step.hasAnyDevices = true
        step.hasOnlyReadOnlyDevices = false
        filterBox.checked = true

        const msg = step.getStorageStatusMessage()
        verify(msg.indexOf("Exclude system drives") >= 0)
        verify(msg.indexOf("read-only") === -1,
               "these devices are not read-only, so it must not say they are")
    }

    // -- Every branch says something -------------------------------------

    function test_every_combination_produces_a_message() {
        // Whatever state the step is in, a user staring at an empty list
        // gets told something rather than nothing.
        const flags = [false, true]
        for (const err of ["", "Permission denied"]) {
            for (const any of flags) {
                for (const ro of flags) {
                    for (const filter of flags) {
                        step.enumerationErrorMessage = err
                        step.hasAnyDevices = any
                        step.hasOnlyReadOnlyDevices = ro
                        filterBox.checked = filter

                        const msg = step.getStorageStatusMessage()
                        verify(msg && msg.length > 0,
                               "err=" + err + " any=" + any +
                               " ro=" + ro + " filter=" + filter)
                    }
                }
            }
        }
    }
}
