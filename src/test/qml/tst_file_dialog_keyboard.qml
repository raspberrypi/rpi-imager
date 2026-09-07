/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Choosing a custom image without a mouse.
 *
 * This is the file picker the application falls back to wherever there is no
 * native one -- the embedded build, and any desktop whose portal is not
 * reachable. It is how "Use custom" gets an image on those machines, so it
 * is the whole of that feature there rather than a nicety.
 *
 * Its keyboard navigation was almost entirely uncovered: twenty-five
 * handlers, being the arrow keys, Enter and Return on each of three lists,
 * plus the buttons. A keyboard user with none of that has a dialog they can
 * tab into and cannot use -- no way to move down the list, no way to open a
 * folder, no way to accept -- and no way out except the window manager.
 *
 * Two properties get most of the attention here.
 *
 * The first is that the arrow keys stop at the ends. Every one of these
 * lists starts with nothing selected, at index -1, and the guard on Up is
 * what stops the selection going back there: a user who presses Up once too
 * often would find their selection gone and Enter doing nothing, with no
 * indication why.
 *
 * The second is that Enter and Return are the same key to a user. They are
 * two keys to Qt -- Return is the main one, Enter the numeric keypad -- and
 * each list carries a separate handler for each. One of the pair being
 * dropped is invisible to anyone who tests with the other.
 *
 * The folder used is the copied module directory, which is a real directory
 * with real subdirectories and files in it wherever the suite runs.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "FileDialogKeyboard"
    when: windowShown
    width: 900
    height: 700
    visible: true

    Component {
        id: dialogComponent
        ImFileDialog {
            // The module directory holds .qml files, so this shows both the
            // files and the folders it contains.
            nameFilters: ["QML files (*.qml)"]
        }
    }

    property var dialog: null

    SignalSpy { id: accepted; signalName: "accepted" }
    SignalSpy { id: rejected; signalName: "rejected" }

    function init() {
        dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog, "the dialog was created")
        accepted.target = dialog
        rejected.target = dialog
        accepted.clear()
        rejected.clear()
        dialog.currentFolder = __qmlModuleRoot
        dialog.open()
        tryVerify(function () { return dialog.opened }, 4000, "the dialog opened")
        // FolderListModel scans asynchronously, and it only scans at all
        // while the dialog is visible.
        tryVerify(function () { return files().count > 0 }, 5000,
                  "the folder was read and has files in it")
        tryVerify(function () { return folders().count > 0 }, 5000,
                  "and subfolders")
        waitForRendering(testCase)
    }

    function cleanup() {
        if (dialog)
            dialog.close()
    }

    function child(name) {
        const c = findChild(dialog, name)
        verify(c, "found " + name)
        return c
    }

    function places() { return child("fileDialogPlacesList") }
    function folders() { return child("fileDialogFoldersList") }
    function files() { return child("fileDialogFilesList") }

    // -- Tabbing into a list picks something ------------------------------

    function test_focusing_a_list_selects_its_first_row_data() {
        return [
            { tag: "the places list",  name: "fileDialogPlacesList" },
            { tag: "the folder list",  name: "fileDialogFoldersList" },
            { tag: "the file list",    name: "fileDialogFilesList" }
        ]
    }

    function test_focusing_a_list_selects_its_first_row(data) {
        // Each list starts at -1 deliberately, so nothing is highlighted
        // before the user goes near it. Once they tab in, something has to
        // be: the arrow keys and Enter all do nothing at -1, so a list that
        // takes focus without selecting is a list that ignores every key.
        const list = child(data.name)
        if (list.count === 0) {
            skip(data.tag + " is empty here, so there is nothing to select")
            return
        }
        compare(list.currentIndex, -1, "nothing is selected to begin with")

        list.forceActiveFocus()
        waitForRendering(testCase)

        compare(list.currentIndex, 0, data.tag + " selected its first row")
    }

    function test_focusing_the_file_list_also_marks_the_file_chosen() {
        // The file list carries the dialog's answer, so selecting a row
        // there has to set it -- otherwise Open stays disabled and Enter
        // does nothing on a list that looks like it has a selection.
        dialog.selectedFile = ""

        files().forceActiveFocus()
        waitForRendering(testCase)

        verify(String(dialog.selectedFile).length > 0,
               "the highlighted file is the chosen one")
    }

    // -- The arrow keys ---------------------------------------------------

    function test_down_and_up_move_the_selection_data() {
        return [
            { tag: "the places list",  name: "fileDialogPlacesList" },
            { tag: "the folder list",  name: "fileDialogFoldersList" },
            { tag: "the file list",    name: "fileDialogFilesList" }
        ]
    }

    function test_down_and_up_move_the_selection(data) {
        const list = child(data.name)
        if (list.count < 2) {
            skip(data.tag + " has fewer than two rows here, so moving "
                 + "between them cannot be told from not moving")
            return
        }
        list.forceActiveFocus()
        verify(list.activeFocus, data.tag + " has focus")
        compare(list.currentIndex, 0)

        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 1, "down moved to the next row")

        keyClick(Qt.Key_Up)
        compare(list.currentIndex, 0, "and up came back")
    }

    function test_up_at_the_top_keeps_the_selection_data() {
        return [
            { tag: "the places list",  name: "fileDialogPlacesList" },
            { tag: "the folder list",  name: "fileDialogFoldersList" },
            { tag: "the file list",    name: "fileDialogFilesList" }
        ]
    }

    function test_up_at_the_top_keeps_the_selection(data) {
        // The one that costs the user their place. Without the guard the
        // index goes to -1, which is the same state as never having chosen
        // anything: the highlight disappears and Enter stops working.
        const list = child(data.name)
        if (list.count === 0) {
            skip(data.tag + " is empty here")
            return
        }
        list.forceActiveFocus()
        compare(list.currentIndex, 0)

        keyClick(Qt.Key_Up)
        keyClick(Qt.Key_Up)

        compare(list.currentIndex, 0,
                data.tag + " kept its first row selected rather than "
                + "dropping back to nothing")
    }

    function test_down_at_the_bottom_keeps_the_selection_data() {
        return [
            { tag: "the places list",  name: "fileDialogPlacesList" },
            { tag: "the folder list",  name: "fileDialogFoldersList" },
            { tag: "the file list",    name: "fileDialogFilesList" }
        ]
    }

    function test_down_at_the_bottom_keeps_the_selection(data) {
        const list = child(data.name)
        if (list.count === 0) {
            skip(data.tag + " is empty here")
            return
        }
        list.forceActiveFocus()
        const last = list.count - 1
        for (let i = 0; i < list.count + 3; i++)
            keyClick(Qt.Key_Down)

        compare(list.currentIndex, last,
                data.tag + " stopped at its last row")
    }

    // -- Enter and Return are the same key to a user ----------------------

    function test_opening_a_folder_from_the_folder_list_data() {
        return [
            { tag: "Return", key: Qt.Key_Return },
            { tag: "Enter",  key: Qt.Key_Enter }
        ]
    }

    function test_opening_a_folder_from_the_folder_list(data) {
        // Without this there is no way to move into a subfolder at all,
        // which for a user whose image is not in their home directory means
        // the dialog cannot reach it.
        const before = String(dialog.currentFolder)
        folders().forceActiveFocus()
        compare(folders().currentIndex, 0)

        keyClick(data.key)
        waitForRendering(testCase)

        verify(String(dialog.currentFolder) !== before,
               data.tag + " opened the folder; still at " + before)
    }

    function test_opening_a_place_from_the_places_list_data() {
        return [
            { tag: "Return", key: Qt.Key_Return },
            { tag: "Enter",  key: Qt.Key_Enter }
        ]
    }

    function test_opening_a_place_from_the_places_list(data) {
        const before = String(dialog.currentFolder)
        places().forceActiveFocus()
        compare(places().currentIndex, 0)

        keyClick(data.key)
        waitForRendering(testCase)

        verify(String(dialog.currentFolder) !== before,
               data.tag + " went to the place; still at " + before)
    }

    function test_accepting_a_file_from_the_file_list_data() {
        return [
            { tag: "Return", key: Qt.Key_Return },
            { tag: "Enter",  key: Qt.Key_Enter }
        ]
    }

    function test_accepting_a_file_from_the_file_list(data) {
        // The end of the whole exercise: a chosen file handed back to the
        // step that asked.
        files().forceActiveFocus()
        waitForRendering(testCase)
        verify(String(dialog.selectedFile).length > 0, "a file is selected")

        keyClick(data.key)

        compare(accepted.count, 1, data.tag + " accepted the dialog")
        compare(rejected.count, 0, "and did not refuse it")
        tryVerify(function () { return !dialog.visible }, 3000,
                  "and put the dialog away")
    }

    // -- Going up ---------------------------------------------------------

    function test_going_up_moves_to_the_parent_folder() {
        const before = String(dialog.currentFolder)
        verify(child("fileDialogUpEntry").visible,
               "there is somewhere above here to go")

        child("fileDialogUpEntry").clicked()
        waitForRendering(testCase)

        const after = String(dialog.currentFolder)
        verify(after !== before, "the folder changed")
        verify(before.indexOf(after) === 0,
               "and the new one is above the old: " + after + " then " + before)
    }

    function test_there_is_nowhere_above_the_root() {
        // Offering it there would either do nothing or walk off the top of
        // the path arithmetic.
        dialog.currentFolder = "file:///"
        waitForRendering(testCase)

        verify(!child("fileDialogUpEntry").visible)
    }

    // -- The two answers --------------------------------------------------

    function test_open_hands_back_the_chosen_file() {
        files().forceActiveFocus()
        waitForRendering(testCase)
        const chosen = String(dialog.selectedFile)
        verify(chosen.length > 0)

        child("fileDialogOpenButton").clicked()

        compare(accepted.count, 1)
        compare(String(dialog.selectedFile), chosen,
                "and it is still the file that was chosen")
    }

    function test_open_is_refused_until_something_is_chosen() {
        // Accepting with nothing selected would hand the step an empty
        // path, which it would take as an image and fail on later.
        dialog.selectedFile = ""
        child("fileDialogPathField").text = ""
        waitForRendering(testCase)

        verify(!child("fileDialogOpenButton").enabled)
    }

    function test_cancel_refuses_and_chooses_nothing() {
        files().forceActiveFocus()
        waitForRendering(testCase)

        child("fileDialogCancelButton").clicked()

        compare(rejected.count, 1, "the dialog reported a refusal")
        compare(accepted.count, 0)
        tryVerify(function () { return !dialog.visible }, 3000)
    }

    function test_escape_refuses_as_well() {
        // A dialog that can only be left by choosing something is a trap,
        // and this one is raised by a click that a user may not have meant.
        dialog.escapePressed()

        compare(rejected.count, 1)
        compare(accepted.count, 0)
        tryVerify(function () { return !dialog.visible }, 3000)
    }
}
