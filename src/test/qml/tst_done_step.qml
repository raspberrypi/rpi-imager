/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * DoneStep: the last screen, and the one place the wizard offers to write a
 * second card.
 *
 * The step was at 0% -- nothing had ever instantiated it -- so none of what
 * it tells the user at the end of a write was checked, and neither was the
 * one button that sends them round again. "Write Another" is the interesting
 * one: it has to keep the board, the image and the customisations, and drop
 * the card, because the card it just finished with is the one being taken
 * out. resetToWriteStep is tested on the container side already; what was
 * missing is that this button is wired to it at all.
 *
 * Two of the step's handlers are deliberately not exercised here. The finish
 * button calls Qt.quit() outside embedded builds, and embedded mode is a
 * compile-time constant, so clicking it in a desktop test build would end the
 * test run rather than assert anything. The eject button calls
 * ImageWriterSingleton.ejectDrive(), which acts on a real device, and
 * ejectState is private with no setter reachable from QML, so neither the
 * call nor the state it produces can be staged. What can be checked without
 * either is that the buttons say the right thing and that the step reports
 * the write honestly, which is what a user reads.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "DoneStep"
    when: windowShown
    width: 900
    height: 700
    visible: true

    // The snapshot is taken when the write completes and then held, because
    // the live customisation flags are cleared for security as soon as the
    // image is written. Everything the summary shows comes from here rather
    // than from the container's current state.
    QtObject {
        id: fakeSnapshot
        property bool customizationSupported: true
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property bool piConnectEnabled: false
        property bool ifI2cEnabled: false
        property bool ifSpiEnabled: false
        property bool if1WireEnabled: false
        property string ifSerial: ""
        property bool featUsbGadgetEnabled: false
    }

    QtObject {
        id: fakeContainer
        property string selectedDeviceName: "Raspberry Pi 5"
        property string selectedOsName: "Raspberry Pi OS (64-bit)"
        property string selectedStorageName: "Generic Mass-Storage 32 GB"
        // WizardStepBase reads this on every step; undefined assigns nothing
        // and warns on each construction.
        property string networkInfoText: ""
        property var completionSnapshot: fakeSnapshot

        property int resetToWriteStepCalls: 0
        function resetToWriteStep() { resetToWriteStepCalls++ }
    }

    Component {
        id: stepComponent
        DoneStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    function resetSnapshot() {
        fakeSnapshot.customizationSupported = true
        fakeSnapshot.hostnameConfigured = false
        fakeSnapshot.localeConfigured = false
        fakeSnapshot.userConfigured = false
        fakeSnapshot.wifiConfigured = false
        fakeSnapshot.sshEnabled = false
        fakeSnapshot.piConnectEnabled = false
        fakeSnapshot.ifI2cEnabled = false
        fakeSnapshot.ifSpiEnabled = false
        fakeSnapshot.if1WireEnabled = false
        fakeSnapshot.ifSerial = ""
        fakeSnapshot.featUsbGadgetEnabled = false
    }

    function init() {
        resetSnapshot()
        fakeContainer.resetToWriteStepCalls = 0
        fakeContainer.selectedDeviceName = "Raspberry Pi 5"
        fakeContainer.selectedOsName = "Raspberry Pi OS (64-bit)"
        fakeContainer.selectedStorageName = "Generic Mass-Storage 32 GB"
    }

    function cleanup() {
        step = null
    }

    function make() {
        step = createTemporaryObject(stepComponent, testCase)
        verify(step !== null, "the step has to instantiate")
        return step
    }

    function findChild(root, objectName) {
        if (!root)
            return null
        if (root.objectName === objectName)
            return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findChild(kids[i], objectName)
            if (found)
                return found
        }
        return null
    }

    // Walk the whole object tree, not just visual children, so buttons that
    // live in a content or footer list are reachable.
    function findByText(root, wanted) {
        if (!root)
            return null
        if (root.text !== undefined && String(root.text) === wanted)
            return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findByText(kids[i], wanted)
            if (found)
                return found
        }
        return null
    }

    // -- The step comes up at all ------------------------------------------

    function test_the_last_screen_says_the_write_finished() {
        const s = make()

        compare(s.title, "Write complete!",
                "the heading is the whole point of the screen")
        // There is nowhere to go forward to and nothing to go back to.
        verify(!s.showNextButton, "no next step from the end")
        verify(!s.showBackButton, "and no going back into a finished write")
    }

    function test_the_summary_names_what_was_written_and_where() {
        const s = make()

        // Each of the three is shown from the container, and each falls back
        // to a "nothing selected" string rather than to an empty row -- a
        // blank line here would read as though the write had no target.
        verify(findByText(s, "Raspberry Pi 5") !== null,
               "the board is named")
        verify(findByText(s, "Raspberry Pi OS (64-bit)") !== null,
               "so is the image")
        verify(findByText(s, "Generic Mass-Storage 32 GB") !== null,
               "and the card it went to")
    }

    function test_a_write_with_nothing_chosen_still_reads_as_a_sentence() {
        fakeContainer.selectedDeviceName = ""
        fakeContainer.selectedOsName = ""
        fakeContainer.selectedStorageName = ""

        const s = make()

        verify(findByText(s, CommonStrings.noDeviceSelected) !== null,
               "an empty board falls back rather than showing a blank row")
        verify(findByText(s, CommonStrings.noImageSelected) !== null)
        verify(findByText(s, CommonStrings.noStorageSelected) !== null)
    }

    // -- What the summary claims was customised ----------------------------
    //
    // The snapshot decides whether the customisation section appears at all.
    // Getting this wrong in the quiet direction is the one that matters: a
    // card written with SSH enabled and a user account set, reported as
    // having no customisations, tells the user their password was not
    // applied when it was.

    function test_a_plain_write_claims_no_customisations() {
        const s = make()

        verify(!s.anyCustomizationsApplied,
               "nothing was configured, so nothing is claimed")
    }

    function test_each_customisation_on_its_own_is_reported() {
        const flags = ["hostnameConfigured", "localeConfigured",
                       "userConfigured", "wifiConfigured", "sshEnabled",
                       "piConnectEnabled", "ifI2cEnabled", "ifSpiEnabled",
                       "if1WireEnabled", "featUsbGadgetEnabled"]

        for (let i = 0; i < flags.length; i++) {
            resetSnapshot()
            fakeSnapshot[flags[i]] = true

            const s = createTemporaryObject(stepComponent, testCase)
            verify(s !== null)
            verify(s.anyCustomizationsApplied,
                   flags[i] + " alone has to count as a customisation")
            s.destroy()
        }
    }

    function test_a_configured_serial_port_counts_but_a_disabled_one_does_not() {
        // ifSerial is a string rather than a flag, and "Disabled" is a value
        // it genuinely takes -- so an emptiness check alone would report a
        // customisation for a user who turned the serial port off.
        resetSnapshot()
        fakeSnapshot.ifSerial = "Disabled"
        let s = createTemporaryObject(stepComponent, testCase)
        verify(s !== null)
        verify(!s.anyCustomizationsApplied,
               "turning the serial port off is not a customisation")
        s.destroy()

        resetSnapshot()
        fakeSnapshot.ifSerial = "Console"
        s = createTemporaryObject(stepComponent, testCase)
        verify(s !== null)
        verify(s.anyCustomizationsApplied, "but configuring it is")
        s.destroy()
    }

    function test_an_image_that_takes_no_customisation_claims_none() {
        // With customisation unsupported the flags are meaningless, and
        // several of them are left set from an earlier screen. Reporting
        // them would tell the user settings had been written to an image
        // that cannot carry them.
        resetSnapshot()
        fakeSnapshot.customizationSupported = false
        fakeSnapshot.hostnameConfigured = true
        fakeSnapshot.sshEnabled = true
        fakeSnapshot.userConfigured = true

        const s = make()

        verify(!s.anyCustomizationsApplied,
               "an image that cannot be customised was not customised")
    }

    // -- Writing a second card ---------------------------------------------

    function test_write_another_sends_the_user_back_for_a_new_card() {
        const s = make()

        const button = findByText(s, "Write Another")
        verify(button !== null, "the button has to be on the screen")
        verify(button.enabled, "and be usable once the write is done")

        mouseClick(button)

        compare(fakeContainer.resetToWriteStepCalls, 1,
                "the button has to go through resetToWriteStep, which is what "
                + "keeps the board and image and drops the finished card")
    }

    function test_write_another_is_described_for_someone_who_cannot_see_it() {
        const s = make()

        const button = findByText(s, "Write Another")
        verify(button !== null)
        verify(String(button.accessibleDescription).length > 0,
               "a button that sends the user round the wizard again needs to "
               + "say so to a screen reader")
        verify(button.activeFocusOnTab,
               "and be reachable without a mouse")
    }

    function test_the_finish_button_is_offered_and_reachable() {
        // Not clicked: outside embedded builds this calls Qt.quit(), which
        // would take the test run with it. What is checkable is that it is
        // there, labelled, enabled and tabbable -- a finish button the
        // keyboard cannot reach leaves a user with no way out of the wizard.
        const s = make()

        const button = findByText(s, CommonStrings.finish)
        verify(button !== null, "there has to be a way to finish")
        verify(button.enabled, "and it works once the write is done")
        verify(button.activeFocusOnTab, "and the keyboard can reach it")
        verify(String(button.accessibleDescription).length > 0,
               "and it says what it will do")
    }

    // -- Reading a long list of customisations without a mouse -------------
    //
    // The customisation summary scrolls when there are more entries than fit.
    // It is a Flickable with explicit Up and Down handlers, because a
    // Flickable on its own does not respond to the arrow keys -- so a
    // keyboard user faced with a clipped list would have no way to see the
    // rest of it.

    function test_the_customisation_list_scrolls_with_the_arrow_keys() {
        // Enough entries that the list is taller than the space for it.
        resetSnapshot()
        fakeSnapshot.hostnameConfigured = true
        fakeSnapshot.localeConfigured = true
        fakeSnapshot.userConfigured = true
        fakeSnapshot.wifiConfigured = true
        fakeSnapshot.sshEnabled = true
        fakeSnapshot.piConnectEnabled = true
        fakeSnapshot.ifI2cEnabled = true
        fakeSnapshot.ifSpiEnabled = true
        fakeSnapshot.if1WireEnabled = true
        fakeSnapshot.featUsbGadgetEnabled = true

        const s = createTemporaryObject(stepComponent, testCase)
        verify(s !== null)
        verify(s.anyCustomizationsApplied)

        const flickable = findFlickable(s)
        verify(flickable !== null, "the summary is in a Flickable")

        // Make it definitely scrollable, whatever the layout worked out to,
        // so the handlers have somewhere to move to.
        flickable.height = 40
        flickable.forceActiveFocus()
        verify(flickable.contentHeight > flickable.height,
               "the list has to overflow for scrolling to mean anything")

        compare(flickable.contentY, 0, "it starts at the top")

        keyClick(Qt.Key_Down)
        verify(flickable.contentY > 0,
               "Down moves through the list rather than doing nothing")

        const afterDown = flickable.contentY
        keyClick(Qt.Key_Up)
        verify(flickable.contentY < afterDown, "and Up comes back")
    }

    function test_arrow_keys_do_not_scroll_past_either_end() {
        resetSnapshot()
        fakeSnapshot.hostnameConfigured = true
        fakeSnapshot.sshEnabled = true

        const s = createTemporaryObject(stepComponent, testCase)
        verify(s !== null)

        const flickable = findFlickable(s)
        verify(flickable !== null)
        flickable.height = 40
        flickable.forceActiveFocus()

        // Already at the top: Up must not take it negative, which would show
        // a gap above the first entry.
        keyClick(Qt.Key_Up)
        verify(flickable.contentY >= 0, "Up at the top stays at the top")

        // And Down repeated must stop at the last entry rather than scroll
        // the list off the screen.
        const maxY = Math.max(0, flickable.contentHeight - flickable.height)
        for (let i = 0; i < 40; i++)
            keyClick(Qt.Key_Down)
        verify(flickable.contentY <= maxY,
               "Down stops at the end of the list, not past it")
    }

    function findFlickable(root) {
        if (!root)
            return null
        if (root.contentY !== undefined && root.contentHeight !== undefined
                && root.flickableDirection !== undefined)
            return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findFlickable(kids[i])
            if (found)
                return found
        }
        return null
    }
}
