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
 *
 * That is worth fixing for more than the count. This is the list users
 * reported needing two clicks to select from -- the model used to rebuild
 * itself as the real list arrived, destroying the delegate mid-click, and Qt
 * delivers a click only when press and release reach the same item. The fix
 * lives in OSListModel; the cases that pin it are C++. These are the other
 * half: that a click on a row, through the delegate, chooses that row.
 *
 * The list is real, and arrives the way a custom repository arrives. The file
 * written here is handed to refreshOsListFrom(), which is the call the
 * Repository dialog makes, so the json is fetched, filtered, parsed and
 * turned into rows by the shipping path rather than by a stand-in model. A
 * file:// repository is a supported thing to point Imager at -- it is what
 * --repo takes -- so nothing here is a test-only route.
 *
 * All three handlers carry weight: dropping the selection from the click
 * fails two cases, making the double click do nothing fails one, and taking
 * the focus move out of the press fails one.
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

    function initTestCase() {
        // The OS list belongs to the singleton writer and is shared by every
        // file in the run, so it is fetched once here rather than per case.
        // No hardware filtering, which is a real state -- it is what the
        // device chooser's "no filtering" option sets, and what holds before
        // a board is picked. It has to be said out loud: the filter belongs
        // to the singleton writer, so whichever file ran before this one may
        // have left a board selected, and entries carrying no hardware tags
        // are filtered out of a filtered list. That is exactly what happened
        // -- these cases passed on their own and the file failed in the suite,
        // with the repository fetched and every row dropped on the way to the
        // model.
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
        // Put the writer back on the list it would otherwise have had. This
        // starts a real fetch, which is what the application does at startup
        // anyway -- what must not be left behind is a repository pointing at
        // a temporary file that goes away with the run.
        ImageWriterSingleton.refreshOsListFromDefaultUrl()
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
        //
        // The two halves are decided differently. An entry carrying "devices"
        // stays only if one of its tags is in the filter. An entry carrying
        // none is kept or dropped by the inclusive flag alone -- inclusive
        // means "keep the untagged ones", which reads the other way round
        // until you have seen it fail.
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
}
