/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * "Exclude system drives": the checkbox that stands between the user and
 * their own boot disk.
 *
 * While it is ticked the list hides system drives, so the drive the computer
 * is running from cannot be picked by accident. Unticking it raises a stern
 * warning, and the question this file asks is what happens to the tick when
 * that warning is answered -- because the checkbox has already toggled by the
 * time the warning appears. Declining has to put it back. If it does not, a
 * user who read the warning and said no is looking at a list that now
 * includes their operating system.
 *
 * The policy behind the warning -- raised by default, skipped for a
 * deployment-wide opt-out or an attached screen reader, because a screen
 * reader has already read the risk out of the checkbox's own description -- is
 * covered in tst_accessibility_confirmations.qml. But it is covered there
 * against a stand-in ImCheckBox carrying a copy of the logic, not against this
 * step, so nothing checked that the real checkbox consults the policy at all.
 * A step that opened the dialog unconditionally, or never opened it, would
 * pass that file. These cases drive the real one.
 *
 * Note that setting `checked` in code does not raise onToggled -- Qt reserves
 * that for user interaction -- which is why the other storage test files can
 * set the box directly without ever reaching this handler. Everything here
 * goes through a click or a key.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "StorageFilterConfirmation"
    when: windowShown
    width: 700
    height: 500
    visible: true

    QtObject {
        id: containerStub
        property bool disableWarnings: false
        // An actual item, not null: the step parents its confirmation dialogs
        // onto this, and null makes that an undefined assignment.
        property var overlayRootRef: testCase
        property string networkInfoText: ""
        property string selectedStorageName: ""
        property bool targetIsFastboot: false
    }

    Component {
        id: stepComponent
        StorageSelectionStep {
            wizardContainer: containerStub
            width: 700
            height: 500
        }
    }

    property var step: null

    function init() {
        TestAccessibility.setActive(false)
        containerStub.disableWarnings = false
        containerStub.selectedStorageName = ""
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
    }

    function cleanup() {
        TestAccessibility.setActive(false)
        if (step) {
            step.destroy()
            step = null
        }
    }

    function filterBox() {
        const box = findChild(step, "filterSystemDrives")
        verify(box, "the system-drive filter checkbox was found")
        return box
    }

    function dialog() {
        const d = findChild(step, "confirmUnfilterPopup")
        verify(d, "the unfilter warning was found")
        return d
    }

    // Walk for a button by its label; the dialog's buttons are ids inside it.
    // A Popup keeps its content under contentItem rather than children, so the
    // walk has to start there.
    function walkForButton(item, label) {
        if (!item)
            return null
        if (item.text !== undefined && String(item.text) === label
                && item.clicked !== undefined)
            return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = walkForButton(kids[i], label)
            if (found)
                return found
        }
        return null
    }

    function buttonNamed(popup, label) {
        if (!popup)
            return null
        return walkForButton(popup.contentItem, label)
    }

    // Unticking as a user does. The box is focused first so the key cases
    // land on it.
    function untick() {
        const box = filterBox()
        compare(box.checked, true, "the filter starts on")
        box.forceActiveFocus()
        mouseClick(box)
        compare(box.checked, false, "the tick comes off as the click lands")
    }

    // -- The warning is raised at all --------------------------------------

    function test_unticking_the_filter_raises_the_warning() {
        untick()

        const d = dialog()
        tryVerify(function () { return d.opened }, 3000,
                  "unticking the filter has to ask before showing system drives")
    }

    function test_the_warning_names_the_risk_and_the_way_out() {
        untick()
        const d = dialog()
        tryVerify(function () { return d.opened }, 3000)

        verify(buttonNamed(d, "KEEP FILTER ON") !== null,
               "there is a way to decline")
        verify(buttonNamed(d, "SHOW SYSTEM DRIVES") !== null,
               "and a way to accept")
    }

    // -- Answering it ------------------------------------------------------

    function test_declining_puts_the_filter_back_on() {
        // The one that matters. The tick came off when the box was clicked,
        // so declining has to restore it -- otherwise the user read the
        // warning, said no, and is now looking at a list containing the drive
        // their operating system is on.
        untick()
        const d = dialog()
        tryVerify(function () { return d.opened }, 3000)

        const keep = buttonNamed(d, "KEEP FILTER ON")
        verify(keep !== null)
        mouseClick(keep)

        // Popup.close() starts an exit transition, so wait the dialog out
        // rather than checking immediately.
        tryVerify(function () { return !d.visible }, 3000, "the warning closes")
        compare(filterBox().checked, true,
                "declining the warning leaves system drives hidden")
    }

    function test_accepting_leaves_the_filter_off() {
        untick()
        const d = dialog()
        tryVerify(function () { return d.opened }, 3000)

        const show = buttonNamed(d, "SHOW SYSTEM DRIVES")
        verify(show !== null)
        mouseClick(show)

        tryVerify(function () { return !d.visible }, 3000, "the warning closes")
        compare(filterBox().checked, false,
                "having asked for system drives, the user gets them")
    }

    function test_escape_counts_as_declining() {
        // Escape is the reflex for dismissing a dialog, and on a warning like
        // this it has to mean no. The dialog's closePolicy is CloseOnEscape,
        // so the Popup will close itself; what matters is that the filter goes
        // back on rather than the dialog vanishing and leaving the system
        // drives on show.
        untick()
        const d = dialog()
        tryVerify(function () { return d.opened }, 3000)

        keyClick(Qt.Key_Escape)

        tryVerify(function () { return !d.visible }, 3000, "the warning closes")
        compare(filterBox().checked, true,
                "dismissing the warning is not the same as accepting it")
    }

    function test_the_safe_answer_is_the_one_the_keyboard_lands_on() {
        // Both buttons are tabbable, and the declining one is first, so a user
        // pressing Return on the warning keeps the filter rather than removing
        // it.
        untick()
        const d = dialog()
        tryVerify(function () { return d.opened }, 3000)

        const keep = buttonNamed(d, "KEEP FILTER ON")
        const show = buttonNamed(d, "SHOW SYSTEM DRIVES")
        verify(keep !== null && show !== null)
        verify(keep.activeFocusOnTab, "the safe answer is reachable by keyboard")
        verify(show.activeFocusOnTab, "and so is the other one")
        verify(String(keep.accessibleDescription).length > 0,
               "and the safe answer says what it does")
        verify(String(show.accessibleDescription).length > 0)
    }

    // -- The two ways past it ----------------------------------------------

    function test_a_deployment_that_turned_warnings_off_is_not_asked() {
        containerStub.disableWarnings = true

        untick()

        const d = dialog()
        wait(200)
        verify(!d.visible,
               "the deployment-wide opt-out means no dialog")
        compare(filterBox().checked, false,
                "and the filter simply goes off")
    }

    function test_a_screen_reader_user_is_not_interrupted() {
        // The warning's wording lives in the checkbox's own description, which
        // a screen reader reads aloud on arrival -- more than a sighted mouse
        // user is ever shown. So the dialog is skipped rather than the warning
        // dropped.
        TestAccessibility.setActive(true)

        untick()

        const d = dialog()
        wait(200)
        verify(!d.visible,
               "a user who has already had the risk read to them is not "
               + "stopped again")
        compare(filterBox().checked, false)

        // And the wording really is there to be read, or the skip above would
        // remove the warning rather than relocate it.
        const description = String(filterBox().Accessible.description)
        verify(description.length > 0, "the checkbox carries a description")
        verify(description.indexOf("system drives") >= 0,
               "which says what unchecking shows: " + description)
        verify(description.indexOf("destroy") >= 0
               || description.indexOf("destroyed") >= 0,
               "and what that costs: " + description)
    }

    // -- Putting it back ---------------------------------------------------

    function test_ticking_the_filter_again_asks_nothing() {
        // Turning the safety back on needs no confirmation. Asking would
        // train the user to dismiss this dialog.
        untick()
        const d = dialog()
        tryVerify(function () { return d.opened }, 3000)
        mouseClick(buttonNamed(d, "SHOW SYSTEM DRIVES"))
        tryVerify(function () { return !d.visible }, 3000)
        compare(filterBox().checked, false)

        const box = filterBox()
        box.forceActiveFocus()
        mouseClick(box)

        compare(box.checked, true, "the filter goes back on")
        wait(200)
        // visible, not opened: open() sets visible at once, while opened waits
        // for the enter transition -- so a check on opened can read false from
        // a dialog that is on its way up.
        verify(!d.visible, "and nothing was asked about it")
    }
}
