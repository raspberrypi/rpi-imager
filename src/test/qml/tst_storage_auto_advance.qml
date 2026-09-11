/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The one thing on the storage step that must never happen.
 *
 * The list supports keyboard auto-advance: choose a drive and the wizard
 * moves straight on. For a system drive it must not, because the
 * confirmation that follows -- typing the drive's name -- is the only thing
 * between the user and writing over the operating system they are running.
 * Auto-advancing past it would carry someone onto the next step with their
 * own boot disk selected and no warning shown.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "StorageAutoAdvance"
    when: windowShown
    width: 700
    height: 500
    visible: true

    QtObject {
        id: containerStub
        property bool disableWarnings: false
        property var overlayRootRef: testCase
        property string networkInfoText: ""
        property string selectedStorageName: ""
        property bool targetIsFastboot: false
    }

    // A drive as the delegate expects to receive it. A list of QObjects is
    // used rather than a ListModel because the delegate requires modelData
    // as well as each named role, and a ListModel supplies only the roles.
    component FakeDrive: QtObject {
        property string device: "/dev/sdz"
        property string description: "Test Drive"
        property string size: "32000000000"
        property bool isUsb: true
        property bool isScsi: false
        property bool isReadOnly: false
        property bool isSystem: false
        property var mountpoints: []
    }

    FakeDrive { id: ordinaryDrive }
    FakeDrive {
        id: systemDrive
        device: "/dev/sda"
        description: "The disk this computer is running from"
        isSystem: true
    }

    Component {
        id: stepComponent
        StorageSelectionStep { wizardContainer: containerStub }
    }

    property var step: null

    SignalSpy {
        id: advanced
        signalName: "nextClicked"
    }

    function init() {
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        advanced.target = step
        advanced.clear()
    }

    function cleanup() {
        advanced.target = null
        if (step) {
            step.destroy()
            step = null
        }
    }

    // Put the given drives in the list and select one, waiting for the
    // delegate to exist -- conditionalNext() asks the delegate first.
    function selectDrive(drives, index) {
        step.dstlist.model = drives
        tryVerify(function () { return step.dstlist.count === drives.length }, 3000,
                  "the drives are listed")
        step.dstlist.currentIndex = index
        tryVerify(function () { return step.dstlist.itemAtIndex(index) !== null }, 3000,
                  "the delegate for the selection exists")
    }

    function test_choosing_a_system_drive_does_not_advance() {
        // The case that matters. Moving on here would skip the confirmation
        // that asks the user to type the name of the disk being erased.
        selectDrive([ordinaryDrive, systemDrive], 1)

        step.conditionalNext()

        compare(advanced.count, 0,
                "auto-advance was refused for the system drive")
    }

    function test_choosing_an_ordinary_drive_advances() {
        // The counterpart: refusing for everything would make the keyboard
        // path useless and is not what is being asserted above.
        selectDrive([ordinaryDrive, systemDrive], 0)

        step.conditionalNext()

        compare(advanced.count, 1, "auto-advance went ahead")
    }

    function test_choosing_nothing_advances() {
        // With no selection there is no system drive to protect, and the
        // step's own Next button is what is disabled in that state.
        step.dstlist.model = [ordinaryDrive]
        tryVerify(function () { return step.dstlist.count === 1 }, 3000)
        step.dstlist.currentIndex = -1

        step.conditionalNext()

        compare(advanced.count, 1)
    }

    function test_the_plain_next_function_always_advances() {
        // next() is wired to the button, which the user presses deliberately
        // after reading the screen; the confirmation is raised from there.
        // Only the keyboard shortcut is conditional.
        selectDrive([systemDrive], 0)

        step.next()

        compare(advanced.count, 1)
    }
}
