/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Clicking an operating system in the list.
 *
 * The step's own handlers are covered elsewhere, against rows written by
 * hand. What was never covered is the delegate between the user and those
 * handlers: the press that moves focus into the list, the click that selects,
 * and the double click that selects and moves on. Every one of them was
 * uncovered because no test had ever had an OS list to click.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "OSDelegate"
    when: windowShown
    width: 900
    height: 700
    visible: true

    // Every property selectOSitem() writes. A stub missing one throws
    // part-way through, leaving the step half-configured with the assertions
    // still passing, because they are about something else.
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

    readonly property var repoPayload: ({
        "os_list": [
            {
                "name": "Test OS Alpha",
                "description": "The first entry",
                "url": "https://example.invalid/alpha.img.xz",
                "icon": "",
                "release_date": "2026-01-01",
                "extract_size": 1048576,
                "image_download_size": 524288,
                "extract_sha256": "aa11"
            },
            {
                "name": "Test OS Beta",
                "description": "The second entry",
                "url": "https://example.invalid/beta.img.xz",
                "icon": "",
                "release_date": "2026-01-02",
                "extract_size": 2097152,
                "image_download_size": 1048576,
                "extract_sha256": "bb22"
            },
            {
                "name": "Test OS Delta",
                "description": "For one board only",
                "url": "https://example.invalid/delta.img.xz",
                "icon": "",
                "release_date": "2026-01-04",
                "extract_size": 1048576,
                "image_download_size": 524288,
                "extract_sha256": "dd44",
                "devices": ["test-board-a"]
            }
        ]
    })

    property url previousRepo: ""

    function initTestCase() {
        testCase.previousRepo = ImageWriterSingleton.osListUrl()

        // The OS list belongs to the singleton writer and is shared by every
        // file in the run, so it is fetched once here rather than per case.
        ImageWriterSingleton.setHWFilterList([], false)

        const url = TestFiles.write("os_delegate_repo.json",
                                    JSON.stringify(testCase.repoPayload))
        verify(url !== "", "wrote the repository file")
        ImageWriterSingleton.refreshOsListFrom(url)

        // Waited on the writer's own filtered document rather than the
        // model: OSListModel::rowCount() is protected, so QML cannot see it,
        // and the list view does not exist until a case builds the step.
        tryVerify(function() {
            return String(ImageWriterSingleton.getFilteredOSlist())
                       .indexOf("Test OS Alpha") >= 0
        }, 10000, "the repository was fetched and parsed")
    }

    function cleanupTestCase() {
        // The repository URL goes back to whatever it was, with setCustomRepo
        // rather than a refetch: refetching the shipped URL empties the list
        // first and only refills it if the machine has network, and several
        // files later in the run fail rather than skip without a list. The
        // entries fetched here are harmless to leave -- what must not be left
        // is a repository pointing at a temporary file that goes away with
        // the run.
        ImageWriterSingleton.setCustomRepo(testCase.previousRepo)
    }

    function restoreRepository() {
        const url = TestFiles.write("os_delegate_repo.json",
                                    JSON.stringify(testCase.repoPayload))
        ImageWriterSingleton.refreshOsListFrom(url)
        tryVerify(function() {
            return String(ImageWriterSingleton.getFilteredOSlist())
                       .indexOf("Test OS Alpha") >= 0
        }, 10000, "the original repository is back")
    }

    function init() {
        fakeContainer.selectedOsName = ""
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        advanced.target = step
        advanced.clear()
        waitForRendering(step)
    }

    function cleanup() {
        if (step) {
            step.destroy()
            step = null
        }
    }

    function osList() {
        const l = findChild(step, "osList")
        verify(l, "found the operating system list")
        tryVerify(function() { return l.count >= 2 }, 5000,
                  "the list has rows to click; count is " + l.count)
        waitForRendering(step)
        return l
    }

    // Rows by name, not by position: the model appends "Erase" and "Use
    // custom" of its own, and a repository is free to be sorted.
    function rowNamed(l, wanted) {
        for (let i = 0; i < l.count; i++) {
            const item = l.itemAtIndex(i)
            if (item && item.name === wanted) {
                verify(item.visible && item.height > 0,
                       "'" + wanted + "' is on screen to be clicked")
                return item
            }
        }
        fail("no row named '" + wanted + "' in a list of " + l.count)
        return null
    }

    function hasRow(l, wanted) {
        for (let i = 0; i < l.count; i++) {
            const item = l.itemAtIndex(i)
            if (item && item.name === wanted)
                return true
        }
        return false
    }

    // -- There is a list to click ------------------------------------------

    function test_the_repository_puts_its_entries_in_the_list() {
        // Without this every case below clicks nothing and passes.
        const l = osList()
        verify(rowNamed(l, "Test OS Alpha"), "the first entry is there")
        verify(rowNamed(l, "Test OS Beta"), "the second entry is there")
    }

    // -- Clicking one -------------------------------------------------------

    function test_clicking_an_entry_chooses_that_entry() {
        const l = osList()

        mouseClick(rowNamed(l, "Test OS Beta"))

        compare(fakeContainer.selectedOsName, "Test OS Beta",
                "the row that was clicked is the one that got chosen")
        verify(step.nextButtonEnabled, "and the user can move on")
    }

    function test_clicking_the_other_entry_chooses_the_other_entry() {
        // The delegate passes its own index and its own data. Both have to be
        // the row under the pointer rather than whichever row is current.
        const l = osList()

        mouseClick(rowNamed(l, "Test OS Alpha"))

        compare(fakeContainer.selectedOsName, "Test OS Alpha")
    }

    function test_clicking_an_entry_does_not_advance_on_its_own() {
        // Only the keyboard advances on selection. A single click that
        // advanced would take the user off the list the moment they touched
        // it, with no chance to look at the other entries.
        const l = osList()

        mouseClick(rowNamed(l, "Test OS Alpha"))
        wait(150)

        compare(advanced.count, 0, "a single click chooses and stays put")
    }

    function test_pressing_a_row_gives_the_list_the_keyboard() {
        // Someone who clicks a row and then reaches for the arrow keys is
        // entitled to have them work. The press handler is what moves focus
        // into the list, and it is separate from the click that selects.
        const l = osList()
        verify(!l.activeFocus, "the list does not start with focus")

        mouseClick(rowNamed(l, "Test OS Alpha"))

        verify(l.activeFocus,
               "clicking a row leaves the keyboard in the list")
    }

    // -- The list arriving while the step is open --------------------------

    function test_an_os_list_arriving_while_the_step_is_open_fills_it() {
        // The case the two-click bug lived in. The step is on screen, the
        // model holds only "Erase" and "Use custom", and the real list lands
        // underneath the user. What has to happen is that the new entries
        // appear; what must not happen is the list being thrown away and
        // rebuilt, which is what swallowed the click.
        const l = osList()
        const before = l.count

        const payload = { "os_list": [
            { "name": "Test OS Gamma", "description": "Arrived late",
              "url": "https://example.invalid/gamma.img.xz", "icon": "",
              "release_date": "2026-01-03", "extract_size": 1048576,
              "image_download_size": 524288, "extract_sha256": "cc33" }
        ]}
        const url = TestFiles.write("os_delegate_late.json", JSON.stringify(payload))
        verify(url !== "", "wrote the second repository file")

        ImageWriterSingleton.refreshOsListFrom(url)

        tryVerify(function() { return hasRow(l, "Test OS Gamma") }, 10000,
                  "the entry that arrived is in the list; count is " + l.count)

        // And it can be chosen straight away, which is the whole point of
        // noticing it arrived.
        mouseClick(rowNamed(l, "Test OS Gamma"))
        compare(fakeContainer.selectedOsName, "Test OS Gamma")

        // Put the original entries back for the cases that come after.
        restoreRepository()
        verify(before > 0)
    }

    // -- Choosing a board narrows the list ---------------------------------

    function test_a_hardware_filter_takes_out_the_entries_it_excludes() {
        // Picking a board narrows the OS list to what will run on it. An
        // entry left in that the board cannot boot is a card that does
        // nothing when it goes in; one wrongly taken out is an OS the user
        // cannot install and has no way to ask for.
        const l = osList()
        verify(rowNamed(l, "Test OS Delta"), "the tagged entry is there")
        verify(rowNamed(l, "Test OS Alpha"), "and the untagged one")

        ImageWriterSingleton.setHWFilterList(["test-board-a"], false)

        tryVerify(function() { return !hasRow(l, "Test OS Alpha") }, 5000,
                  "an entry with no tags is not offered for a chosen board")
        verify(hasRow(l, "Test OS Delta"),
               "and the entry tagged for that board stays")

        ImageWriterSingleton.setHWFilterList([], false)
        tryVerify(function() { return hasRow(l, "Test OS Alpha") }, 5000,
                  "clearing the filter brings the rest back")
    }

    function test_a_filter_for_another_board_takes_out_the_tagged_entry_too() {
        // The other direction: an entry tagged for a board that is not the
        // one chosen has to go, or the list offers an image that will not
        // boot on the hardware in front of the user.
        const l = osList()
        verify(hasRow(l, "Test OS Delta"), "the tagged entry is there")

        ImageWriterSingleton.setHWFilterList(["test-board-b"], true)

        tryVerify(function() { return !hasRow(l, "Test OS Delta") }, 5000,
                  "an entry tagged for a different board is not offered")
        // Inclusive, so the untagged entries are still on offer -- which is
        // what stops a board with no matching images showing an empty list.
        verify(hasRow(l, "Test OS Alpha"),
               "while entries with no board tags at all remain")

        ImageWriterSingleton.setHWFilterList([], false)
        tryVerify(function() { return hasRow(l, "Test OS Delta") }, 5000,
                  "clearing the filter brings it back")
    }

    // -- Double-clicking ----------------------------------------------------

    function test_double_clicking_an_entry_chooses_it_and_moves_on() {
        const l = osList()

        mouseDoubleClickSequence(rowNamed(l, "Test OS Beta"))

        compare(fakeContainer.selectedOsName, "Test OS Beta", "it chose the OS")
        tryVerify(function() { return advanced.count === 1 }, 3000,
                  "and advanced to the next step")
    }

    // ── Keeping your place ────────────────────────────────────────────
    //
    // The OS list re-evaluates every delegate when an image finishes caching,
    // and the handler saves and restores the scroll position around that.
    // Somebody scrolled half way down looking for an entry should not lose
    // their place while they are reading, for a reason nothing on screen
    // explains.

    function longRepository(count) {
        const entries = []
        for (let i = 0; i < count; i++) {
            entries.push({
                "name": "Entry " + (i < 10 ? "0" + i : i),
                "description": "One of many, so the list is longer than the window",
                "url": "https://example.invalid/entry" + i + ".img.xz",
                "icon": "",
                "release_date": "2026-01-01",
                "extract_size": 1048576,
                "image_download_size": 524288,
                "extract_sha256": "aa" + i
            })
        }
        return { "os_list": entries }
    }

    function test_the_list_keeps_its_place_when_an_image_finishes_caching() {
        const url = TestFiles.write("os_delegate_long.json",
                                    JSON.stringify(longRepository(40)))
        ImageWriterSingleton.refreshOsListFrom(url)
        tryVerify(function () {
            return String(ImageWriterSingleton.getFilteredOSlist())
                       .indexOf("Entry 39") >= 0
        }, 10000, "the long repository was fetched")

        const l = osList()
        tryVerify(function () { return l.contentHeight > l.height }, 5000,
                  "the list is longer than the window: " + l.contentHeight
                  + " into " + l.height)

        // Somewhere in the middle, as a user reading down the list would be.
        l.contentY = Math.floor((l.contentHeight - l.height) / 2)
        waitForRendering(step)
        const readingAt = l.contentY
        verify(readingAt > 0, "the view is part-way down")

        // What the writer emits when a download finishes and the entry's
        // "cached" badge changes.
        ImageWriterSingleton.cacheStatusChanged()

        tryVerify(function () { return l.contentY === readingAt }, 3000,
                  "the view stayed where the user left it; it is at "
                  + l.contentY + " rather than " + readingAt)

        restoreRepository()
    }

    // ── Coming back out of a category ─────────────────────────────────
    //
    // Descending into a category replaces the whole list, so the way back is
    // the only way back. There is a "Go back" row for the pointer; Left is
    // the keyboard's, and it is the one that is easy to leave off.

    function test_left_comes_back_out_of_a_category() {
        const url = TestFiles.write("os_delegate_category.json",
                                    JSON.stringify({ "os_list": [{
                                        "name": "Test category",
                                        "description": "Has entries underneath it",
                                        "icon": "",
                                        "subitems": [{
                                            "name": "Inside the category",
                                            "description": "Only reachable by descending",
                                            "url": "https://example.invalid/inside.img.xz",
                                            "icon": "",
                                            "release_date": "2026-01-01",
                                            "extract_size": 1048576,
                                            "image_download_size": 524288,
                                            "extract_sha256": "cc77"
                                        }]
                                    }]}))
        ImageWriterSingleton.refreshOsListFrom(url)
        tryVerify(function () {
            return String(ImageWriterSingleton.getFilteredOSlist())
                       .indexOf("Test category") >= 0
        }, 10000, "the repository with a category was fetched")

        const swipe = findChild(step, "osCategorySwipeView")
        verify(swipe, "found the category view")
        compare(swipe.currentIndex, 0, "starting on the top-level list")

        const l = osList()
        mouseClick(rowNamed(l, "Test category"))

        tryVerify(function () { return swipe.currentIndex === 1 }, 3000,
                  "clicking a category descends into it")

        const sub = findChild(step, "osSublistView")
        verify(sub, "found the sublist")
        sub.forceActiveFocus()
        waitForRendering(step)
        keyClick(Qt.Key_Left)

        tryVerify(function () { return swipe.currentIndex === 0 }, 3000,
                  "Left came back out to the list underneath")

        restoreRepository()
    }

    // ── When the OS list did not load either ──────────────────────────
    //
    // The OS step has its own offline banner and its own Retry, on the screen
    // a user is dropped onto when the list never arrived. Unlike the device
    // screen's, this one is shown on the state of the list alone, so it can
    // be reached whatever else has run.

    function test_the_os_list_offline_banner_has_a_retry_that_works() {
        const name = "os_delegate_offline.json"
        const url = TestFiles.write(name, JSON.stringify({}))
        verify(url !== "", "wrote a repository holding nothing")
        ImageWriterSingleton.refreshOsListFrom(url)
        tryVerify(function () {
            return ImageWriterSingleton.isOsListUnavailable
        }, 10000, "the list is unavailable, as it is with no connection")

        const banner = findChild(step, "osListOfflineBanner")
        verify(banner, "found the offline banner")
        tryVerify(function () { return banner.visible }, 3000,
                  "the banner explains why the list is empty")

        const retry = findChild(step, "osListRetryButton")
        verify(retry, "and offers a way to try again")
        tryVerify(function () { return retry.visible && retry.height > 0 },
                  3000, "which is on screen to be pressed")

        // The banner appearing pushes the rest of the step down, and the
        // layout is still settling for a frame or two afterwards. A click
        // aimed at where the button was lands on nothing and the case reports
        // that Retry did not work, which is not what went wrong.
        waitForRendering(step)
        const settled = retry.mapToItem(testCase, retry.width / 2, retry.height / 2)

        // The connection comes back: the same URL, answering properly.
        verify(TestFiles.write(name, JSON.stringify(testCase.repoPayload)) !== "",
               "rewrote the repository with entries in it")

        mouseClick(testCase, settled.x, settled.y)

        tryVerify(function () {
            return !ImageWriterSingleton.isOsListUnavailable
        }, 10000, "pressing Retry fetched the list again")
        tryVerify(function () { return !banner.visible }, 5000,
                  "and the banner went away with it")

        const l = osList()
        tryVerify(function () { return hasRow(l, "Test OS Alpha") }, 5000,
                  "with the entries in the list to choose from")

        restoreRepository()
    }
}
