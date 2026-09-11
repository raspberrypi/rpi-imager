/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * "Skip customisation": the button that has to leave nothing behind.
 *
 * Every customisation step carries the same button, labelled "Skip
 * customisation" -- not "skip this step" -- and it jumps straight to the
 * writing step. So whichever screen it is pressed on, the answer has to be
 * the same: nothing the user had configured is still configured, and the
 * summary shown before the card is erased says so.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "SkipCustomisation"
    when: windowShown
    width: 800
    height: 600
    visible: true

    Component {
        id: containerComponent
        WizardContainer {}
    }

    property var wiz: null

    function initTestCase() {
        wiz = containerComponent.createObject(testCase)
        verify(wiz, "the wizard container was created")
        // main.qml supplies this in the application; the steps parent their
        // dialogs onto it, and without it those assignments fail silently.
        wiz.overlayRootRef = testCase
    }

    function cleanupTestCase() {
        if (wiz) {
            wiz.destroy()
            wiz = null
        }
    }

    // Every flag that means "the user configured something". The availability
    // flags next to them on the container are what the chosen OS supports,
    // not what the user asked for, so skipping must not touch those.
    readonly property var configuredFlags: [
        "hostnameConfigured", "localeConfigured", "userConfigured",
        "wifiConfigured", "sshEnabled", "secureBootEnabled",
        "piConnectEnabled",
        "ifI2cEnabled", "ifSpiEnabled", "if1WireEnabled",
        "featUsbGadgetEnabled"
    ]

    function configureEverything() {
        for (let i = 0; i < configuredFlags.length; i++)
            wiz[configuredFlags[i]] = true
        // ifSerial is a string rather than a flag; anything but "" and
        // "Disabled" counts as configured.
        wiz.ifSerial = "Console"
    }

    function stillConfigured() {
        const left = []
        for (let i = 0; i < configuredFlags.length; i++)
            if (wiz[configuredFlags[i]])
                left.push(configuredFlags[i])
        if (wiz.ifSerial !== "" && wiz.ifSerial !== "Disabled")
            left.push("ifSerial=" + wiz.ifSerial)
        return left
    }

    // The step currently on the stack, so its skipClicked can be raised the
    // way the button raises it. The stack is an id inside the container rather
    // than an exposed property, so it is found by walking the object tree.
    function findStack(item) {
        if (!item)
            return null
        if (item.currentItem !== undefined && item.depth !== undefined)
            return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findStack(kids[i])
            if (found)
                return found
        }
        return null
    }

    function currentStepItem() {
        const stack = findStack(wiz)
        verify(stack !== null, "the container has a step stack")
        return stack.currentItem
    }

    function init() {
        // Every step reachable, as it would be for someone who has been
        // through the wizard and is going back over it.
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
        wiz.customizationSupported = true
        wiz.selectedDeviceName = "Raspberry Pi 5"
        wiz.selectedOsName = "Raspberry Pi OS (64-bit)"
        wiz.selectedStorageName = "Generic Mass-Storage 32 GB"
        wiz.ifSerial = ""
        for (let j = 0; j < configuredFlags.length; j++)
            wiz[configuredFlags[j]] = false
    }

    // -- The property, on every step that offers the button ----------------

    function test_skipping_leaves_nothing_configured_data() {
        return [
            { tag: "hostname",   step: wiz.stepHostnameCustomization },
            { tag: "locale",     step: wiz.stepLocaleCustomization },
            { tag: "user",       step: wiz.stepUserCustomization },
            { tag: "wifi",       step: wiz.stepWifiCustomization },
            { tag: "remote",     step: wiz.stepRemoteAccess },
            { tag: "secureboot", step: wiz.stepSecureBootCustomization },
            { tag: "piconnect",  step: wiz.stepPiConnectCustomization },
            { tag: "interfaces", step: wiz.stepIfAndFeatures },
        ]
    }

    function test_skipping_leaves_nothing_configured(data) {
        wiz.jumpToStep(data.step)
        compare(wiz.currentStep, data.step, "the step under test is showing")

        configureEverything()
        verify(stillConfigured().length > 0, "something is configured to skip")

        const item = currentStepItem()
        verify(item !== null, "the step is on the stack")
        item.skipClicked()

        const left = stillConfigured()
        compare(left.length, 0,
                "pressing Skip customisation on the " + data.tag
                + " step left these configured: " + left.join(", ")
                + " -- the writing step's summary reads the live flags, so it "
                + "would list them on a card the user chose not to customise")
    }

    function test_skipping_goes_to_the_writing_step_data() {
        return test_skipping_leaves_nothing_configured_data()
    }

    function test_skipping_goes_to_the_writing_step(data) {
        wiz.jumpToStep(data.step)
        const item = currentStepItem()
        verify(item !== null)

        item.skipClicked()

        compare(wiz.currentStep, wiz.stepWriting,
                "Skip customisation goes straight to the write, from the "
                + data.tag + " step as from any other")
    }

    // -- What skipping must not do -----------------------------------------

    function test_skipping_does_not_forget_the_board_the_image_or_the_card() {
        // It skips customisation, not the wizard. Clearing any of these would
        // send the user back to the start, or worse, write with no target.
        wiz.jumpToStep(wiz.stepWifiCustomization)
        configureEverything()

        currentStepItem().skipClicked()

        compare(wiz.selectedDeviceName, "Raspberry Pi 5", "the board is kept")
        compare(wiz.selectedOsName, "Raspberry Pi OS (64-bit)", "and the image")
        compare(wiz.selectedStorageName, "Generic Mass-Storage 32 GB",
                "and the card it is going to")
    }

    function test_skipping_does_not_change_what_the_os_supports() {
        // The availability flags describe the chosen OS. Clearing them on a
        // skip would make the wizard think the OS supports nothing, and the
        // steps would stay hidden if the user came back.
        wiz.jumpToStep(wiz.stepWifiCustomization)
        wiz.piConnectAvailable = true
        wiz.ccRpiAvailable = true
        wiz.customizationSupported = true

        currentStepItem().skipClicked()

        verify(wiz.piConnectAvailable,
               "what the OS supports is not the user's choice to skip")
        verify(wiz.ccRpiAvailable)
        verify(wiz.customizationSupported)
    }

    // -- Clearing a string flag with a boolean -----------------------------

    function test_a_board_without_interface_support_reports_no_serial_console() {
        // The interfaces step skips itself when the chosen OS supports none of
        // them, clearing the flags on the way past. ifSerial is a string, and
        // it was being cleared with a boolean: false became the string
        // "false", which is neither "" nor "Disabled" -- the two values every
        // reader treats as unconfigured. So the sidebar marked the step done,
        // and both the pre-erase summary and the completion screen listed a
        // serial console the user had never been offered.
        //
        // The step calls nextStep() on that path, which is what captured the
        // wrong value into the snapshot the completion screen reads.
        wiz.ccRpiAvailable = false
        wiz.ifSerial = "Console"

        wiz.jumpToStep(wiz.stepIfAndFeatures)

        verify(wiz.ifSerial === "" || wiz.ifSerial === "Disabled",
               "a board with no interface support leaves no serial console "
               + "configured, but ifSerial is " + JSON.stringify(wiz.ifSerial))

        const snapshot = wiz.completionSnapshot
        if (snapshot !== undefined && snapshot !== null
                && snapshot.ifSerial !== undefined)
            verify(snapshot.ifSerial === "" || snapshot.ifSerial === "Disabled",
                   "and the completion screen is told the same, not "
                   + JSON.stringify(snapshot.ifSerial))
    }

    // -- And the two summaries agree ---------------------------------------

    function test_after_skipping_neither_summary_claims_a_customisation() {
        // WritingStep reads the live flags; DoneStep reads the snapshot taken
        // on the way into the writing step. After a skip both have to come out
        // empty, or the card is described differently on the two screens.
        wiz.jumpToStep(wiz.stepIfAndFeatures)
        configureEverything()

        currentStepItem().skipClicked()

        compare(stillConfigured().length, 0,
                "nothing left for the writing step's summary to list")

        const snapshot = wiz.completionSnapshot
        verify(snapshot !== undefined && snapshot !== null,
               "there is a snapshot for the completion screen to read")
        const claimed = []
        for (let i = 0; i < configuredFlags.length; i++)
            if (snapshot[configuredFlags[i]])
                claimed.push(configuredFlags[i])
        if (snapshot.ifSerial !== undefined && snapshot.ifSerial !== ""
                && snapshot.ifSerial !== "Disabled")
            claimed.push("ifSerial=" + snapshot.ifSerial)
        compare(claimed.length, 0,
                "and nothing for the completion screen either, which would "
                + "otherwise disagree with it: " + claimed.join(", "))
    }
}
