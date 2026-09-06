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
    }

    Component {
        id: dialogComponent
        RepositoryDialog {
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
}
