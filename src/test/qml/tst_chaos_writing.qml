/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The writing step, driven by a writer having a bad run.
 *
 * Every other storm in this directory asserts that a write never begins.
 * This one is the other half: the write has begun, and what arrives is the
 * sequence a real run produces when something is wrong -- a total that turns
 * out to be understated, a phase that goes backwards, a status line built
 * from a message nobody vetted, a success after an error.
 *
 * Safe by construction rather than by care. The step is built against a stub
 * container, so the wizard's write path does not exist here; no source and no
 * destination are ever set, so the writer cannot become ready; and the storm
 * calls the step's own progress handlers rather than anything that writes.
 * Nothing in this file can reach a device.
 *
 * What it looks for is the step telling the user two different things at
 * once. A percentage past a hundred is not one of them -- that is a real
 * report, from a total the writer was given and that turned out to be short,
 * and it stays on screen because it is the only sign anybody gets. What must
 * hold is that the bar agrees with the label wherever it can, that nothing
 * reads NaN, and that no figure goes below zero.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosWriting"
    when: windowShown
    width: 900
    height: 700
    visible: true

    property int rngState: 1
    property bool stormFinished: false
    property int labelsChecked: 0
    property int percentagesChecked: 0
    property int transitionsSeen: 0
    property int ratesChecked: 0

    QtObject {
        id: containerStub
        property bool disableWarnings: false
        property bool customizationSupported: true
        property bool featUsbGadgetEnabled: false
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property bool piConnectEnabled: false
        property bool ifI2cEnabled: false
        property bool if1WireEnabled: false
        property bool ifSpiEnabled: false
        property string ifSerial: ""
        property string networkInfoText: ""
        property string selectedDeviceName: "Raspberry Pi 5"
        property string selectedOsName: "Raspberry Pi OS (64-bit)"
        property string selectedStorageName: "Generic Mass-Storage 32 GB"
        property int steps: 0
        function nextStep() { steps++ }
    }

    Component {
        id: stepComponent
        WritingStep {
            wizardContainer: containerStub
            width: 900
            height: 700
        }
    }

    property var step: null

    // Math.imul, not *: the product reaches 2.4e18, which is 263 times
    // past the largest integer a double holds exactly, so the multiply
    // rounded before the mask ever ran. The sequence stopped being the
    // LCG it was written as at the second draw, and consecutive draws
    // correlated -- eleven jumps in a row chose the same destination of
    // twelve on one seed, which is what found this.
    function rnd() {
        rngState = (Math.imul(rngState, 1103515245) + 12345) & 0x7fffffff
        return rngState / 0x7fffffff
    }

    function rndInt(n) { return n > 0 ? Math.floor(rnd() * n) % n : 0 }

    function repeat(str, n) {
        var out = ""
        for (var i = 0; i < n; ++i) out += str
        return out
    }

    // Pairs a writer can really emit. The signal carries two quint64, so
    // there are no negatives and no fractions to feed it -- but a total is a
    // declared size, and a declaration can be wrong in either direction.
    function progressPairs() {
        return [
            [0, 0],                       // nothing known yet
            [1, 0],                       // bytes with no total: the gzip case
            [512, 1024],
            [1024, 1024],
            [2048, 1024],                 // more written than was declared
            [1, 3],                       // a third, which does not divide
            [4294967296, 4294967295],     // 4 GiB against just under it
            [9007199254740993, 9007199254740992],  // past what a double holds
            [1, 18446744073709551615],    // a total the size of the field
            [18446744073709551615, 1]     // and the same as the amount done
        ]
    }

    // Status lines come from the layer below, which builds them from paths
    // and device names. None of it is ours.
    function statusLines() {
        return [
            "",
            "Mounting the partition",
            "Unmounting <b>/dev/sda1</b>",
            "<img src='http://rpi-imager-chaos-beacon/x.png'>",
            repeat("status ", 700),
            "Checking the cached image",
            "100% complete",
            "‮" + "elbadaernu",
            "Asking for authorisation\nline two"
        ]
    }

    // What the writer says is limiting it, and how fast it is going. The
    // status is built below us and the figure is an int off a signal, and
    // both are drawn into the same label.
    function bottlenecks() {
        return [
            ["", 0],
            ["Waiting for the card", 0],
            ["Waiting for the card", 1],
            ["Waiting for the card", 1024],
            ["<b>Storage</b> is the bottleneck", 2147483647],
            // Not -1: dividing that by 1024 rounds to -0, which prints as
            // "0" and hides a missing guard. -5120 shows as -5 MB/s.
            ["Verifying", -1],
            ["Verifying", -5120],
            [repeat("bottleneck ", 400), 512]
        ]
    }

    function init() {
        TestAccessibility.setActive(false)

        // The lock that matters. Every file in a run shares one writer, and a
        // file that ran earlier may have left a source on it.
        ImageWriterSingleton.setSrc("")
        ImageWriterSingleton.setDst("")
        verify(!ImageWriterSingleton.readyToWrite(),
               "nothing is ready to write, so nothing here can start one")

        stormFinished = false
        labelsChecked = 0
        percentagesChecked = 0
        transitionsSeen = 0
        ratesChecked = 0

        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        waitForRendering(step)

        // A status line is somebody else's string. Drawn as rich text, the
        // <img> above is fetched, and the fetch is the disclosure.
        failOnWarning(/rpi-imager-chaos-beacon/)
        failOnWarning(/TypeError/)
        failOnWarning(/ReferenceError/)
        failOnWarning(/Binding loop/)
        failOnWarning(/is not a function/)
        failOnWarning(/Unable to assign/)
    }

    function cleanup() {
        // Checked here rather than after the loop: a test function can be
        // abandoned part-way through one, and everything written after it
        // goes too, leaving a pass that stands for nothing.
        verify(stormFinished, "the storm reached the end of its loop")
        verify(labelsChecked > 0, "labels were read (" + labelsChecked + ")")
        verify(percentagesChecked > 0,
               "and percentages were among them (" + percentagesChecked + ")")
        verify(transitionsSeen > 0,
               "and the run changed phase (" + transitionsSeen + ")")
        verify(ratesChecked > 0,
               "and a throughput was drawn (" + ratesChecked + ")")

        // Leave no write running whatever happened, or the next file inherits
        // a writer that is still busy and fails somewhere else.
        if (step) {
            step.bottleneckStatus = ""
            step.writeThroughputKBps = 0
            step.operationWarning = ""
        }
        ImageWriterSingleton.onCancelled()
        ImageWriterSingleton.setSrc("")
        ImageWriterSingleton.setDst("")
        if (step) {
            step.destroy()
            step = null
        }
    }

    function test_storm_data() {
        var override = TestEnv.value("RPI_CHAOS_SEED")
        if (override.length > 0)
            return [ { tag: "seed-" + override, seed: parseInt(override) } ]
        return [ { tag: "seed-23", seed: 23 }, { tag: "seed-7717", seed: 7717 } ]
    }

    function test_storm(data) {
        rngState = data.seed

        const pairs = progressPairs()
        const lines = statusLines()
        const keys = [Qt.Key_Down, Qt.Key_Up, Qt.Key_Tab, Qt.Key_Backtab,
                      Qt.Key_Escape, Qt.Key_Home, Qt.Key_End]

        // Into the writing state, which is what every handler below is a
        // no-op until it reaches.
        ImageWriterSingleton.onFinalizing()
        tryVerify(function() { return step.isWriting }, 3000,
                  "a write is under way")

        // One deliberate pass of each before the random ones. The checks
        // below say what a percentage and a rate may read, which says
        // nothing at all on a run that drew neither -- and a sweep of thirty
        // seeds found one that drew no percentage, because the storm can
        // leave the writing state and every handler is a no-op outside it.
        step.onWriteProgress(512, 1024)
        step.bottleneckStatus = "Waiting for the card"
        step.writeThroughputKBps = 4096
        examineLabels()
        verify(percentagesChecked > 0, "a percentage was drawn to check")
        verify(ratesChecked > 0, "and a rate alongside it")

        for (var stepNo = 0; stepNo < 120; ++stepNo) {
            var action = rndInt(11)
            if (action < 3) {
                var wp = pairs[rndInt(pairs.length)]
                step.onWriteProgress(wp[0], wp[1])
            } else if (action < 6) {
                var vp = pairs[rndInt(pairs.length)]
                step.onVerifyProgress(vp[0], vp[1])
            } else if (action === 6) {
                step.onPreparationStatusUpdate(lines[rndInt(lines.length)])
            } else if (action === 7) {
                // A gzip over 4 GB reports no size, and the step counts
                // megabytes instead. It can switch back on the next run.
                step.isIndeterminateProgress = !step.isIndeterminateProgress
            } else if (action === 8) {
                var b = bottlenecks()[rndInt(bottlenecks().length)]
                step.bottleneckStatus = b[0]
                step.writeThroughputKBps = b[1]
                step.operationWarning = lines[rndInt(lines.length)]
            } else if (action === 9) {
                keyClick(keys[rndInt(keys.length)])
            } else {
                // The three ways a run ends, and the one that starts it
                // again. A success after an error is deliberate: both arrive
                // on the same connection, and nothing orders them.
                var which = rndInt(4)
                if (which === 0)
                    ImageWriterSingleton.onError("no space left on device")
                else if (which === 1)
                    ImageWriterSingleton.onSuccess()
                else if (which === 2)
                    ImageWriterSingleton.onCancelled()
                else
                    ImageWriterSingleton.onFinalizing()
                ++transitionsSeen
                wait(1)
            }

            examineLabels()
            checkTheBar()
            verify(!ImageWriterSingleton.readyToWrite(),
                   "the writer stayed unready throughout")
        }

        stormFinished = true
        console.log("ChaosWriting seed", data.seed, "labels", labelsChecked,
                    "percentages", percentagesChecked,
                    "transitions", transitionsSeen)
    }

    // Every figure on the step has to be one a person could read against a
    // progress bar. A total that was understated and a value that ran past
    // the end both come out here.
    function examineLabels() {
        var labels = []
        collectText(step, labels, 0)
        for (var i = 0; i < labels.length; ++i) {
            var raw = String(labels[i])
            var s = raw.toLowerCase()
            ++labelsChecked
            verify(s.indexOf("nan") < 0, "a figure read as NaN: " + raw)
            verify(s.indexOf("infinity") < 0, "a figure ran away: " + raw)
            verify(s.indexOf("undefined") < 0, "a label read as undefined: " + raw)

            // A status line can carry a percent sign of its own, so only the
            // step's own progress text counts here.
            var m = raw.match(/^(?:Writing|Verifying)\.\.\. (-?\d+)%$/)
            if (m) {
                var percent = parseInt(m[1])
                ++percentagesChecked
                // Nothing below zero: the signal carries two unsigned
                // quantities, so a negative would be ours.
                verify(percent >= 0, "no negative progress: " + raw)
                agreeWithTheBar(percent, raw)
            }

            // The throughput is an int off a signal, shown only when the
            // step has decided it is positive. A rate below zero on screen
            // means that decision was skipped.
            var r = raw.match(/(-?\d+) MB\/s/)
            if (r) {
                ++ratesChecked
                verify(parseInt(r[1]) >= 0, "no negative rate: " + raw)
            }
        }
    }

    // The label and the bar are set together, so below a hundred they are the
    // same number or the step is saying two things at once. Above it the bar
    // has nowhere left to go and sits full, which is the one disagreement
    // that is allowed -- and the label keeps the real figure.
    function agreeWithTheBar(percent, raw) {
        var bar = findChild(step, "writeProgressBar")
        if (!bar)
            return
        verify(bar.value >= bar.from && bar.value <= bar.to,
               "the bar stayed inside its own range: " + bar.value)
        if (percent <= 100)
            compare(Math.round(bar.value), percent,
                    "the bar reads what the label does: " + raw)
        else
            compare(bar.value, bar.to,
                    "and above its range it is full: " + raw)
    }

    function checkTheBar() {
        var bar = findChild(step, "writeProgressBar")
        if (!bar)
            return
        verify(bar.value >= bar.from && bar.value <= bar.to,
               "the bar stayed inside its own range: " + bar.value
               + " in " + bar.from + ".." + bar.to)
    }

    function collectText(item, out, depth) {
        if (!item || depth > 10 || out.length > 40)
            return
        if (typeof item.text === "string" && item.text.length > 0)
            out.push(item.text)
        var kids = item.children
        for (var i = 0; kids && i < kids.length; ++i)
            collectText(kids[i], out, depth + 1)
    }
}
