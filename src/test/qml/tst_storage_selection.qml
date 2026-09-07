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

    // The step treats a refusal as nothing to do, so "nothing was selected"
    // holds whether the dialog reported a refusal or simply went quiet. What
    // the dialog said is watched separately.
    SignalSpy {
        id: refused
        signalName: "cancelled"
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

    // ── The gate on the confirmation itself ───────────────────────────
    //
    // Every case above emits confirmed() or cancelled() straight at the
    // dialog, which says what the step does with the answer and nothing
    // about how the dialog decides. The buttons, the escape key and the
    // Enter key in the name box were all uncovered.
    //
    // That is the gap that matters most in this application. The dialog is
    // what stands between a mis-click and the disk the user's computer
    // boots from, and the whole of its strength is that CONTINUE stays
    // disabled until the drive's name has been typed out. If that condition
    // ever came loose, every case above would still pass -- the dialog
    // would open, the user would press the only red button in front of
    // them, and their system disk would be overwritten.

    function systemChild(name) {
        var c = findChild(systemDialog(), name)
        verify(c, "found " + name)
        return c
    }

    function askAbout(overrides) {
        var d = drive(overrides)
        d.isSystem = true
        step.selectDstItem(d)
        tryVerify(function () { return systemDialog().opened }, 3000,
                  "the confirmation was raised")
        refused.target = systemDialog()
        refused.clear()
        return d
    }

    function test_continue_is_refused_until_the_name_is_typed() {
        askAbout({ description: "The disk this computer runs from",
                   device: "/dev/sda" })
        var go = systemChild("systemDriveContinueButton")
        verify(!go.enabled, "nothing typed yet")

        systemChild("systemDriveNameInput").text = "The disk this computer"
        verify(!go.enabled, "half of the name is not the name")

        systemChild("systemDriveNameInput").text = "The disk this computer runs from"
        verify(go.enabled, "typed out in full, so the user may proceed")
    }

    function test_what_does_not_count_as_the_name_data() {
        return [
            { tag: "nothing at all",   typed: "" },
            { tag: "something else",   typed: "/dev/sdb" },
            { tag: "the wrong case",   typed: "SDA SYSTEM DISK" },
            { tag: "a trailing space", typed: "sda system disk " },
            { tag: "a leading space",  typed: " sda system disk" },
            { tag: "only the start",   typed: "sda" },
            { tag: "a superset",       typed: "sda system disk 2" }
        ]
    }

    function test_what_does_not_count_as_the_name(data) {
        // Exactly, or not at all. A user who cannot be bothered to type it
        // is a user who has not read what they are agreeing to, and a
        // near-match is how a drive gets confirmed by autocomplete.
        askAbout({ description: "sda system disk", device: "/dev/sda" })

        systemChild("systemDriveNameInput").text = data.typed

        verify(!systemChild("systemDriveContinueButton").enabled, data.tag)
    }

    function test_a_drive_with_no_name_cannot_be_confirmed_by_an_empty_box() {
        // The name is the drive's description, and nothing guarantees there
        // is one. With an empty name an empty box matches it, so the second
        // half of the condition -- that there is a name at all -- is the
        // only thing between an unnamed system drive and a dialog that
        // opens with its red button already armed.
        askAbout({ description: "", device: "/dev/sda" })

        compare(systemChild("systemDriveNameInput").text, "",
                "the box starts empty")
        verify(!systemChild("systemDriveContinueButton").enabled,
               "and an empty box does not match an empty name")
    }

    function test_pressing_continue_without_the_name_chooses_nothing() {
        // The button is drawn disabled; it also refuses when asked
        // directly, which is what the Enter key in the name box goes
        // through.
        askAbout({ description: "The disk this computer runs from",
                   device: "/dev/sda" })

        systemChild("systemDriveContinueButton").clicked()

        compare(containerStub.selectedStorageName, "")
        verify(!step.nextButtonEnabled)
        compare(advanced.count, 0)
        verify(systemDialog().opened, "and the question is still on screen")
    }

    function test_enter_in_the_name_box_confirms_once_it_matches() {
        // Typing the name and pressing return is the natural way through.
        askAbout({ description: "The disk this computer runs from",
                   device: "/dev/sda" })
        var box = systemChild("systemDriveNameInput")
        box.text = "The disk this computer runs from"
        box.forceActiveFocus()

        keyClick(Qt.Key_Return)

        compare(containerStub.selectedStorageName,
                "The disk this computer runs from")
        verify(step.nextButtonEnabled)
    }

    function test_enter_with_a_partial_name_does_nothing() {
        askAbout({ description: "The disk this computer runs from",
                   device: "/dev/sda" })
        var box = systemChild("systemDriveNameInput")
        box.text = "The disk"
        box.forceActiveFocus()

        keyClick(Qt.Key_Return)

        compare(containerStub.selectedStorageName, "")
        verify(systemDialog().opened, "the dialog stays up")
    }

    function test_pressing_continue_with_the_name_chooses_the_drive() {
        askAbout({ description: "The disk this computer runs from",
                   device: "/dev/sda" })
        systemChild("systemDriveNameInput").text =
                "The disk this computer runs from"

        systemChild("systemDriveContinueButton").clicked()

        compare(containerStub.selectedStorageName,
                "The disk this computer runs from")
        verify(step.nextButtonEnabled)
        tryVerify(function () { return advanced.count === 1 }, 3000,
                  "and it advanced")
    }

    function test_pressing_cancel_chooses_nothing() {
        askAbout({ description: "The disk this computer runs from",
                   device: "/dev/sda" })
        // Even with the name typed out: the user changed their mind, and
        // the name in the box is not the decision.
        systemChild("systemDriveNameInput").text =
                "The disk this computer runs from"

        systemChild("systemDriveCancelButton").clicked()

        compare(containerStub.selectedStorageName, "")
        verify(!step.nextButtonEnabled)
        compare(advanced.count, 0)
        tryVerify(function () { return !systemDialog().visible }, 3000)
        compare(refused.count, 1,
                "and the refusal was reported rather than the dialog just "
                + "going quiet")
    }

    function test_escape_chooses_nothing() {
        askAbout({ description: "The disk this computer runs from",
                   device: "/dev/sda" })
        systemChild("systemDriveNameInput").text =
                "The disk this computer runs from"

        systemDialog().escapePressed()

        compare(containerStub.selectedStorageName, "")
        verify(!step.nextButtonEnabled)
        compare(advanced.count, 0)
        compare(refused.count, 1, "and it was reported as a refusal")
    }

    function test_asking_again_starts_from_an_empty_box() {
        // The worst version of not clearing it: the user backs out, thinks
        // better of it, clicks the same drive again -- and the dialog comes
        // up with the name already typed and CONTINUE already armed, one
        // stray click from erasing the disk they just declined.
        askAbout({ description: "The disk this computer runs from",
                   device: "/dev/sda" })
        systemChild("systemDriveNameInput").text =
                "The disk this computer runs from"
        verify(systemChild("systemDriveContinueButton").enabled, "armed")
        systemChild("systemDriveCancelButton").clicked()
        tryVerify(function () { return !systemDialog().visible }, 3000)

        askAbout({ description: "The disk this computer runs from",
                   device: "/dev/sda" })

        compare(systemChild("systemDriveNameInput").text, "",
                "the box was cleared")
        verify(!systemChild("systemDriveContinueButton").enabled,
               "so the drive has to be named again")
    }
}
