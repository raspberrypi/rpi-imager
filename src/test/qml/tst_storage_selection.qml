/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * StorageSelectionStep: choosing the drive that is about to be erased.
 *
 * Three refusals and one confirmation, none of them covered. A read-only
 * device cannot be chosen at all. A system drive is not chosen when
 * clicked -- it raises the dialog that asks for its name to be typed, and
 * only that dialog's confirmation selects it. Cancelling that dialog has
 * to leave nothing selected, or the whole typed confirmation is
 * decorative: the user declines and the drive is chosen anyway.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "StorageSelection"
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

    Component {
        id: stepComponent
        StorageSelectionStep { wizardContainer: containerStub }
    }

    property var step: null

    SignalSpy {
        id: advanced
        signalName: "nextClicked"
    }

    // selectDstItem() reads plain properties off whatever it is handed, so a
    // JS object stands in for the delegate.
    function drive(overrides) {
        var d = {
            device: "/dev/sdz",
            description: "Generic Mass-Storage",
            size: "32000000000",
            mountpoints: [],
            isSystem: false,
            isReadOnly: false,
            isRpiboot: false,
            isFastbootStorage: false,
            fastbootBlockDevice: "",
            unselectable: false
        }
        for (var k in overrides) d[k] = overrides[k]
        return d
    }

    function init() {
        containerStub.selectedStorageName = ""
        containerStub.targetIsFastboot = false
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

    function systemDialog() {
        var d = findChild(step, "systemDriveConfirmDialog")
        verify(d, "found the system drive confirmation")
        return d
    }

    // ── What cannot be chosen ─────────────────────────────────────────

    function test_an_unselectable_device_is_not_chosen() {
        // A card with its write-protect tab set, and nothing else about it
        // that makes it a valid target.
        step.selectDstItem(drive({ unselectable: true }))

        compare(containerStub.selectedStorageName, "")
        verify(!step.nextButtonEnabled)
    }

    // ── An ordinary drive ─────────────────────────────────────────────

    function test_an_ordinary_drive_is_chosen_directly() {
        step.selectDstItem(drive({}))

        compare(containerStub.selectedStorageName, "Generic Mass-Storage")
        verify(step.nextButtonEnabled)
        compare(advanced.count, 0,
                "and it does not advance on its own -- Next is for that")
    }

    function test_the_kind_of_target_is_recorded_data() {
        // Drives the organisation branch of the Pi Connect step: a fastboot
        // target registers its device identity at flash time, anything else
        // gets an auth key written into the image.
        return [
            { tag: "an ordinary card", over: {}, fastboot: false },
            { tag: "fastboot storage", over: { isFastbootStorage: true }, fastboot: true },
            { tag: "an rpiboot device", over: { isRpiboot: true }, fastboot: true }
        ]
    }

    function test_the_kind_of_target_is_recorded(data) {
        step.selectDstItem(drive(data.over))

        compare(containerStub.targetIsFastboot, data.fastboot, data.tag)
    }

    // ── A system drive ────────────────────────────────────────────────

    function test_clicking_a_system_drive_asks_before_choosing_it() {
        // The click raises the dialog. Nothing is selected yet, which is the
        // point: the confirmation is not a notice shown after the fact.
        step.selectDstItem(drive({
            isSystem: true,
            description: "The disk this computer runs from",
            device: "/dev/sda"
        }))

        tryVerify(function () { return systemDialog().opened }, 3000,
                  "the confirmation was raised")
        compare(containerStub.selectedStorageName, "",
                "and the drive is not chosen")
        verify(!step.nextButtonEnabled)
    }

    function test_the_dialog_is_told_which_drive_it_is_asking_about() {
        // It asks the user to type this name, so it has to be the name of
        // the drive that was actually clicked.
        step.selectDstItem(drive({
            isSystem: true,
            description: "The disk this computer runs from",
            device: "/dev/sda"
        }))

        compare(systemDialog().driveName, "The disk this computer runs from")
        compare(systemDialog().device, "/dev/sda")
    }

    function test_declining_the_confirmation_chooses_nothing() {
        // The case that makes the confirmation mean anything. If cancelling
        // still selected the drive, typing its name would be a formality
        // and declining would have no effect.
        step.selectDstItem(drive({ isSystem: true, device: "/dev/sda" }))
        tryVerify(function () { return systemDialog().opened }, 3000)

        systemDialog().cancelled()

        compare(containerStub.selectedStorageName, "")
        verify(!step.nextButtonEnabled)
        compare(advanced.count, 0)
    }

    function test_confirming_chooses_the_drive_and_moves_on() {
        // Having typed the name of their own boot disk, the user does not
        // need to be asked again by the Next button.
        step.selectDstItem(drive({
            isSystem: true,
            description: "The disk this computer runs from",
            device: "/dev/sda"
        }))
        tryVerify(function () { return systemDialog().opened }, 3000)

        systemDialog().confirmed()

        compare(containerStub.selectedStorageName, "The disk this computer runs from")
        verify(step.nextButtonEnabled)
        tryVerify(function () { return advanced.count === 1 }, 3000,
                  "and it advanced")
    }

    function test_confirming_puts_the_filter_back_on() {
        // Having deliberately picked one system drive is not a decision to
        // list them all from then on.
        var filterBox = findChild(step, "filterSystemDrives")
        verify(filterBox)
        filterBox.checked = false

        step.selectDstItem(drive({ isSystem: true, device: "/dev/sda" }))
        tryVerify(function () { return systemDialog().opened }, 3000)
        systemDialog().confirmed()

        verify(filterBox.checked, "system drives are hidden again")
    }
}
