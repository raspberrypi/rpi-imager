/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Telling an OS apart from a folder of them.
 *
 * The OS list mixes three kinds of entry that look alike in the delegate:
 * something to write, a sublist to descend into ("Raspberry Pi OS (other)",
 * "Emulation and game OS"), and the back entry that leaves one. Mistake a
 * folder for an image and selecting it arms a write with nothing to write;
 * mistake an image for a folder and it cannot be chosen at all.
 *
 * The entries come from JSON off the network, so every field is optional
 * and any of them can be missing, empty, or not a string.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "OSSublistDetection"
    when: windowShown
    width: 700
    height: 500
    visible: true

    QtObject {
        id: settingsStub
        property string wifiSSID: ""
        property string wifiMode: "secure"
    }

    QtObject {
        id: containerStub
        // WizardStepBase reads this on every step.
        property string networkInfoText: ""
        property bool ccRpiAvailable: false
        property var customizationSettings: settingsStub
        property bool customizationSupported: true
        property bool featUsbGadgetEnabled: false
        property bool hostnameConfigured: false
        property bool ifAndFeaturesAvailable: false
        property bool ifI2cEnabled: false
        property bool if1WireEnabled: false
        property string ifSerial: ""
        property bool ifSpiEnabled: false
        property bool localeConfigured: false
        property var overlayRootRef: null
    }

    Component {
        id: stepComponent
        OSSelectionStep { wizardContainer: containerStub }
    }

    property var step: null

    function initTestCase() {
        step = stepComponent.createObject(testCase)
        verify(step, "the OS step was created")
    }

    function cleanupTestCase() {
        if (step) {
            step.destroy()
            step = null
        }
    }

    // -- Something to write ------------------------------------------------

    function test_a_plain_image_is_not_a_sublist() {
        verify(!step.isOSsublist({
            name: "Raspberry Pi OS (64-bit)",
            subitems_json: "",
            subitems_url: ""
        }))
    }

    function test_an_entry_with_no_subitem_fields_at_all_is_not_a_sublist() {
        // Entries come from JSON, so a field that was never written is
        // undefined rather than empty.
        verify(!step.isOSsublist({ name: "Raspberry Pi OS Lite" }))
    }

    // -- A folder to descend into ------------------------------------------

    function test_an_entry_with_inline_subitems_is_a_sublist() {
        verify(step.isOSsublist({
            name: "Emulation and game OS",
            subitems_json: "[{\"name\":\"nested\"}]",
            subitems_url: ""
        }))
    }

    function test_an_entry_with_a_subitems_url_is_a_sublist() {
        verify(step.isOSsublist({
            name: "Raspberry Pi OS (other)",
            subitems_json: "",
            subitems_url: "https://example.invalid/other.json"
        }))
    }

    function test_either_field_alone_is_enough() {
        verify(step.isOSsublist({ subitems_json: "[]" }))
        verify(step.isOSsublist({ subitems_url: "https://example.invalid/x" }))
    }

    // -- The way back out --------------------------------------------------

    function test_the_back_entry_is_not_a_sublist() {
        // It carries a subitems_url like a folder does, and descending into
        // it would be going the wrong way.
        verify(!step.isOSsublist({
            name: "Go back",
            subitems_json: "",
            subitems_url: "internal://back"
        }))
    }

    function test_the_back_sentinel_is_matched_exactly() {
        // A real url that merely begins the same way is still a sublist.
        verify(step.isOSsublist({ subitems_url: "internal://back-catalogue" }))
        verify(step.isOSsublist({ subitems_url: "internal://backup.json" }))
    }

    function test_a_back_entry_carrying_inline_subitems_is_still_a_sublist() {
        // The json field is checked independently, so it wins.
        verify(step.isOSsublist({
            subitems_json: "[{\"name\":\"nested\"}]",
            subitems_url: "internal://back"
        }))
    }

    // -- Fields that are not strings ---------------------------------------

    function test_a_non_string_subitems_field_is_ignored() {
        // JSON can put anything in a field. Only a string counts.
        verify(!step.isOSsublist({ subitems_json: 42 }))
        verify(!step.isOSsublist({ subitems_url: 42 }))
        verify(!step.isOSsublist({ subitems_json: {} }))
        verify(!step.isOSsublist({ subitems_url: [] }))
        verify(!step.isOSsublist({ subitems_json: true }))
    }

    function test_a_null_subitems_field_is_ignored() {
        verify(!step.isOSsublist({ subitems_json: null, subitems_url: null }))
    }

    function test_an_entry_with_nothing_in_it_is_not_a_sublist() {
        verify(!step.isOSsublist({}))
    }
}
