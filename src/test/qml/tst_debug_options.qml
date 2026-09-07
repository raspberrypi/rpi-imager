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


    // ── Leaving the dialog without applying anything ──────────────────
    //
    // Every case above calls applySettings() directly. The three ways a
    // person actually leaves this dialog -- Apply, Cancel, escape -- were
    // uncovered, and the two that are supposed to change nothing are the
    // ones that matter.
    //
    // These switches change how the next write behaves: forcing secure
    // boot, ignoring the device's own I/O limits, skipping the end-of-device
    // check. Someone opening the dialog to look at it, flicking a switch and
    // then thinking better of it must not be left with that setting in force
    // for a card they write afterwards.

    function test_cancelling_applies_nothing() {
        var was = ImageWriterSingleton.getDebugForceSecureBoot()
        child("debugForceSecureBoot").checked = !was

        child("debugCancelButton").clicked()

        tryVerify(function () { return !dialog.visible }, 3000,
                  "the dialog closed")
        compare(ImageWriterSingleton.getDebugForceSecureBoot(), was,
                "and the switch that was flicked was not applied")
    }

    function test_escape_applies_nothing_either() {
        var was = ImageWriterSingleton.getDebugIgnoreDeviceLimits()
        child("debugIgnoreDeviceLimits").checked = !was

        dialog.escapePressed()

        tryVerify(function () { return !dialog.visible }, 3000)
        compare(ImageWriterSingleton.getDebugIgnoreDeviceLimits(), was)
    }

    function test_apply_applies_and_closes() {
        // The counterpart, without which the two above would pass on a
        // dialog that could never change anything at all.
        var was = ImageWriterSingleton.getDebugVerboseLogging()
        child("debugVerboseLogging").checked = !was

        child("debugApplyButton").clicked()

        compare(ImageWriterSingleton.getDebugVerboseLogging(), !was,
                "the switch took effect")
        tryVerify(function () { return !dialog.visible }, 3000,
                  "and the dialog closed behind it")

        ImageWriterSingleton.setDebugVerboseLogging(was)
    }

    // ── The queue depth is not a free number ──────────────────────────

    function test_the_queue_depth_snaps_to_a_value_the_writer_supports_data() {
        return [
            { tag: "just above 16",  dropped: 17,  lands: 16 },
            { tag: "just below 32",  dropped: 30,  lands: 32 },
            { tag: "between 64 and 128", dropped: 100, lands: 128 },
            { tag: "the very bottom", dropped: 1,   lands: 1 },
            { tag: "the very top",    dropped: 512, lands: 512 }
        ]
    }

    function test_the_queue_depth_snaps_to_a_value_the_writer_supports(data) {
        // The slider moves one at a time across a range of five hundred, and
        // what it lands on is handed to the writer as a ring-buffer depth.
        // Snapping is what keeps that a value the ring is built for; without
        // it the number depends on where a mouse happened to stop.
        var slider = child("debugQueueDepthSlider")
        slider.value = data.dropped

        slider.moved()

        compare(Math.round(slider.value), data.lands, data.tag)
    }

    function test_a_snapped_depth_is_what_gets_applied() {
        // End to end: what the writer is given is the snapped value, not
        // the one the slider was left on.
        var original = ImageWriterSingleton.getDebugAsyncQueueDepth()
        var slider = child("debugQueueDepthSlider")
        slider.value = 100
        slider.moved()

        child("debugApplyButton").clicked()

        compare(ImageWriterSingleton.getDebugAsyncQueueDepth(), 128)

        ImageWriterSingleton.setDebugAsyncQueueDepth(original)
    }
}
