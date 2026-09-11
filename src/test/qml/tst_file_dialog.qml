/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * ImFileDialog's path handling.
 *
 * Seven callers: choosing a custom image on the OS step, picking a signing
 * key, an SSH public key, a repository file, a save location. All of them
 * end up handing a path to something that opens it, so a path that comes
 * back subtly wrong is a file that cannot be opened, or the wrong one.
 *
 * The functions below are the whole of the path logic. They are pure, so
 * they can be driven directly rather than through the file browser.
 */

import QtCore
import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ImFileDialog"
    when: windowShown
    width: 600
    height: 400
    visible: true

    Component {
        id: dialogComponent
        ImFileDialog {}
    }

    function create(props) {
        const d = createTemporaryObject(dialogComponent, testCase, props)
        verify(d, "the dialog was created")
        return d
    }

    // -- Turning a filter list into globs ----------------------------------

    function test_a_filter_yields_the_globs_inside_its_brackets() {
        const d = create({})
        compare(d._extractGlobs(["Images (*.png *.jpg)"]), ["*.png", "*.jpg"])
    }

    function test_several_filters_are_all_collected() {
        const d = create({})
        compare(d._extractGlobs(["Disk images (*.img *.iso)", "Archives (*.zip)"]),
                ["*.img", "*.iso", "*.zip"])
    }

    function test_a_bare_glob_with_no_brackets_is_taken_as_is() {
        const d = create({})
        compare(d._extractGlobs(["*.img"]), ["*.img"])
    }

    function test_a_filter_with_no_glob_at_all_is_ignored() {
        const d = create({})
        compare(d._extractGlobs(["Nonsense"]), ["*"],
                "and the everything glob is the fallback")
    }

    function test_an_empty_filter_list_matches_everything() {
        const d = create({})
        compare(d._extractGlobs([]), ["*"])
    }

    function test_extra_whitespace_between_globs_is_not_a_glob() {
        const d = create({})
        compare(d._extractGlobs(["Images (*.png    *.jpg)"]), ["*.png", "*.jpg"])
    }

    // -- The image globs actually enforced ---------------------------------

    function test_specific_globs_are_kept() {
        const d = create({})
        compare(d._getImageGlobs(["Disk images (*.img)"]), ["*.img"])
    }

    function test_all_files_falls_back_to_the_image_formats() {
        // Deliberate: the picker enforces image extensions rather than
        // letting someone select a document and have the write fail later.
        const d = create({})
        const globs = d._getImageGlobs(["All files (*)"])
        verify(globs.indexOf("*") === -1, "the everything glob is dropped")
        verify(globs.indexOf("*.img") >= 0)
        verify(globs.indexOf("*.xz") >= 0)
        verify(globs.indexOf("*.zst") >= 0)
    }

    function test_no_filters_falls_back_to_the_image_formats() {
        const d = create({})
        verify(d._getImageGlobs([]).indexOf("*.img") >= 0)
    }

    // -- Recognising a path as a file rather than a folder -----------------

    function test_a_path_with_an_extension_is_a_file() {
        const d = create({})
        verify(d._looksLikeFilePath("/home/pi/os.img"))
        verify(d._looksLikeFilePath("os.img"))
    }

    function test_a_path_ending_in_a_slash_is_a_folder() {
        const d = create({})
        verify(!d._looksLikeFilePath("/home/pi/"))
    }

    function test_a_path_with_no_extension_is_a_folder() {
        const d = create({})
        verify(!d._looksLikeFilePath("/home/pi/images"))
    }

    function test_an_empty_path_is_not_a_file() {
        const d = create({})
        verify(!d._looksLikeFilePath(""))
        verify(!d._looksLikeFilePath("   "))
    }

    function test_a_dotfile_is_not_mistaken_for_an_extension() {
        // ".bashrc" is a leading dot, not a name plus extension.
        const d = create({})
        verify(!d._looksLikeFilePath("/home/pi/.bashrc"))
    }

    // -- Path to URL and back ----------------------------------------------

    function test_an_absolute_path_becomes_a_file_url() {
        const d = create({})
        compare(d._toFileUrl("/home/pi/os.img"), "file:///home/pi/os.img")
    }

    function test_a_url_is_left_alone() {
        const d = create({})
        compare(d._toFileUrl("file:///home/pi/os.img"), "file:///home/pi/os.img")
    }

    function test_an_empty_path_yields_nothing() {
        const d = create({})
        compare(d._toFileUrl(""), "")
    }

    function test_surrounding_whitespace_is_trimmed() {
        // Paths get pasted, and a trailing newline off a terminal is common.
        const d = create({})
        compare(d._toFileUrl("  /home/pi/os.img  "), "file:///home/pi/os.img")
    }

    function test_a_tilde_expands_to_the_home_directory() {
        const d = create({})
        const home = String(StandardPaths.writableLocation(StandardPaths.HomeLocation))
        const homePath = home.indexOf("file://") === 0 ? home.substring(7) : home

        compare(d._toFileUrl("~"), "file://" + homePath)
        compare(d._toFileUrl("~/os.img"), "file://" + homePath + "/os.img")
    }

    function test_a_url_becomes_a_display_path() {
        const d = create({})
        compare(d._toDisplayPath("file:///home/pi/os.img"), "/home/pi/os.img")
    }

    function test_the_root_url_displays_as_a_single_slash() {
        const d = create({})
        compare(d._toDisplayPath("file://"), "/")
        compare(d._toDisplayPath("file:///"), "/")
    }

    function test_a_plain_path_passes_through_unchanged() {
        const d = create({})
        compare(d._toDisplayPath("/home/pi"), "/home/pi")
    }

    // -- Walking up the tree -----------------------------------------------

    function test_the_parent_of_a_folder_is_the_one_above_it() {
        const d = create({})
        compare(String(d._parentUrl("file:///home/pi/images")), "file:///home/pi")
        compare(String(d._parentUrl("file:///home/pi")), "file:///home")
    }

    function test_the_parent_of_a_top_level_folder_is_root() {
        const d = create({})
        compare(String(d._parentUrl("file:///home")), "file:///")
    }

    function test_root_is_its_own_parent() {
        const d = create({})
        compare(String(d._parentUrl("file:///")), "file:///")
    }

    function test_trailing_slashes_do_not_change_the_parent() {
        const d = create({})
        compare(String(d._parentUrl("file:///home/pi/")), "file:///home")
        compare(String(d._parentUrl("file:///home/pi///")), "file:///home")
    }

    function test_root_is_recognised_however_it_is_written() {
        const d = create({})
        verify(d._isRoot("file:///"))
        verify(d._isRoot("file:////"))
        verify(d._isRoot("/"))
        verify(!d._isRoot("file:///home"))
        verify(!d._isRoot("/home"))
    }

    function test_going_up_stops_at_root() {
        const d = create({ currentFolder: "file:///home/pi" })
        verify(d._canGoUp())

        d._goUp()
        compare(String(d.currentFolder), "file:///home")
        d._goUp()
        compare(String(d.currentFolder), "file:///")

        verify(!d._canGoUp(), "there is nothing above root")
        d._goUp()
        compare(String(d.currentFolder), "file:///", "and asking again is harmless")
    }

    // -- Building the path a caller is handed ------------------------------

    function test_the_built_path_joins_the_folder_and_the_name() {
        const d = create({ currentFolder: "file:///home/pi" })
        d._currentFilename = "os.img"
        compare(d._buildFilePath(), "/home/pi/os.img")
    }

    function test_a_folder_that_already_ends_in_a_slash_gains_no_second_one() {
        const d = create({ currentFolder: "file:///home/pi/" })
        d._currentFilename = "os.img"
        compare(d._buildFilePath(), "/home/pi/os.img")
    }

    function test_saving_into_root_builds_a_single_slash_path() {
        const d = create({ currentFolder: "file:///" })
        d._currentFilename = "os.img"
        compare(d._buildFilePath(), "/os.img")
    }

    function test_no_filename_builds_nothing() {
        const d = create({ currentFolder: "file:///home/pi" })
        d._currentFilename = ""
        compare(d._buildFilePath(), "")

        d._currentFilename = "   "
        compare(d._buildFilePath(), "", "and whitespace is not a filename")
    }

    function test_the_filename_is_trimmed_into_the_path() {
        const d = create({ currentFolder: "file:///home/pi" })
        d._currentFilename = "  os.img  "
        compare(d._buildFilePath(), "/home/pi/os.img")
    }

    // -- Folder names people actually have ---------------------------------
    //
    // A URL escapes what a path does not, and _toDisplayPath() strips the
    // scheme with a substring rather than decoding. Whether the two
    // round-trip decides whether a folder with a space in its name can be
    // navigated into and saved to at all, and "Raspberry Pi images" is a
    // name somebody has. They do round-trip today, because QML's url to
    // string conversion does not percent-encode these. That is worth
    // holding still: switching to an encoded form anywhere upstream would
    // put %20 in front of the user and into the path handed to the writer.

    function test_a_folder_name_with_a_space_survives_the_round_trip() {
        const d = create({})
        const url = Qt.resolvedUrl("file:///home/pi/Raspberry Pi images")
        compare(d._toDisplayPath(url), "/home/pi/Raspberry Pi images")
    }

    function test_a_built_path_under_a_folder_with_a_space_is_usable() {
        const d = create({ currentFolder: "file:///home/pi/Raspberry Pi images" })
        d._currentFilename = "os.img"
        compare(d._buildFilePath(), "/home/pi/Raspberry Pi images/os.img")
    }

    function test_a_folder_name_with_a_hash_survives_the_round_trip() {
        const d = create({})
        const url = Qt.resolvedUrl("file:///home/pi/images #2")
        compare(d._toDisplayPath(url), "/home/pi/images #2")
    }

    function test_going_up_out_of_a_folder_with_a_space_works() {
        const d = create({ currentFolder: "file:///home/pi/Raspberry Pi images" })
        d._goUp()
        compare(d._toDisplayPath(d.currentFolder), "/home/pi")
    }

    // ── Typing a path instead of browsing to it ───────────────────────
    //
    // The path field is the way through this dialog without a mouse, which
    // on an embedded build is the only way. What Return does there depends
    // on what was typed: a file is chosen and the dialog closes, a
    // directory is opened. Getting that the wrong way round either refuses
    // to accept a file the user named, or "chooses" a directory and hands
    // whatever opens it a path to something that is not a file.

    function openedDialog(props) {
        const d = createTemporaryObject(dialogComponent, testCase, props)
        verify(d, "the dialog was created")
        d.open()
        tryVerify(function () { return d.visible }, 3000, "the dialog opened")
        return d
    }

    function pathFieldOf(d) {
        const f = findChild(d, "fileDialogPathField")
        verify(f, "found the path field")
        return f
    }

    function test_typing_a_file_path_chooses_it() {
        const dir = StandardPaths.writableLocation(StandardPaths.TempLocation)
        const d = openedDialog({})
        const spy = createTemporaryQmlObject(
            'import QtTest; SignalSpy {}', testCase)
        spy.target = d
        spy.signalName = "accepted"

        const field = pathFieldOf(d)
        field.text = String(dir).replace("file://", "") + "/some-image.img"
        field.accepted()

        tryVerify(function () { return spy.count === 1 }, 3000,
                  "the dialog accepted")
        // Waited for rather than asserted outright: this is a Popup, and
        // clearing visible starts an exit transition rather than finishing
        // one.
        tryVerify(function () { return !d.visible }, 3000, "and closed")
        verify(String(d.selectedFile).indexOf("some-image.img") !== -1,
               "with the file that was typed: " + d.selectedFile)
    }

    function test_typing_a_directory_navigates_into_it() {
        // Not an acceptance: the user asked to go somewhere, not to choose
        // the somewhere.
        const dir = String(StandardPaths.writableLocation(
            StandardPaths.TempLocation)).replace("file://", "")
        const d = openedDialog({})
        const spy = createTemporaryQmlObject(
            'import QtTest; SignalSpy {}', testCase)
        spy.target = d
        spy.signalName = "accepted"

        const field = pathFieldOf(d)
        field.text = dir
        field.accepted()

        wait(200)
        compare(spy.count, 0, "nothing was chosen")
        verify(d.visible, "the dialog is still open")
        verify(String(d.currentFolder).indexOf(dir) !== -1,
               "and it moved there: " + d.currentFolder)
    }

    function test_typing_nothing_does_nothing() {
        // An empty field must not resolve to the current folder and be
        // accepted as a choice.
        const d = openedDialog({})
        const spy = createTemporaryQmlObject(
            'import QtTest; SignalSpy {}', testCase)
        spy.target = d
        spy.signalName = "accepted"

        const field = pathFieldOf(d)
        field.text = ""
        field.accepted()

        wait(200)
        compare(spy.count, 0)
        verify(d.visible)
    }

    // ── Naming a file to save ─────────────────────────────────────────

    function test_naming_a_file_to_save_accepts_it() {
        const d = openedDialog({ isSaveDialog: true, suggestedFilename: "report.json" })
        const spy = createTemporaryQmlObject(
            'import QtTest; SignalSpy {}', testCase)
        spy.target = d
        spy.signalName = "accepted"

        const field = findChild(d, "fileDialogFilenameField")
        verify(field, "found the filename field")
        field.text = "chosen-name.json"
        field.accepted()

        tryVerify(function () { return spy.count === 1 }, 3000)
        verify(String(d.selectedFile).indexOf("chosen-name.json") !== -1,
               "the name given is the name used: " + d.selectedFile)
    }

    function test_saving_with_no_name_does_not_accept() {
        // The path would otherwise end in a slash, and whatever opens it
        // would be handed a directory to write a file into.
        const d = openedDialog({ isSaveDialog: true, suggestedFilename: "report.json" })
        const spy = createTemporaryQmlObject(
            'import QtTest; SignalSpy {}', testCase)
        spy.target = d
        spy.signalName = "accepted"

        const field = findChild(d, "fileDialogFilenameField")
        verify(field)
        field.text = ""
        field.accepted()

        wait(200)
        compare(spy.count, 0, "nothing was accepted")
        verify(d.visible, "and the dialog waits for a name")
    }

    function test_a_save_dialog_opens_with_the_suggested_name() {
        // Offered rather than left blank, so Return is a sensible default
        // and the user is not made to invent a filename.
        const d = openedDialog({ isSaveDialog: true, suggestedFilename: "performance.json" })

        compare(d._currentFilename, "performance.json")
    }
}
