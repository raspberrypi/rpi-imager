/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * OSSelectionStep: what choosing a row actually does.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "OSSelectionStepChoice"
    when: windowShown
    width: 900
    height: 700
    visible: true

    // Every property selectOSitem() writes when a concrete OS is chosen.
    // Choosing an OS re-derives the whole capability picture and scrubs
    // settings the new OS cannot honour, so a stub missing any of these
    // throws part-way through and leaves the step half-configured -- with
    // the assertions still passing, because they are about something else.
    QtObject {
        id: fakeContainer
        property var customizationSettings: ({})
        property string selectedOsName: ""
        property string networkInfoText: ""
        property bool customizationSupported: true
        property bool ccRpiAvailable: false
        property bool ifAndFeaturesAvailable: false
        property bool piConnectAvailable: false
        property bool piConnectEnabled: false
        property bool secureBootAvailable: false
        property bool secureBootEnabled: false
        property bool passwordlessSudoAvailable: false
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property bool ifI2cEnabled: false
        property bool ifSpiEnabled: false
        property bool if1WireEnabled: false
        property bool featUsbGadgetEnabled: false
        property string ifSerial: ""
        property Item overlayRootRef: testCase
        function jumpToStep(n) {}
    }

    Component {
        id: stepComponent
        OSSelectionStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    SignalSpy {
        id: advanced
        signalName: "nextClicked"
    }

    // The three kinds of row, as the model hands them over.
    // A concrete OS reaches ImageWriter.setSrc(), which is C++ and typed --
    // the size fields have to be numbers or the call is refused outright.
    readonly property var anOs: ({
        name: "Raspberry Pi OS (64-bit)",
        subitems_json: "",
        subitems_url: "",
        url: "https://example.invalid/os.img.xz",
        image_download_size: 500,
        extract_size: 1000,
        extract_sha256: "abc123",
        contains_multiple_files: false,
        release_date: "2025-01-01",
        init_format: "",
        // Also handed to C++, and undefined matches neither overload of
        // setSWCapabilitiesList -- so an incomplete row is refused at the
        // boundary rather than quietly selected.
        capabilities: []
    })
    readonly property var aCategory: ({
        name: "Emulation and game OS",
        subitems_json: '[{"name":"nested","subitems_json":"","subitems_url":""}]',
        subitems_url: ""
    })
    readonly property var aRemoteCategory: ({
        name: "Other general-purpose OS",
        subitems_json: "",
        subitems_url: "https://example.invalid/other.json"
    })
    readonly property var theBackRow: ({
        name: "Go back",
        subitems_json: "",
        subitems_url: "internal://back"
    })

    function init() {
        fakeContainer.selectedOsName = ""
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

    // Make advancing possible, so a case that does not advance is saying
    // something about the row rather than about the button being disabled.
    function allowAdvance() {
        step.oslist.currentIndex = 0
        fakeContainer.selectedOsName = "Raspberry Pi OS (64-bit)"
        tryVerify(function () { return step.nextButtonEnabled }, 3000,
                  "the step would let us move on")
    }

    // ── Which rows tell the wizard to move on ─────────────────────────

    function test_a_row_is_recognised_for_what_it_is_data() {
        return [
            { tag: "an OS",               row: "anOs",            sublist: false },
            { tag: "an inline category",  row: "aCategory",       sublist: true },
            { tag: "a fetched category",  row: "aRemoteCategory", sublist: true },
            // The back row carries a subitems_url like a category, and is
            // told apart only by its value. Treated as a category it would
            // try to descend into itself.
            { tag: "the back row",        row: "theBackRow",      sublist: false }
        ]
    }

    function test_a_row_is_recognised_for_what_it_is(data) {
        compare(step.isOSsublist(testCase[data.row]), data.sublist, data.tag)
    }

    function test_choosing_an_os_from_the_keyboard_moves_on() {
        allowAdvance()

        step.handleOSSelection(anOs, true, false)

        tryVerify(function () { return advanced.count === 1 }, 3000,
                  "the wizard advanced to the card chooser")
    }

    function test_choosing_a_category_does_not_move_on_data() {
        return [
            { tag: "inline",  row: "aCategory" },
            { tag: "fetched", row: "aRemoteCategory" }
        ]
    }

    function test_choosing_a_category_does_not_move_on(data) {
        // The case that matters. Advancing here would land on the card
        // chooser with no image chosen and a row that has no URL.
        allowAdvance()

        step.handleOSSelection(testCase[data.row], true, false)

        wait(300)
        compare(advanced.count, 0, data.tag + " category did not advance")
    }

    function test_choosing_go_back_does_not_move_on() {
        // Going up a level is not a choice of image.
        allowAdvance()

        step.handleOSSelection(theBackRow, true, false)

        wait(300)
        compare(advanced.count, 0)
    }

    function test_choosing_an_os_with_the_mouse_does_not_move_on() {
        // Auto-advance is the keyboard's convenience; a mouse user is
        // already looking at the Next button and may want to change their
        // mind before pressing it.
        allowAdvance()

        step.handleOSSelection(anOs, false, true)

        wait(300)
        compare(advanced.count, 0)
    }

    // ── The right arrow only descends ─────────────────────────────────

    function test_the_right_arrow_descends_into_a_category() {
        step.handleOSNavigation(aCategory)

        tryVerify(function () { return step.categorySelected.length > 0 }, 3000,
                  "it went into the category")
    }

    function test_the_right_arrow_ignores_an_os() {
        // Right arrow means "go deeper". On a row with nothing below it that
        // must do nothing, not select it.
        allowAdvance()

        step.handleOSNavigation(anOs)

        wait(300)
        compare(advanced.count, 0)
        compare(step.categorySelected, "", "and nothing was descended into")
    }
    // ── Changing your mind about a custom image ───────────────────────
    //
    // "Use custom" opens a file picker, and the way out of it is Cancel.
    // Nothing is supposed to happen: the OS already chosen stays chosen, and
    // the customisation that OS supports stays available.
    //
    // Worth stating, because accepting is destructive here by design -- a
    // custom image cannot carry customisation, so choosing one throws the
    // staged Wi-Fi, user account and SSH settings away. A cancel that fell
    // through to the same handler would do all of that for a user who
    // pressed Cancel, and nothing would say so.

    function test_cancelling_the_image_picker_changes_nothing() {
        fakeContainer.selectedOsName = "Raspberry Pi OS (64-bit)"
        fakeContainer.customizationSupported = true
        fakeContainer.wifiConfigured = true

        const picker = step.customImageFileDialog
        verify(picker, "the step carries the image picker")
        picker.open()
        tryVerify(function () { return picker.opened }, 3000,
                  "the picker came up")

        // Browsed to a file and then thought better of it, which is the
        // case that matters: with nothing highlighted there is nothing a
        // fall-through could pick up.
        const decoy = TestFiles.write("cancelled-choice.img", "not an image")
        verify(decoy !== "", "wrote a file for the picker to be pointing at")
        picker.selectedFile = decoy

        const cancel = findChild(picker, "fileDialogCancelButton")
        verify(cancel, "the picker offers a way out")
        // Emitted rather than clicked: the picker is a Popup, and the
        // offscreen harness does not deliver synthesised presses into one.
        cancel.clicked()

        tryVerify(function () { return !picker.visible }, 3000,
                  "the picker closed")

        compare(fakeContainer.selectedOsName, "Raspberry Pi OS (64-bit)",
                "the OS already chosen is still chosen")
        verify(fakeContainer.customizationSupported,
               "and it still advertises customisation")
        verify(fakeContainer.wifiConfigured,
               "so the staged settings are still there")
    }
}
