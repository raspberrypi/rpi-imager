/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The drive list, drawn from descriptors nobody chose.
 *
 * The OS list is untrusted because a repository serves it. The drive list is
 * untrusted for a different reason: the machine serves it, and a card reader
 * reports whatever its firmware says -- a blank description, a capacity of
 * zero, a name longer than the row. It is also the dangerous list, because a
 * row here names the disk a write erases.
 *
 * The ordered storage cases feed it two tidy cards. This feeds it the shapes
 * a real machine produces on a bad day, storms the step, and swaps the list
 * out underneath the storm.
 *
 * Three locks. The step is built on its own against a stub container, so the
 * wizard's write path does not exist here; no source is ever set, so the
 * writer cannot be ready; and every descriptor names a device under a prefix
 * that cannot be a device, checked before the list is set.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosStorage"
    when: windowShown
    width: 900
    height: 700
    visible: true

    // Nothing can open a path below a name that is not a directory, so a
    // write that escaped every other lock would still fail to reach a disk.
    readonly property string fakePrefix: "/dev/null/rpi-imager-chaos-"

    property int rngState: 1
    property bool writeStarted: false
    property bool stormFinished: false
    property int rowsExamined: 0
    property int figuresChecked: 0
    property int selectionsSeen: 0
    property int forbiddenSelections: 0

    Connections {
        target: ImageWriterSingleton
        function onPreparationStatusUpdate(msg) { testCase.writeStarted = true }
        function onWriteProgress(now, total)    { testCase.writeStarted = true }
        function onSuccess()                    { testCase.writeStarted = true }
    }

    QtObject {
        id: containerStub
        // Declared because WizardStepBase binds to it. A stub missing a
        // property the real container has does not fail the binding, it
        // resolves to undefined -- which QML reports and nothing read.
        property string networkInfoText: ""
        property bool disableWarnings: false
        property var overlayRootRef: overlayRoot
        property string selectedStorageName: ""
        property bool targetIsFastboot: false
    }

    Item {
        id: overlayRoot
        anchors.fill: parent
    }

    Component {
        id: stepComponent
        StorageSelectionStep {
            wizardContainer: containerStub
            anchors.fill: parent
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

    // Descriptors of the shape a reader or an enclosure really reports, plus
    // the ones that only arrive when something is wrong. The two ordinary
    // cards at the end matter as much as the rest: without a row that can be
    // chosen, the check that the forbidden rows were not chosen says nothing.
    function hostileDrives() {
        return [
            // No capacity at all, and one byte of it. Both divide into the
            // capacity check and both are drawn as a figure.
            { device: fakePrefix + "zero", description: "Reader with no card",
              size: 0 },
            { device: fakePrefix + "onebyte", description: "One byte", size: 1 },

            // The top of the range the descriptor can hold. The size crosses
            // into QML as a string and comes back through parseFloat, which
            // is where a figure stops being exact.
            { device: fakePrefix + "huge", description: "Improbable array",
              size: 18446744073709551615 },
            { device: fakePrefix + "nearly", description: "Almost as improbable",
              size: 9007199254740993 },

            // Nothing to call it by. The step falls back to the device name.
            { device: fakePrefix + "nameless", description: "", size: 32000000000 },

            // A name longer than the row, one that is only spaces, and one
            // carrying the markup and direction marks a label may interpret.
            { device: fakePrefix + "longname",
              description: repeat("Mass-Storage ", 160), size: 16000000000 },
            { device: fakePrefix + "blankname", description: "      ",
              size: 16000000000 },
            { device: fakePrefix + "markup",
              description: "<b>SD</b> <img src=\"rpi-imager-chaos-beacon.png\"/> "
                           + "\u202E drac ",
              size: 8000000000 },

            // Mounted, deeply and not at all.
            { device: fakePrefix + "mounted", description: "Mounted card",
              size: 32000000000,
              mountpoints: ["/media/a", "/media/b", repeat("/deep", 40)] },

            // The rows that must not become a destination however hard the
            // storm tries: the disk the machine is running from, and media
            // with the write tab closed.
            { device: fakePrefix + "system", description: "System disk",
              size: 512000000000, isSystem: true, isUsb: false,
              isRemovable: false, mountpoints: ["/"] },
            { device: fakePrefix + "system2", description: "System disk two",
              size: 1000000000000, isSystem: true, isUsb: false,
              isRemovable: false },
            { device: fakePrefix + "readonly", description: "Locked card",
              size: 32000000000, isReadOnly: true },
            { device: fakePrefix + "readonlysys", description: "Locked system disk",
              size: 32000000000, isReadOnly: true, isSystem: true },

            // And two that are simply cards.
            { device: fakePrefix + "card1", description: "First card",
              size: 32000000000 },
            { device: fakePrefix + "card2", description: "Second card",
              size: 64000000000 }
        ]
    }

    function forbiddenNames() {
        var out = []
        var all = hostileDrives()
        for (var i = 0; i < all.length; ++i)
            if (all[i].isSystem === true || all[i].isReadOnly === true)
                out.push(all[i].description || all[i].device)
        return out
    }

    // The first lock, checked rather than trusted: a descriptor naming a real
    // device would put the storm within reach of a disk.
    function setDrives(drives) {
        for (var i = 0; i < drives.length; ++i)
            verify(drives[i].device.indexOf(fakePrefix) === 0,
                   "drive " + i + " names a device that cannot exist")
        TestDrives.set(drives)
    }

    function init() {
        TestDrives.clear()
        containerStub.selectedStorageName = ""
        containerStub.targetIsFastboot = false
        // Every file in the run shares one writer, and a file that ran
        // earlier may have left a source on it. Cleared rather than assumed,
        // or the lock below holds only when this file runs first.
        ImageWriterSingleton.setSrc("")
        ImageWriterSingleton.setDst("")
        verify(!ImageWriterSingleton.readyToWrite(),
               "no source is set, so nothing is ready to write")

        writeStarted = false
        stormFinished = false
        rowsExamined = 0
        figuresChecked = 0
        selectionsSeen = 0
        forbiddenSelections = 0

        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        waitForRendering(step)

        // A description is a device's own firmware talking. Drawn as rich
        // text, the <img> in the one above is fetched, and the fetch is the
        // disclosure. Nothing may ask for that name.
        failOnWarning(/rpi-imager-chaos-beacon/)
        failOnWarning(/TypeError/)
        failOnWarning(/ReferenceError/)
        failOnWarning(/Binding loop/)
        failOnWarning(/is not a function/)
        failOnWarning(/Unable to assign/)
    }

    function cleanup() {
        // Checked here rather than after the loop. A test function can be
        // abandoned part-way through one, and everything written after it
        // goes too, leaving a pass that stands for nothing.
        verify(stormFinished, "the storm reached the end of its loop")
        verify(rowsExamined > 0, "rows were drawn to examine (" + rowsExamined + ")")
        verify(figuresChecked > 0,
               "and their figures were read (" + figuresChecked + ")")
        verify(selectionsSeen > 0,
               "something was chosen, so refusing counts for something ("
               + selectionsSeen + ")")
        compare(forbiddenSelections, 0,
                "no system or read-only drive became the destination")
        verify(!writeStarted, "and no write began")

        TestDrives.clear()
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

        const drives = hostileDrives()
        const forbidden = forbiddenNames()
        const keys = [Qt.Key_Down, Qt.Key_Up, Qt.Key_Tab, Qt.Key_Backtab,
                      Qt.Key_Return, Qt.Key_Space, Qt.Key_Escape, Qt.Key_Home,
                      Qt.Key_End, Qt.Key_PageDown]
        setDrives(drives)

        const list = findChild(step, "storageDeviceList")
        verify(list, "found the storage list")
        tryVerify(function() { return list.count > 0 }, 5000,
                  "the rows arrived (" + list.count + ")")

        // One deliberate choice, before the random ones. The check that
        // matters here is that no system or read-only drive became the
        // destination, and that says nothing on a run where nothing was
        // chosen at all -- which a sweep of ten seeds showed is four runs in
        // ten. The guard in cleanup() caught it; this is what stops it
        // happening.
        chooseFirstSelectable(list)

        var clicks = 0
        for (var stepNo = 0; stepNo < 90; ++stepNo) {
            // The list is swapped underneath the storm every so often: a
            // reader removed while its row is under the cursor is the shape
            // that leaves a selection pointing at a row that has gone.
            if (stepNo % 30 === 29) {
                TestDrives.clear()
                wait(1)
                setDrives(drives.slice(rndInt(4)).reverse())
                wait(1)
            }

            var targets = []
            collect(step, targets, 0)
            verify(targets.length > 0, "something is on screen")

            var action = rndInt(9)
            if (action < 4) {
                var it = targets[rndInt(targets.length)]
                if (!isDestructive(it)) {
                    mouseClick(it, rndInt(Math.max(1, Math.floor(it.width))),
                                   rndInt(Math.max(1, Math.floor(it.height))))
                    ++clicks
                }
            } else if (action < 7) {
                keyClick(keys[rndInt(keys.length)])
            } else {
                step.width = 700 + rndInt(500)
                step.height = 440 + rndInt(300)
                wait(1)
            }

            examineRows(list)
            checkSelection(forbidden)
            verify(!writeStarted, "no write began")
            verify(!ImageWriterSingleton.readyToWrite(),
                   "the writer stayed unready")
        }

        stormFinished = true
        console.log("ChaosStorage seed", data.seed, "clicks", clicks,
                    "rows", rowsExamined, "figures", figuresChecked,
                    "selections", selectionsSeen)
    }

    // Click the first row a user is allowed to choose, so the storm is known
    // to have chosen something.
    //
    // Not a row named in the fixture: a view realises only what is on
    // screen, and the ordinary cards sit at the end of fifteen rows, so
    // asking for one by name found nothing at all.
    function chooseFirstSelectable(list) {
        // The rows exist in the model before the view has made any of them.
        // Scanned straight after the count arrives, every itemAtIndex is
        // null and this reported no selectable row out of thirteen.
        waitForRendering(step, 2000)
        list.positionViewAtBeginning()
        waitForRendering(step, 2000)

        for (var i = 0; i < list.count; ++i) {
            var row = list.itemAtIndex(i)
            if (!row || !row.visible || row.height <= 0)
                continue
            if (row.isSystem === true || row.isReadOnly === true)
                continue
            mouseClick(row, Math.floor(row.width / 2), Math.floor(row.height / 2))
            waitForRendering(step, 1000)
            return true
        }
        fail("no selectable row on screen to choose from " + list.count)
        return false
    }

    // Every figure on a drawn row has to be one a person could read against a
    // card. A capacity that arrives as a string and a fraction that wraps
    // both come out here, as a label saying NaN or nothing at all.
    function examineRows(list) {
        for (var i = 0; i < list.count && i < 6; ++i) {
            var row = list.itemAtIndex(i)
            if (!row || !row.visible || row.height <= 0)
                continue
            ++rowsExamined

            var labels = []
            collectText(row, labels, 0)
            for (var t = 0; t < labels.length; ++t) {
                var s = String(labels[t]).toLowerCase()
                verify(s.indexOf("nan") < 0, "a figure read as NaN: " + labels[t])
                verify(s.indexOf("infinity") < 0, "a figure ran away: " + labels[t])
                verify(s.indexOf("undefined") < 0,
                       "a label read as undefined: " + labels[t])
                ++figuresChecked
            }
        }
    }

    // The property the whole file exists for. selectDstItem() assigns this
    // name on the same branch that calls setDst, and returns before either
    // for a system disk or read-only media, so the name standing here is the
    // destination the writer was given.
    function checkSelection(forbidden) {
        var chosen = String(containerStub.selectedStorageName || "")
        if (chosen.length === 0)
            return
        ++selectionsSeen
        for (var i = 0; i < forbidden.length; ++i) {
            if (chosen === forbidden[i]) {
                ++forbiddenSelections
                console.log("ChaosStorage chose a forbidden drive:", chosen)
            }
        }
    }

    function collect(item, out, depth) {
        if (!item || depth > 12 || out.length > 250)
            return
        if (item.visible === false)
            return
        if (item.width > 0 && item.height > 0)
            out.push(item)
        var kids = item.children
        for (var i = 0; kids && i < kids.length; ++i)
            collect(kids[i], out, depth + 1)
    }

    function collectText(item, out, depth) {
        if (!item || depth > 8 || out.length > 40)
            return
        if (typeof item.text === "string" && item.text.length > 0)
            out.push(item.text)
        var kids = item.children
        for (var i = 0; kids && i < kids.length; ++i)
            collectText(kids[i], out, depth + 1)
    }

    function isDestructive(item) {
        var t = (item.text !== undefined && typeof item.text === "string")
                ? item.text.toLowerCase() : ""
        var n = (item.objectName || "").toLowerCase()
        return t.indexOf("write") >= 0 || t.indexOf("erase") >= 0
            || t.indexOf("format") >= 0 || n.indexOf("write") >= 0
            || n.indexOf("erase") >= 0 || n.indexOf("format") >= 0
    }
}
