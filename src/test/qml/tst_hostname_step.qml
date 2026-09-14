/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * HostnameCustomizationStep: the name the board answers to.
 *
 * The hostname is how a headless Pi is found. Get it onto the card and the
 * user reaches it by name on the first boot; lose it and they are hunting
 * through a router's lease table for a board they cannot otherwise identify.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "HostnameStep"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property var customizationSettings: ({})
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property string networkInfoText: ""
        property int stepWriting: 11
        property int jumpedTo: -1
        function jumpToStep(n) { jumpedTo = n }
        function skipAllCustomisation() { jumpToStep(stepWriting) }
    }

    Component {
        id: stepComponent
        HostnameCustomizationStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    function init() {
        fakeContainer.customizationSettings = ({})
        fakeContainer.hostnameConfigured = false
        fakeContainer.jumpedTo = -1
        ImageWriterSingleton.removePersistedCustomisationSetting("hostname")
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
    }

    function cleanup() {
        if (step) {
            step.destroy()
            step = null
        }
        ImageWriterSingleton.removePersistedCustomisationSetting("hostname")
    }

    function field() {
        var f = findChild(step, "hostnameField")
        verify(f, "found the hostname field")
        return f
    }

    function persisted() {
        return ImageWriterSingleton.getSavedCustomisationSettings().hostname
    }

    // Builds the step again with a hostname already in the settings, which
    // is the only way to reach the restore path: init() creates the step
    // before a case can put anything there.
    function rebuildWith(saved) {
        step.destroy()
        step = null
        fakeContainer.customizationSettings = ({ hostname: saved })
        fakeContainer.hostnameConfigured = false
        step = stepComponent.createObject(testCase)
        verify(step, "the step was rebuilt")
        return step
    }

    function notice() {
        return findChild(step, "hostnameRejectedNotice")
    }

    // -- What comes back from a previous session ---------------------------

    function test_a_saved_hostname_that_still_passes_is_filled_in() {
        rebuildWith("pi-in-the-shed")

        compare(field().text, "pi-in-the-shed")
        verify(fakeContainer.hostnameConfigured, "and counts as configured")
        verify(!notice().visible, "with nothing to complain about")
    }

    function test_a_saved_hostname_the_rules_now_refuse_is_not_filled_in() {
        // Assigning text does not run the validator -- Qt only clears
        // acceptableInput, which nothing here reads -- so a value written by
        // an older version, edited by hand into the settings file, or left
        // behind by a tightening of the rules came straight back and was
        // written out again on Next.
        rebuildWith("-starts-with-a-hyphen")

        compare(field().text, "")
        verify(!fakeContainer.hostnameConfigured,
               "and the step is not treated as configured")
    }

    function test_a_refused_hostname_is_explained_rather_than_dropped() {
        // An empty box and no reason for it is the worst of the outcomes.
        rebuildWith("not a hostname")

        verify(notice().visible, "the step says what happened")
        verify(notice().text.indexOf("not a hostname") >= 0,
               "and names the value it refused: " + notice().text)
    }

    function test_a_refused_hostname_is_not_written_back_out() {
        rebuildWith("far-too-long-" + "x".repeat(80))

        step.nextClicked()

        verify(fakeContainer.customizationSettings.hostname === undefined,
               "the bad value did not survive the step")
    }

    function test_what_is_quoted_back_is_printable_and_bounded() {
        // The value is the user's own, but a hand-edited settings file can
        // hold anything, and this string goes in front of them.
        rebuildWith("bad\u0001\u0002name\u007f" + "y".repeat(200))

        verify(notice().visible, "the step says what happened");
        var shown = notice().text
        verify(shown.length < 200, "the quote is bounded (" + shown.length + ")")
        for (var i = 0; i < shown.length; ++i) {
            var c = shown.charCodeAt(i)
            verify(c >= 0x20 && c !== 0x7F,
                   "printable at " + i + " (" + c + ")")
        }
    }

    // -- What leaving the step records -------------------------------------

    function test_a_hostname_typed_in_is_recorded() {
        field().text = "pi-in-the-shed"

        step.nextClicked()

        compare(fakeContainer.customizationSettings.hostname, "pi-in-the-shed")
        verify(fakeContainer.hostnameConfigured,
               "and the wizard knows the step was configured")
    }

    function test_a_hostname_is_kept_for_the_next_session() {
        // Someone imaging a batch of boards should not retype it each time.
        field().text = "pi-in-the-shed"

        step.nextClicked()

        compare(persisted(), "pi-in-the-shed")
    }

    function test_an_empty_field_records_no_hostname_at_all() {
        // Absent, not empty. An empty hostname in the generated
        // configuration is not a board with no name, it is a board with a
        // broken one.
        field().text = ""

        step.nextClicked()

        verify(fakeContainer.customizationSettings.hostname === undefined,
               "the setting is absent rather than empty")
        verify(!fakeContainer.hostnameConfigured)
    }

    function test_clearing_the_field_takes_the_saved_one_with_it() {
        // The direction that matters for a batch: having named one board,
        // the name has to be droppable for the next. Left behind, it comes
        // back on the next card and two boards answer to the same name.
        field().text = "pi-in-the-shed"
        step.nextClicked()
        compare(persisted(), "pi-in-the-shed")

        field().text = ""
        step.nextClicked()

        verify(fakeContainer.customizationSettings.hostname === undefined,
               "the runtime setting went")
        verify(persisted() === undefined, "and so did the saved one")
    }

    function test_a_changed_hostname_replaces_the_old_one() {
        field().text = "first-name"
        step.nextClicked()

        field().text = "second-name"
        step.nextClicked()

        compare(fakeContainer.customizationSettings.hostname, "second-name")
        compare(persisted(), "second-name")
    }

    function test_the_field_trims_what_was_pasted_in() {
        // A hostname copied out of a document brings whitespace with it, and
        // a trailing space is not part of a name any resolver will answer to.
        field().text = "  pi-in-the-shed  "

        step.nextClicked()

        compare(fakeContainer.customizationSettings.hostname, "pi-in-the-shed")
    }

    // -- Skipping ----------------------------------------------------------

    function test_skipping_here_skips_all_of_it() {
        step.skipClicked()

        compare(fakeContainer.jumpedTo, fakeContainer.stepWriting)
    }
}
