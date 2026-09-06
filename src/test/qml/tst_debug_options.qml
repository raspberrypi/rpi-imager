/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * DebugOptionsDialog: twelve switches that change how a write behaves.
 *
 * The dialog is a round trip and nothing more -- initialize() reads twelve
 * settings into twelve controls, applySettings() writes them back. Which is
 * exactly the shape that goes wrong quietly: a control wired to the wrong
 * getter, or a setter left out of the list, changes a setting the user did
 * not touch or fails to change one they did, and there is nothing on screen
 * to show it.
 *
 * That matters most for the ones that switch off a safety check. "Skip
 * end-of-device" is counterfeit-card mode: it disables the write that
 * proves the card really holds what it claims, which is the only thing
 * standing between a fake card and an image that appears to write and then
 * loses data. If the box did not apply, someone diagnosing a fake card gets
 * the wrong answer; if it applied when unticked, everyone loses the check.
 *
 * The dialog was at 0%.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "DebugOptions"
    when: windowShown
    width: 900
    height: 800
    visible: true

    QtObject {
        id: fakeContainer
        property bool disableWarnings: false
        property bool secureBootAvailable: false
    }

    Component {
        id: dialogComponent
        DebugOptionsDialog {
            wizardContainer: fakeContainer
        }
    }

    property var dialog: null

    function init() {
        dialog = dialogComponent.createObject(testCase)
        verify(dialog, "the dialog was created")
        dialog.open()
        tryVerify(function () { return dialog.opened }, 3000, "the dialog opened")
    }

    function cleanup() {
        if (dialog) {
            dialog.close()
            dialog.destroy()
            dialog = null
        }
    }

    // Closing a Popup is deferred, and the dialog only re-reads its settings
    // when onClosed has reset its initialized flag. Reopening in the same
    // frame therefore shows the values it already had. A person cannot close
    // and reopen a dialog inside one frame; a test can.
    function reopen() {
        dialog.close()
        // Not !opened: a Popup clears that when the close begins, but emits
        // closed() -- which is what resets the dialog's initialized flag --
        // only after the exit transition has finished. Reopening in between
        // gets a dialog that never re-read its settings.
        tryVerify(function () { return !dialog.visible }, 3000, "the dialog closed")
        dialog.open()
        tryVerify(function () { return dialog.opened }, 3000, "the dialog reopened")
    }

    function child(name) {
        var c = findChild(dialog, name)
        verify(c, "found " + name)
        return c
    }

    // Every boolean switch, with the pair of accessors it is supposed to sit
    // between. Reading and writing through the singleton rather than through
    // a second copy of the dialog is what makes a crossed pair visible.
    function test_each_switch_reaches_the_setting_it_names_data() {
        return [
            { tag: "direct I/O",           control: "debugDirectIO",
              get: "getDebugDirectIO",           set: "setDebugDirectIO" },
            { tag: "async I/O",            control: "debugAsyncIO",
              get: "getDebugAsyncIO",            set: "setDebugAsyncIO" },
            { tag: "ignore device limits", control: "debugIgnoreDeviceLimits",
              get: "getDebugIgnoreDeviceLimits", set: "setDebugIgnoreDeviceLimits" },
            { tag: "periodic sync",        control: "debugPeriodicSync",
              get: "getDebugPeriodicSync",       set: "setDebugPeriodicSync" },
            { tag: "verbose logging",      control: "debugVerboseLogging",
              get: "getDebugVerboseLogging",     set: "setDebugVerboseLogging" },
            { tag: "IPv4 only",            control: "debugIPv4Only",
              get: "getDebugIPv4Only",           set: "setDebugIPv4Only" },
            { tag: "counterfeit card",     control: "debugSkipEndOfDevice",
              get: "getDebugSkipEndOfDevice",    set: "setDebugSkipEndOfDevice" },
            { tag: "rpiboot",              control: "debugRpiboot",
              get: "getDebugRpiboot",            set: "setDebugRpiboot" },
            { tag: "force secure boot",    control: "debugForceSecureBoot",
              get: "getDebugForceSecureBoot",    set: "setDebugForceSecureBoot" },
            { tag: "sign fastboot gadget", control: "debugSignFastbootGadget",
              get: "getDebugSignFastbootGadget", set: "setDebugSignFastbootGadget" }
        ]
    }

    function test_each_switch_reaches_the_setting_it_names(data) {
        var original = ImageWriterSingleton[data.get]()

        // Ticking it and applying has to reach that setting and no other.
        child(data.control).checked = true
        dialog.applySettings()
        verify(ImageWriterSingleton[data.get](), data.tag + " was switched on")

        child(data.control).checked = false
        dialog.applySettings()
        verify(!ImageWriterSingleton[data.get](), data.tag + " was switched off again")

        ImageWriterSingleton[data.set](original)
    }

    function test_reopening_shows_what_is_actually_set_data() {
        return test_each_switch_reaches_the_setting_it_names_data()
    }

    function test_reopening_shows_what_is_actually_set(data) {
        // The other half of the round trip: a setting changed elsewhere has
        // to be read back, or the dialog shows a stale answer and applying
        // it silently reverts whatever changed.
        var original = ImageWriterSingleton[data.get]()

        ImageWriterSingleton[data.set](true)
        reopen()

        verify(child(data.control).checked, data.tag + " read back as set")

        ImageWriterSingleton[data.set](original)
    }

    function test_the_queue_depth_survives_the_round_trip() {
        var original = ImageWriterSingleton.getDebugAsyncQueueDepth()

        ImageWriterSingleton.setDebugAsyncQueueDepth(16)
        reopen()

        dialog.applySettings()
        compare(ImageWriterSingleton.getDebugAsyncQueueDepth(), 16,
                "reopening and applying did not move it")

        ImageWriterSingleton.setDebugAsyncQueueDepth(original)
    }

    function test_switches_do_not_disturb_each_other() {
        // A crossed pair would show up here even if each switch looked
        // right on its own.
        var wasSkip = ImageWriterSingleton.getDebugSkipEndOfDevice()
        var wasRpiboot = ImageWriterSingleton.getDebugRpiboot()

        ImageWriterSingleton.setDebugSkipEndOfDevice(false)
        ImageWriterSingleton.setDebugRpiboot(false)
        reopen()

        child("debugSkipEndOfDevice").checked = true
        dialog.applySettings()

        verify(ImageWriterSingleton.getDebugSkipEndOfDevice(), "the one that was ticked")
        verify(!ImageWriterSingleton.getDebugRpiboot(), "and only that one")

        ImageWriterSingleton.setDebugSkipEndOfDevice(wasSkip)
        ImageWriterSingleton.setDebugRpiboot(wasRpiboot)
    }
}
