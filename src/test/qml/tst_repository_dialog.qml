/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * RepositoryDialog: where the source of every image on offer is chosen.
 *
 * Apply is the gate. Whatever it lets through replaces the OS list for the
 * whole application, so it must not enable for an address the fetcher will
 * not accept -- and the check it consults is the same one C++ applies to a
 * deep link, so the two cannot drift apart.
 *
 * The dialog was at 0%: the gate had never been evaluated in a test, which
 * means dropping the binding to the validator would have gone unnoticed
 * and let anything through.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "RepositoryDialog"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property bool disableWarnings: false
        // applySettings() restarts the wizard when the source changed,
        // because the OS the user picked came from the old one.
        property int resets: 0
        function resetWizard() { fakeContainer.resets++ }
    }

    Component {
        id: dialogComponent
        RepositoryDialog {
            wizardContainer: fakeContainer
        }
    }

    property var dialog: null

    function init() {
        fakeContainer.resets = 0
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

    function child(name) {
        var c = findChild(dialog, name)
        verify(c, "found " + name)
        return c
    }

    function chooseCustomUri(text) {
        child("repoCustomUriRadio").checked = true
        child("repoCustomUriField").text = text
    }

    function test_the_official_repository_needs_no_address() {
        child("repoOfficialRadio").checked = true
        tryVerify(function () { return child("repoApplyButton").enabled }, 3000)
    }

    function test_a_well_formed_list_address_is_accepted() {
        chooseCustomUri("https://example.com/os_list.json")
        tryVerify(function () { return child("repoApplyButton").enabled }, 3000)
    }

    function test_an_empty_address_is_not_enough() {
        chooseCustomUri("")
        wait(200)
        verify(!child("repoApplyButton").enabled)
    }

    function test_an_address_that_is_not_a_list_is_refused_data() {
        return [
            { tag: "not a url",        uri: "just some words" },
            { tag: "no extension",     uri: "https://example.com/" },
            { tag: "wrong extension",  uri: "https://example.com/os_list.xml" },
            // file:// would read the local disk rather than fetch anything.
            { tag: "local file",       uri: "file:///etc/passwd.json" },
            { tag: "not a fetch",      uri: "javascript:alert(1)//.json" },
            // The extension has to be in the path, not smuggled into a query.
            { tag: "extension in query", uri: "https://example.com/evil?x=.json" }
        ]
    }

    function test_an_address_that_is_not_a_list_is_refused(data) {
        chooseCustomUri(data.uri)
        wait(200)
        verify(!child("repoApplyButton").enabled,
               "Apply stayed disabled for: " + data.tag)
    }

    function test_a_pre_signed_address_is_still_accepted() {
        // Blob storage hands the list out with a token attached; refusing
        // those would rule out an ordinary hosting arrangement.
        chooseCustomUri("https://acct.blob.core.windows.net/c/manifest.json?sv=2021&sig=abc")
        tryVerify(function () { return child("repoApplyButton").enabled }, 3000)
    }

    function test_correcting_a_bad_address_re_enables_apply() {
        // The gate is a live binding, not a one-shot check on open.
        chooseCustomUri("nonsense")
        wait(200)
        verify(!child("repoApplyButton").enabled)

        child("repoCustomUriField").text = "https://example.com/os_list.json"
        tryVerify(function () { return child("repoApplyButton").enabled }, 3000)
    }

    function test_an_address_pasted_with_a_newline_is_cleaned_up_not_rejected() {
        // Copying a URL out of a browser brings a newline with it. The field
        // trims, so this is accepted and applied without the newline.
        //
        // Worth pinning because C++ rejects the untrimmed form outright --
        // that guard is for the deep-link "repo=" path, which never passes
        // through a text field. Remove the trim here and a pasted address
        // stops working while the deep link keeps going, which is a
        // difficult report to make sense of.
        chooseCustomUri("https://example.com/os_list.json\n")

        tryVerify(function () { return child("repoApplyButton").enabled }, 3000,
                  "the pasted address was accepted")
        compare(child("repoCustomUriField").value, "https://example.com/os_list.json",
                "and the newline is gone from what would be fetched")
    }


    // ── What Apply and Cancel do to the rest of the wizard ────────────
    //
    // Changing the content repository invalidates everything downstream:
    // the OS the user chose came from the old source and may not be in the
    // new one. So applySettings() restarts the wizard -- and only when the
    // source actually changed, because a restart throws away the device,
    // the card and every customisation setting along with the OS.
    //
    // Which makes the guard the interesting half. Open the options, look at
    // the repository, press Apply without touching anything, and all of
    // that work has to still be there.
    //
    // The branches that did change something are deliberately not driven
    // here. Each calls refreshOsListFrom(), which clears the application's
    // OS list and starts a fetch on the singleton every other test file in
    // this process shares. What is checked instead is that Apply ran at all
    // -- it clears `initialized` on every path -- so "nothing changed"
    // cannot be confused with "Apply did nothing".

    function test_applying_without_changing_anything_leaves_the_wizard_alone() {
        // The official repository, which is what the dialog opens on when
        // no custom one is set, and nothing touched.
        verify(child("repoOfficialRadio").checked,
               "the dialog opened on the official source")

        child("repoApplyButton").clicked()

        compare(fakeContainer.resets, 0,
                "the device, the card and the customisation are still there")
        verify(!dialog.initialized,
               "and Apply did run -- it re-reads its state next time")
    }

    function test_cancelling_leaves_the_wizard_alone() {
        chooseCustomUri("https://example.com/os_list.json")
        tryVerify(function () { return child("repoApplyButton").enabled }, 3000)

        child("repoCancelButton").clicked()

        compare(fakeContainer.resets, 0)
        tryVerify(function () { return !dialog.visible }, 3000)
        verify(!dialog.initialized,
               "so a later open reads the source that is actually in use, "
               + "not the one that was typed and abandoned")
    }

    // ── Choosing a file rather than typing an address ─────────────────

    function test_a_file_chosen_from_the_picker_becomes_the_pending_source() {
        // The picker is used where no native dialog is available. Its answer
        // has to land on the dialog, or the Apply button stays disabled and
        // the user has no way to use a local list at all.
        dialog.selectedRepo = ""
        dialog.repoFileDialog.selectedFile = "file:///tmp/imager-test/os_list.json"

        dialog.repoFileDialog.accepted()

        compare(String(dialog.selectedRepo),
                "file:///tmp/imager-test/os_list.json")
    }

    function test_a_chosen_file_enables_apply() {
        child("repoCustomFileRadio").checked = true
        dialog.repoFileDialog.selectedFile = "file:///tmp/imager-test/os_list.json"
        dialog.repoFileDialog.accepted()

        tryVerify(function () { return child("repoApplyButton").enabled }, 3000,
                  "a chosen file is enough to apply")
    }

    function test_choosing_the_file_option_puts_the_cursor_in_the_field() {
        // The field is read-only and filled by the picker, so the focus is
        // what carries a keyboard user on to the Browse button next to it.
        child("repoCustomFileRadio").checked = true

        tryVerify(function () {
            return child("repoCustomFilePathField").activeFocus
        }, 3000, "the path field took focus")
    }
}
