/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The board list, which comes from the same repository as the OS list.
 *
 * imager.devices in the repository json names every board the chooser
 * offers, with the tags that then filter the OS list, an icon URL, and a
 * description. It is as untrusted as the OS list beside it and was stormed
 * by nothing.
 *
 * The list is also rebuilt in place rather than reset, by a diff keyed on
 * the board's name -- and nothing stops a repository serving two boards with
 * one name. The rebuild indexes the incoming list by the position of the
 * live one, so a diff that disagreed with itself would read past the end.
 * The storm reloads a different list underneath the view on purpose, which
 * is the only way that path runs more than once.
 *
 * Three locks, as the other storms: the step is built against a stub, so the
 * wizard's write path does not exist here; no source or destination is ever
 * set; and no drive is ever put in the list.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosDevices"
    when: windowShown
    width: 900
    height: 700
    visible: true

    property var step: null
    property int rngState: 1
    property bool writeStarted: false
    property bool stormFinished: false
    property int rowsExamined: 0
    property int labelsChecked: 0
    property int reloadsMade: 0
    property int selectionsSeen: 0

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
        property string selectedDeviceName: ""
    }

    Item {
        id: overlayRoot
        anchors.fill: parent
    }

    Component {
        id: stepComponent
        DeviceSelectionStep {
            wizardContainer: containerStub
            anchors.fill: parent
        }
    }

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

    // A board list no repository would serve, every field still the type the
    // parser expects -- anything it rejected would never reach the chooser,
    // and the chooser is the subject.
    //
    // `variant` shifts the shape between reloads so the diff has something to
    // do: a run removed from the middle, a run added, an order changed, and
    // the same name twice throughout.
    function hostileDevices(variant) {
        var out = []

        out.push({ name: "Raspberry Pi 5", tags: ["pi5"],
                   capabilities: ["fastboot"], icon: "icons/local.png",
                   description: "the ordinary case", matching_type: "exclusive",
                   architecture: "arm64", default: true })

        // Nothing at all: every field the delegate reads is absent.
        out.push({})

        // The same name twice. The rebuild is keyed on it.
        out.push({ name: "Twice Over", description: "first of two",
                   tags: ["a"], architecture: "arm64" })
        out.push({ name: "Twice Over", description: "second of two",
                   tags: ["b"], architecture: "armhf" })

        // Longer than the row, and nothing but spaces.
        out.push({ name: repeat("a-very-long-board-name-", 90),
                   description: repeat("and a description to match ", 60),
                   tags: [], architecture: "arm64" })
        out.push({ name: "      ", description: "      ", tags: [] })

        // Markup, and a direction mark. Drawn as rich text, the image is
        // fetched; failOnWarning below says it must not be.
        out.push({ name: "<b>Pi</b> <img src=\"rpi-imager-device-beacon.png\"/>",
                   description: "\u202E noitpircsed <i>markup</i>",
                   tags: ["x"], architecture: "arm64" })

        // Fields that are the wrong shape but still valid json: a string
        // where an array belongs, and an array of objects where strings do.
        out.push({ name: "Wrong Shapes", tags: "not-an-array",
                   capabilities: [ { nested: true }, ["deeper"] ],
                   description: "types the reader did not ask for",
                   architecture: 42, matching_type: [] })

        // Icons through every form the sanitiser has an opinion about.
        const icons = ["file://a-host/share/board.png", "data:image/png;base64,AAAA",
                       "ftp://example.invalid/board.png", "icons/local.png",
                       "https://example.invalid/board.png", "",
                       // Schemes are case-insensitive, and the routing used to
                       // match "https://" as text, so this one validated as
                       // remote and then went to Image.source unrouted.
                       "HTTPS://example.invalid/board.png"]
        for (var i = 0; i < icons.length; ++i)
            out.push({ name: "Icon Form " + i, icon: icons[i],
                       description: "icon " + i, tags: ["icon"],
                       architecture: "arm64" })

        // Two more claiming to be the default.
        out.push({ name: "Default Two", description: "also default",
                   default: true, tags: [], architecture: "arm64" })
        out.push({ name: "Default Three", description: "and another",
                   default: true, tags: [], architecture: "arm64" })

        // Enough to scroll, with the run that moves between variants sitting
        // in the middle of them.
        for (var j = 0; j < 120; ++j) {
            if (variant === 1 && j >= 40 && j < 60)
                continue                       // a run taken out of the middle
            out.push({ name: "Board " + (variant === 2 ? (119 - j) : j),
                       description: "one of many",
                       tags: [ "t" + (j % 7) ],
                       capabilities: (j % 4 === 0) ? ["fastboot"] : [],
                       icon: "icons/local.png",
                       architecture: (j % 2) ? "arm64" : "armhf" })
        }

        if (variant === 2) {
            out.push({ name: "Appended Late", description: "only in variant two",
                       tags: [], architecture: "arm64" })
        }

        return out
    }

    function repositoryJson(variant) {
        return JSON.stringify({
            imager: { devices: hostileDevices(variant) },
            os_list: [
                { name: "An operating system", description: "for the chooser",
                  url: "https://example.invalid/a.img.xz",
                  image_download_size: 1, extract_size: 1,
                  init_format: "systemd", devices: ["pi5"] },
                { name: "Another", description: "so the list is not empty",
                  url: "https://example.invalid/b.img.xz",
                  image_download_size: 1, extract_size: 1, init_format: "none" }
            ]
        })
    }

    function loadVariant(variant) {
        const url = TestFiles.write("hostile-devices-" + variant + ".json",
                                    repositoryJson(variant))
        verify(url.length > 0, "the repository was written")
        ImageWriterSingleton.refreshOsListFrom(url)
        ++reloadsMade
    }

    function init() {
        TestDrives.clear()
        compare(TestDrives.count(), 0, "no drive while the board list is stormed")
        ImageWriterSingleton.setSrc("")
        ImageWriterSingleton.setDst("")
        verify(!ImageWriterSingleton.readyToWrite(), "the writer is not ready")

        containerStub.selectedDeviceName = ""
        writeStarted = false
        stormFinished = false
        rowsExamined = 0
        labelsChecked = 0
        reloadsMade = 0
        selectionsSeen = 0

        // A board name is repository text. Drawn as rich text the image in
        // the one above is fetched, and the fetch is the disclosure.
        failOnWarning(/rpi-imager-device-beacon/)
        failOnWarning(/TypeError/)
        failOnWarning(/ReferenceError/)
        failOnWarning(/Binding loop/)
        failOnWarning(/is not a function/)
        failOnWarning(/Unable to assign/)
    }

    function cleanup() {
        // Read here rather than after the loop: a test function can be
        // abandoned part-way through one, and every check written after it
        // goes too, leaving a pass that stands for nothing.
        verify(stormFinished, "the storm reached the end of its loop")
        verify(rowsExamined > 0, "rows were drawn to examine (" + rowsExamined + ")")
        verify(labelsChecked > 0, "and their labels read (" + labelsChecked + ")")
        verify(reloadsMade >= 4,
               "the list was rebuilt underneath the view (" + reloadsMade + ")")
        verify(selectionsSeen > 0,
               "and a board was chosen (" + selectionsSeen + ")")
        verify(!writeStarted, "no write began")

        if (step) {
            step.destroy()
            step = null
        }
        TestDrives.clear()
        ImageWriterSingleton.setSrc("")
        ImageWriterSingleton.setDst("")
    }

    function cleanupTestCase() {
        // Put the repository back, or every file after this one reads the
        // hostile list: one ImageWriter is shared by all of them.
        //
        // Emptying it first is the part that matters. reload() returns early
        // when "devices" is not an array and leaves the rows it already has,
        // so a file that later writes {} to make the list unavailable finds
        // a hundred and twenty boards still sitting there and its offline
        // screen never appears.
        const empty = TestFiles.write("chaos-devices-empty.json",
                                      JSON.stringify({ imager: { devices: [] },
                                                       os_list: [] }))
        if (empty.length > 0) {
            ImageWriterSingleton.refreshOsListFrom(empty)
            tryVerify(function () {
                return ImageWriterSingleton.isOsListUnavailable
            }, 10000, "the board list was emptied")
        }
        ImageWriterSingleton.refreshOsListFromDefaultUrl()
    }

    function test_storm_data() {
        var override = TestEnv.value("RPI_CHAOS_SEED")
        if (override.length > 0)
            return [ { tag: "seed-" + override, seed: parseInt(override) } ]
        return [ { tag: "seed-31", seed: 31 }, { tag: "seed-6101", seed: 6101 } ]
    }

    function test_storm(data) {
        rngState = data.seed

        loadVariant(0)
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        waitForRendering(step)

        const list = step.hwlist
        verify(list, "the step exposes its list")
        tryVerify(function() { return list.count > 0 }, 8000,
                  "the boards arrived (" + list.count + ")")

        const keys = [Qt.Key_Down, Qt.Key_Up, Qt.Key_Tab, Qt.Key_Backtab,
                      Qt.Key_Return, Qt.Key_Space, Qt.Key_Escape, Qt.Key_Home,
                      Qt.Key_End, Qt.Key_PageDown, Qt.Key_PageUp]
        var clicks = 0

        for (var stepNo = 0; stepNo < 90; ++stepNo) {
            // Swap the list for a differently shaped one. This is the only
            // way the rebuild runs at all, and the only way it runs against
            // a list holding the same name twice.
            if (stepNo % 20 === 19) {
                loadVariant(1 + (stepNo / 20) % 2)
                tryVerify(function() { return list.count > 0 }, 8000,
                          "the boards came back")
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
            if (String(containerStub.selectedDeviceName || "").length > 0)
                ++selectionsSeen
            verify(!writeStarted, "no write began")
            compare(TestDrives.count(), 0, "the drive list stayed empty")
            verify(!ImageWriterSingleton.readyToWrite(), "the writer stayed unready")
        }

        // A deterministic choice, so the guard below answers for this seed
        // rather than for whichever other one happened to land a click on a
        // row. Seed 5441 swept clean in every respect except that the walk
        // never chose a board, and a counter totalled across seeds would have
        // let the other twenty-nine cover for it.
        //
        // It still asks the same question: if clicking a row stops setting
        // the name, this finds nothing to count and the guard fires.
        if (selectionsSeen === 0 && list.count > 0) {
            var firstRow = list.itemAtIndex(0)
            if (firstRow && firstRow.visible && firstRow.height > 0) {
                mouseClick(firstRow, Math.floor(firstRow.width / 2),
                                     Math.floor(firstRow.height / 2))
                if (String(containerStub.selectedDeviceName || "").length > 0)
                    ++selectionsSeen
            }
        }

        stormFinished = true
        console.log("ChaosDevices seed", data.seed, "clicks", clicks,
                    "rows", rowsExamined, "labels", labelsChecked,
                    "reloads", reloadsMade, "selections", selectionsSeen)
    }

    // Whatever the repository said, what is drawn has to be readable text.
    // A field of the wrong type reaches the delegate as undefined and a size
    // that did not survive its conversion as NaN.
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
                verify(s.indexOf("nan") < 0, "a label read as NaN: " + labels[t])
                verify(s.indexOf("infinity") < 0, "a label ran away: " + labels[t])
                verify(s.indexOf("undefined") < 0,
                       "a label read as undefined: " + labels[t])
                ++labelsChecked
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
