/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Raspberry Pi Connect: the two credentials this step handles, and what it
 * takes to be rid of one.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "PiConnectCredentials"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property Item overlayRootRef: testCase
        property var customizationSettings: ({})
        property bool piConnectAvailable: true
        property bool piConnectEnabled: false
        property bool targetIsFastboot: false
        property string connectOrgDescription: ""
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property string networkInfoText: ""
        property int stepWriting: 9
        property int jumpedTo: -1
        function jumpToStep(n) { jumpedTo = n }
        function skipAllCustomisation() { jumpToStep(stepWriting) }
    }

    Component {
        id: stepComponent
        PiConnectCustomizationStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    // hasStoredOrgKey and orgModeEnabled are read once when the step is
    // built, not bound to anything that notifies, so the settings they read
    // have to be in place first.
    function build(settings) {
        for (const key in settings)
            ImageWriterSingleton.setSetting(key, settings[key])
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        return step
    }

    function init() {
        fakeContainer.connectOrgDescription = ""
        fakeContainer.piConnectEnabled = false
        fakeContainer.customizationSettings = ({})
        ImageWriterSingleton.setSetting("connect_org_enabled", false)
        ImageWriterSingleton.clearConnectOrgRegistration()
    }

    function cleanup() {
        if (step) {
            step.destroy()
            step = null
        }
        ImageWriterSingleton.setSetting("connect_org_enabled", false)
        ImageWriterSingleton.clearConnectOrgRegistration()
    }

    function child(name) {
        const c = findChild(step, name)
        verify(c, "found " + name)
        return c
    }

    function findByText(item, label) {
        if (!item)
            return null
        if (item.text !== undefined && String(item.text) === label
                && item.clicked !== undefined)
            return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findByText(kids[i], label)
            if (found)
                return found
        }
        return null
    }

    // -- Being rid of a saved organisation key -----------------------------

    function test_a_saved_key_is_reported_as_stored_without_being_shown() {
        build({ "connect_org_enabled": true,
                "connect_org_api_key": "org-key-that-must-not-be-echoed",
                "connect_org_description": "Test org" })

        verify(step.orgModeEnabled, "the step is in organisation mode")
        verify(step.hasStoredOrgKey, "and knows a key is held")

        // The key itself must not come back to the screen. Only the
        // description does, which is not a secret.
        verify(findByText(step, "org-key-that-must-not-be-echoed") === null,
               "a saved key is never re-shown")
    }

    function test_clearing_a_saved_key_actually_removes_it() {
        // The user's only evidence the key is stored is a placeholder, so if
        // this button clears the fields and leaves the setting, they are told
        // it is gone while it is still on disk and still going out with the
        // next write.
        build({ "connect_org_enabled": true,
                "connect_org_api_key": "org-key-to-remove",
                "connect_org_description": "Test org" })
        verify(ImageWriterSingleton.hasConnectOrgRegistration(),
               "there is a registration to clear")

        const clear = findByText(step, "Clear saved key")
        verify(clear !== null, "the clear button is offered")
        verify(clear.visible, "and is on the screen")
        mouseClick(clear)

        verify(!ImageWriterSingleton.hasConnectOrgRegistration(),
               "the saved key is gone from the settings, not just the fields")
        compare(ImageWriterSingleton.getConnectOrgDescription(), "",
                "and so is the description saved beside it")
        compare(fakeContainer.connectOrgDescription, "",
                "and the wizard is not still holding it")
        verify(!step.orgKeyDirty,
               "with nothing typed, there is nothing pending to save")
    }

    function test_the_clear_button_is_described_for_a_screen_reader() {
        build({ "connect_org_enabled": true,
                "connect_org_api_key": "org-key",
                "connect_org_description": "Test org" })

        const clear = findByText(step, "Clear saved key")
        verify(clear !== null)
        verify(String(clear.accessibleDescription).length > 0,
               "a button that removes a stored credential says what it does")
    }

    function test_nothing_is_offered_to_clear_when_nothing_is_saved() {
        // Offering to clear a key that is not there invites the user to
        // wonder whether one is.
        build({ "connect_org_enabled": true })

        verify(!step.hasStoredOrgKey, "no key is held")
        const clear = findByText(step, "Clear saved key")
        verify(clear === null || !clear.visible,
               "so there is nothing to clear")
    }

    // -- The per-user token, and the field it goes in ----------------------

    function test_the_token_field_starts_locked() {
        // Locked until either the browser delivers a token or the countdown
        // runs out, so a user does not start typing over one that is on its
        // way.
        build({})

        verify(!step.tokenFieldEnabled, "the field is not editable yet")
        compare(step.countdownSeconds, 25, "with the full wait ahead of it")
        verify(!step.connectTokenReceived, "and no token in hand")
    }

    function test_a_token_from_the_browser_leaves_the_field_locked() {
        // The field stays disabled for good once the browser has supplied a
        // token: editing it could only turn a token that works into one that
        // does not, and the failure would not show until the Pi tried to
        // enrol.
        build({})

        ImageWriterSingleton.connectTokenReceived("token-from-the-browser")

        verify(step.connectTokenReceived, "the token was taken")
        compare(step.connectToken, "token-from-the-browser")
        verify(step.tokenFromBrowser, "and recorded as coming from the browser")
        verify(!step.tokenFieldEnabled,
               "so the field it landed in stays locked")
    }

    function test_the_field_says_which_of_the_three_states_it_is_in() {
        // The placeholder is the only thing telling the user whether to wait,
        // to paste, or that a token has already arrived.
        build({})
        const field = child("connectTokenField")

        compare(String(field.placeholderText), "Waiting for token (25s)",
                "while the countdown runs, it says how long is left")

        step.countdownSeconds = 0
        compare(String(field.placeholderText), "Paste token here",
                "when it runs out, it invites the token to be pasted")

        ImageWriterSingleton.connectTokenReceived("token-from-the-browser")
        compare(String(field.placeholderText), "Token received from browser",
                "and once one arrives, it says so instead")
    }

    function test_the_field_is_editable_exactly_when_it_is_unlocked() {
        // tokenFieldEnabled is what the countdown sets; this is the wiring
        // that turns it into a field the user can actually type in.
        build({})
        const field = child("connectTokenField")

        verify(!field.enabled, "locked to begin with")
        step.tokenFieldEnabled = true
        verify(field.enabled,
               "and editable once unlocked, so a user whose browser callback "
               + "never arrived can still paste the token by hand")
    }

    // -- Wiping the token when the write is done ---------------------------

    function test_a_spent_token_is_wiped_from_everywhere() {
        // The token is single-use and is cleared when the write completes.
        // Anything left behind would be offered to the next card, where it no
        // longer works -- and the Pi would fail to enrol with no explanation.
        build({})
        ImageWriterSingleton.connectTokenReceived("token-from-the-browser")
        fakeContainer.piConnectEnabled = true
        fakeContainer.customizationSettings.piConnectEnabled = true
        // Run the countdown down first, or the assertion that it is put back
        // to 25 below would hold whether or not anything reset it.
        step.countdownSeconds = 0
        verify(step.connectTokenReceived)

        ImageWriterSingleton.connectTokenCleared()

        verify(!step.connectTokenReceived, "the token is no longer held")
        compare(step.connectToken, "", "nor its text")
        verify(!step.tokenFromBrowser, "nor where it came from")
        verify(!step.tokenFieldEnabled, "the field is locked again")
        compare(step.countdownSeconds, 25, "and the wait is back to the start")
        verify(!fakeContainer.piConnectEnabled,
               "the wizard no longer claims Connect is set up")
        verify(fakeContainer.customizationSettings.piConnectEnabled === undefined,
               "and it is removed from what the generator is given, rather "
               + "than left set to false")

        const field = child("connectTokenField")
        compare(String(field.text), "", "and the field is empty")
    }
}
