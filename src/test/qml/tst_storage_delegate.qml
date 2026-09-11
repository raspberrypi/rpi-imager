/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Clicking a drive in the storage list.
 *
 * The other storage cases reach the step's selectDstItem() directly, handing
 * it a plain object standing in for a row. That covers what happens after a
 * drive is chosen; it says nothing about the choosing, and the three handlers
 * that do it -- the delegate's click, its double click, and the selectDrive()
 * the keyboard path calls -- were the last uncovered sites in the step.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "StorageDelegate"
    when: windowShown
    width: 800
    height: 600
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
        StorageSelectionStep {
            wizardContainer: containerStub
            anchors.fill: parent
        }
    }

    property var step: null

    SignalSpy {
        id: advanced
        signalName: "nextClicked"
    }

    function init() {
        TestDrives.clear()
        containerStub.selectedStorageName = ""
        containerStub.targetIsFastboot = false
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        advanced.target = step
        advanced.clear()
        waitForRendering(step)
    }

    function cleanup() {
        // The model belongs to the singleton writer and outlives the step, so
        // a case that left rows behind would seed the next one.
        TestDrives.clear()
        if (step) {
            step.destroy()
            step = null
        }
    }

    function list() {
        const l = findChild(step, "storageDeviceList")
        verify(l, "found the storage list")
        return l
    }

    // Put drives in the real model and wait for the delegates to exist.
    function populate(drives) {
        TestDrives.set(drives)
        const l = list()
        tryVerify(function() { return l.count === drives.length },
                  3000, "the list shows " + drives.length + " row(s), not " + l.count)
        waitForRendering(step)
        return l
    }

    function rowAt(l, index) {
        const item = l.itemAtIndex(index)
        verify(item, "row " + index + " has a delegate")
        // A hidden row is zero pixels high, and a click at its position lands
        // on whatever is underneath -- which is how a case asserting that
        // nothing happened passes while doing nothing. Checked here so every
        // case that takes a row gets the guarantee.
        verify(item.visible && item.height > 0,
               "row " + index + " is on screen to be clicked")
        return item
    }

    function card(overrides) {
        var d = {
            device: "/dev/sdz",
            description: "Generic Mass-Storage",
            size: 32000000000,
            isSystem: false,
            isReadOnly: false
        }
        for (var k in overrides) d[k] = overrides[k]
        return d
    }

    // -- The rows are really there -----------------------------------------

    function test_the_harness_fills_the_list_the_step_binds_to() {
        // Every case below is vacuous without this: an empty list has no
        // delegate to click, and clicking nothing passes quietly.
        const l = populate([card({ device: "/dev/sda", description: "First card" }),
                            card({ device: "/dev/sdb", description: "Second card" })])
        compare(l.count, 2)
        verify(rowAt(l, 0).width > 0, "the row has been laid out")
    }

    // -- Clicking one -------------------------------------------------------

    function test_clicking_a_drive_chooses_it() {
        const l = populate([card({ device: "/dev/sda", description: "SanDisk Cruzer" })])

        mouseClick(rowAt(l, 0))

        compare(containerStub.selectedStorageName, "SanDisk Cruzer",
                "clicking the row has to choose that drive")
        verify(step.nextButtonEnabled, "and let the user move on")
    }

    function test_clicking_the_second_drive_chooses_the_second_drive() {
        // The delegate passes its own index to the list and its own data to
        // the step. Both have to be the row that was clicked, not the row
        // that happens to be current.
        const l = populate([card({ device: "/dev/sda", description: "Top card" }),
                            card({ device: "/dev/sdb", description: "Bottom card" })])

        mouseClick(rowAt(l, 1))

        compare(containerStub.selectedStorageName, "Bottom card")
        compare(l.currentIndex, 1, "and the highlight moved with it")
    }

    function test_clicking_a_read_only_drive_chooses_nothing() {
        // A card with its lock tab across, or a device the system will not
        // let go of. The row is shown, dimmed, and must not respond.
        const l = populate([card({ device: "/dev/sdc", description: "Locked card",
                                   isReadOnly: true })])

        mouseClick(rowAt(l, 0))

        compare(containerStub.selectedStorageName, "",
                "a read-only drive cannot be the target")
        verify(!step.nextButtonEnabled, "so there is nothing to move on to")
    }

    function test_clicking_a_system_drive_asks_first() {
        // The one row where a click is a question rather than an answer: the
        // disk the machine is running from. Choosing it takes typing its name
        // into the dialog, which has its own cases; what matters here is that
        // the click alone does not select it.
        const l = populate([card({ device: "/dev/sda", description: "System disk",
                                   isSystem: true })])

        // System drives are hidden until the filter is turned off, and a
        // hidden row is zero pixels high -- a click at its position lands on
        // whatever is underneath, which is how the first draft of this case
        // passed while testing nothing. The checkbox is set directly rather
        // than clicked: going through the toggle raises the unfilter
        // confirmation, which belongs to its own file.
        const filter = findChild(step, "filterSystemDrives")
        verify(filter, "found the system-drive filter")
        filter.checked = false
        waitForRendering(step)

        mouseClick(rowAt(l, 0))

        compare(containerStub.selectedStorageName, "",
                "the click must not choose the system disk on its own")
        const dialog = findChild(step, "systemDriveConfirmDialog")
        verify(dialog, "found the confirmation")
        tryVerify(function() { return dialog.opened }, 3000,
                  "clicking a system drive has to raise the confirmation")
    }

    // -- Double-clicking ----------------------------------------------------

    function test_double_clicking_a_drive_chooses_it_and_moves_on() {
        // The shortcut for people who know which card they want. It has to do
        // both halves: a double click that advances without selecting would
        // carry an unset target into the next step.
        const l = populate([card({ device: "/dev/sda", description: "Quick card" })])

        mouseDoubleClickSequence(rowAt(l, 0))

        compare(containerStub.selectedStorageName, "Quick card", "it chose the drive")
        tryVerify(function() { return advanced.count === 1 }, 3000,
                  "and advanced to the next step")
    }

    function test_double_clicking_a_read_only_drive_does_neither() {
        const l = populate([card({ device: "/dev/sdc", description: "Locked card",
                                   isReadOnly: true })])

        mouseDoubleClickSequence(rowAt(l, 0))

        compare(containerStub.selectedStorageName, "")
        compare(advanced.count, 0, "and it did not skip past the empty choice")
    }

    // -- Choosing from the keyboard ----------------------------------------

    function test_choosing_with_the_keyboard_goes_through_the_delegate() {
        // The list hands the delegate back to itself and calls selectDrive()
        // on it, so a keyboard user and a mouse user take different routes to
        // the same choice. Both have to arrive.
        const l = populate([card({ device: "/dev/sda", description: "First card" }),
                            card({ device: "/dev/sdb", description: "Second card" })])

        l.forceActiveFocus()
        l.currentIndex = 1
        waitForRendering(step)
        keyClick(Qt.Key_Space)

        tryVerify(function() {
            return containerStub.selectedStorageName === "Second card"
        }, 3000, "the keyboard chose the current row; got '"
                 + containerStub.selectedStorageName + "'")
    }

    function test_the_keyboard_will_not_choose_a_read_only_drive() {
        const l = populate([card({ device: "/dev/sdc", description: "Locked card",
                                   isReadOnly: true })])

        l.forceActiveFocus()
        l.currentIndex = 0
        waitForRendering(step)
        keyClick(Qt.Key_Space)

        // Given a moment to be wrong in.
        wait(150)
        compare(containerStub.selectedStorageName, "",
                "the refusal cannot depend on the mouse being the one asking")
    }
}
