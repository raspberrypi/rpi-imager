/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * EulaAcceptanceStep: the licence an OS asks the user to accept.
 *
 * Nothing here fetches: what a fetch would bring is emitted on the writer's
 * signals directly.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "EulaStep"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property string networkInfoText: ""
        property string selectedOsName: "Vendor OS"
        property bool eulaAccepted: false
        property int stepStorageSelection: 3
        property int invalidatedFrom: -1
        function invalidateStepsFrom(n) { invalidatedFrom = n }
    }

    Component {
        id: stepComponent
        EulaAcceptanceStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    function init() {
        ImageWriterSingleton.setSrc("")
        fakeContainer.eulaAccepted = false
        fakeContainer.invalidatedFrom = -1
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        waitForRendering(step)
    }

    function cleanup() {
        if (step) {
            step.destroy()
            step = null
        }
        ImageWriterSingleton.setSrc("")
    }

    function named(name) {
        const item = findChild(step, name)
        verify(item, "found " + name)
        return item
    }

    function arrive(text, acceptedBefore) {
        ImageWriterSingleton.eulaReady(text, acceptedBefore)
        compare(step.fetchStatus, "ready")
        waitForRendering(step)
    }

    function test_the_os_is_named() {
        compare(step.subtitle, "Vendor OS")
    }

    function test_next_waits_until_the_licence_is_accepted() {
        arrive("Terms of use", false)
        verify(!step.nextButtonEnabled, "nothing accepted yet")

        mouseClick(named("eulaAcceptCheckBox"))

        verify(named("eulaAcceptCheckBox").checked)
        verify(step.nextButtonEnabled, "accepted, so Next goes on")
        verify(fakeContainer.eulaAccepted)
    }

    function test_withdrawing_acceptance_closes_the_way_on() {
        arrive("Terms of use", false)
        const box = named("eulaAcceptCheckBox")
        mouseClick(box)
        verify(step.nextButtonEnabled)

        mouseClick(box)

        verify(!step.nextButtonEnabled)
        verify(!fakeContainer.eulaAccepted)
        compare(fakeContainer.invalidatedFrom, fakeContainer.stepStorageSelection,
                "and the steps reached on the strength of it")
    }

    function test_a_licence_accepted_before_is_shown_accepted() {
        arrive("Terms of use", true)

        verify(named("eulaAcceptCheckBox").checked)
        verify(step.nextButtonEnabled)
        verify(named("eulaAcceptedBeforeText").visible, "and says why")
    }

    function test_a_new_licence_is_not_said_to_be_accepted_before() {
        arrive("Terms of use", false)
        verify(!named("eulaAcceptedBeforeText").visible)
    }

    function test_the_text_is_never_read_as_markup() {
        // Rich text would drop the tags from view and fetch the image.
        const raw = "<b>Bold</b> <img src=\"https://example.invalid/x.png\"> &amp;"
        arrive(raw, false)

        const area = named("eulaText")
        compare(area.textFormat, TextEdit.PlainText)
        verify(area.readOnly, "and it cannot be edited")
        compare(area.getText(0, area.length), raw, "shown exactly as sent")
    }

    function test_a_failure_says_why_and_offers_another_go() {
        ImageWriterSingleton.eulaFailed("HTTP 404: Not Found")

        compare(step.fetchStatus, "failed")
        const reason = named("eulaFailureText")
        verify(reason.visible)
        verify(reason.text.indexOf("HTTP 404: Not Found") >= 0, reason.text)
        verify(named("eulaRetryButton").visible)
        verify(!named("eulaAcceptCheckBox").visible, "nothing to accept")
        verify(!step.nextButtonEnabled)
    }

    function test_retry_asks_again() {
        ImageWriterSingleton.eulaFailed("HTTP 503: Service Unavailable")
        // Refused at once, without fetching.
        ImageWriterSingleton.setEulaUrl("http://example.invalid/licence.txt")

        mouseClick(named("eulaRetryButton"))

        compare(step.fetchStatus, "failed")
        verify(named("eulaFailureText").text.indexOf("https") >= 0,
               "the new answer, not the old one")
    }

    function test_a_failure_after_acceptance_withdraws_it() {
        arrive("Terms of use", true)
        verify(fakeContainer.eulaAccepted)

        ImageWriterSingleton.eulaFailed("HTTP 500: Internal Server Error")

        verify(!fakeContainer.eulaAccepted)
        verify(!step.nextButtonEnabled)
    }
}
