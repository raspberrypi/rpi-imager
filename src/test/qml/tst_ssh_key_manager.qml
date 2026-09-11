/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * SshKeyManager: the keys that decide whether you can get into the Pi.
 *
 * These are the authorized_keys entries written to the image. A key dropped
 * silently, or mangled on the way in, produces a board that refuses the
 * login the user set up -- and the only way to find out is to try it after
 * first boot.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "SshKeyManager"
    when: windowShown
    width: 600
    height: 400
    visible: true

    Component {
        id: managerComponent
        SshKeyManager {}
    }

    property var mgr: null

    function init() {
        mgr = managerComponent.createObject(testCase)
        verify(mgr, "the manager was created")
        mgr.keys = []
    }

    function cleanup() {
        if (mgr) {
            mgr.destroy()
            mgr = null
        }
    }

    readonly property string rsaKey: "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAAB user@host"
    readonly property string edKey: "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI other@host"

    // ── Reading a file of keys ────────────────────────────────────────

    function test_nothing_in_means_nothing_out() {
        compare(mgr.splitKeys("").length, 0)
        compare(mgr.splitKeys(null).length, 0)
    }

    function test_one_key_per_line() {
        var keys = mgr.splitKeys(rsaKey + "\n" + edKey)
        compare(keys.length, 2)
        compare(keys[0], rsaKey)
        compare(keys[1], edKey)
    }

    function test_blank_lines_and_stray_whitespace_are_dropped() {
        // authorized_keys files collected by hand have both.
        var keys = mgr.splitKeys("\n  " + rsaKey + "  \n\n\n" + edKey + "\n")
        compare(keys.length, 2)
        compare(keys[0], rsaKey, "leading and trailing spaces are gone")
        compare(keys[1], edKey)
    }

    function test_windows_line_endings_do_not_produce_broken_keys() {
        // A file written on Windows would otherwise leave a \r on the end of
        // every key, which sshd will not match.
        var keys = mgr.splitKeys(rsaKey + "\r\n" + edKey + "\r\n")
        compare(keys.length, 2)
        compare(keys[0], rsaKey)
        compare(keys[1], edKey)
    }

    // ── PuTTY's format ────────────────────────────────────────────────

    function test_a_putty_key_is_reassembled_into_one_line() {
        // PuTTY writes the body over several lines with a header and a
        // comment around it. Pasted verbatim it has to come back as a single
        // authorized_keys entry, not five broken ones.
        var putty = "---- BEGIN SSH2 PUBLIC KEY ----\n"
                  + "Comment: \"rsa-key-20250101\"\n"
                  + "AAAAB3NzaC1yc2EAAAADAQABAAAB\n"
                  + "CDEFGHIJKLMNOPQRSTUVWXYZ0123\n"
                  + "---- END SSH2 PUBLIC KEY ----"

        var keys = mgr.splitKeys(putty)

        compare(keys.length, 1, "one key, not one per line")
        compare(keys[0], "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABCDEFGHIJKLMNOPQRSTUVWXYZ0123",
                "the body is joined and given the prefix sshd expects")
    }

    function test_a_putty_key_and_an_openssh_key_in_one_file() {
        var mixed = rsaKey + "\n"
                  + "---- BEGIN SSH2 PUBLIC KEY ----\n"
                  + "AAAABBBB\n"
                  + "---- END SSH2 PUBLIC KEY ----\n"
                  + edKey

        var keys = mgr.splitKeys(mixed)

        compare(keys.length, 3)
        compare(keys[0], rsaKey)
        compare(keys[1], "ssh-rsa AAAABBBB")
        compare(keys[2], edKey)
    }

    // ── Not collecting the same key twice ─────────────────────────────

    function test_the_same_key_is_not_added_twice_from_a_file() {
        mgr.addKeysFromFile(rsaKey + "\n" + rsaKey + "\n" + edKey)
        compare(mgr.keys.length, 2)
    }

    function test_a_second_file_does_not_re_add_what_is_already_there() {
        mgr.addKeysFromFile(rsaKey)
        mgr.addKeysFromFile(rsaKey + "\n" + edKey)
        compare(mgr.keys.length, 2)
    }

    function test_adding_one_key_twice_keeps_one() {
        mgr.addKey(rsaKey)
        mgr.addKey(rsaKey)
        compare(mgr.keys.length, 1)
    }

    function test_an_empty_key_is_not_added() {
        mgr.addKey("")
        mgr.addKey("   ")
        compare(mgr.keys.length, 0)
    }

    // ── Removing, and what gets written ───────────────────────────────

    function test_removing_a_key_leaves_the_others_alone() {
        mgr.addKey(rsaKey)
        mgr.addKey(edKey)

        mgr.removeKey(0)

        compare(mgr.keys.length, 1)
        compare(mgr.keys[0], edKey, "the right one was removed")
    }

    function test_removing_an_index_that_is_not_there_changes_nothing() {
        mgr.addKey(rsaKey)
        mgr.removeKey(5)
        mgr.removeKey(-1)
        compare(mgr.keys.length, 1)
    }

    function test_what_gets_written_is_one_key_per_line() {
        // This is the authorized_keys content.
        mgr.addKey(rsaKey)
        mgr.addKey(edKey)
        compare(mgr.getAllKeysAsString(), rsaKey + "\n" + edKey)
    }

    // ── The algorithm has to match the key ────────────────────────────

    function test_a_putty_ed25519_key_is_not_labelled_as_rsa() {
        // PuTTYgen exports ed25519 as readily as RSA, and the RFC 4716 block
        // it writes says nothing about which. Labelling it ssh-rsa produces
        // an authorized_keys line sshd refuses -- the user watches the key go
        // in and then cannot log in, with nothing to point at.
        var putty = "---- BEGIN SSH2 PUBLIC KEY ----\n"
                  + "Comment: \"ed25519-key-20250101\"\n"
                  + "AAAAC3NzaC1lZDI1NTE5AAAAI\n"
                  + "---- END SSH2 PUBLIC KEY ----"

        var keys = mgr.splitKeys(putty)

        compare(keys.length, 1)
        compare(keys[0], "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI")
    }

    function test_the_algorithm_is_read_out_of_the_key_itself_data() {
        return [
            { tag: "rsa",     body: "AAAAB3NzaC1yc2E",           algorithm: "ssh-rsa" },
            { tag: "ed25519", body: "AAAAC3NzaC1lZDI1NTE5AAAAI", algorithm: "ssh-ed25519" },
            { tag: "dss",     body: "AAAAB3NzaC1kc3M",           algorithm: "ssh-dss" }
        ]
    }

    function test_the_algorithm_is_read_out_of_the_key_itself(data) {
        compare(mgr.sshAlgorithmFromBlob(data.body), data.algorithm)
    }

    function test_an_unreadable_blob_falls_back_rather_than_inventing_a_name() {
        compare(mgr.sshAlgorithmFromBlob(""), "")
        compare(mgr.sshAlgorithmFromBlob("!!!not base64!!!"), "")
        compare(mgr.sshAlgorithmFromBlob("AAAA"), "", "a zero-length name is not a name")

        // And the caller still produces something usable.
        var keys = mgr.splitKeys("---- BEGIN SSH2 PUBLIC KEY ----\nQUJD\n---- END SSH2 PUBLIC KEY ----")
        compare(keys.length, 1)
        compare(keys[0], "ssh-rsa QUJD")
    }

    // ── Lines that share a name with a JavaScript builtin ─────────────

    function test_a_line_named_after_a_builtin_is_not_silently_dropped() {
        // deduplicateKeys indexes an object by the key text. A plain object
        // inherits Object.prototype, so "constructor" and "toString" read as
        // already-seen before anything has been seen, and vanished. Nobody
        // has an SSH key called toString, but a file the user points at can
        // contain anything, and losing a line without saying so is the one
        // thing this must not do.
        mgr.addKeysFromFile("constructor\ntoString\n__proto__\n" + rsaKey)

        compare(mgr.keys.length, 4)
        compare(mgr.keys[3], rsaKey)
    }


    // ── Getting a key in there in the first place ─────────────────────
    //
    // Everything above drives addKey() and addKeysFromFile() directly. The
    // three handlers a user actually goes through -- Enter in the paste
    // field, the Add/Browse button, and the file picker coming back -- had
    // never run.
    //
    // That hop failing is quiet in the worst way. The key is pasted, the
    // field looks like it was accepted, the card is written, and the board
    // comes up with an authorized_keys the user is not in. They find out
    // when SSH refuses them, on a headless machine with no other way in.

    function field() {
        var f = findChild(mgr, "sshAddKeyField")
        verify(f, "found the key field")
        return f
    }

    function addOrBrowse() {
        var b = findChild(mgr, "sshAddOrBrowseButton")
        verify(b, "found the add/browse button")
        return b
    }

    function test_pressing_return_in_the_field_adds_the_key() {
        field().text = rsaKey
        field().forceActiveFocus()

        keyClick(Qt.Key_Return)

        compare(mgr.keys.length, 1, "the key was added")
        compare(mgr.keys[0], rsaKey)
    }

    function test_the_field_is_emptied_so_the_next_key_can_be_pasted() {
        // Left as it was, the next paste lands on the end of the last key
        // and produces one long line that is not a key at all.
        field().text = rsaKey
        field().forceActiveFocus()
        keyClick(Qt.Key_Return)

        compare(field().text, "", "the field is ready for the next one")

        field().text = edKey
        keyClick(Qt.Key_Return)

        compare(mgr.keys.length, 2, "and the second key went in on its own")
        compare(mgr.keys[1], edKey)
    }

    function test_return_on_an_empty_field_adds_nothing() {
        // An empty line in authorized_keys is harmless; an empty entry in
        // the list the user is looking at is not, because it reads as a key
        // they have added.
        field().text = ""
        field().forceActiveFocus()

        keyClick(Qt.Key_Return)

        compare(mgr.keys.length, 0)
    }

    function test_the_button_adds_what_is_in_the_field() {
        field().text = edKey

        addOrBrowse().clicked()

        compare(mgr.keys.length, 1)
        compare(mgr.keys[0], edKey)
        compare(field().text, "", "and empties the field behind it")
    }

    function test_the_button_says_which_of_the_two_things_it_will_do() {
        // One button, two jobs. Saying "Browse" while it would add, or the
        // other way round, is how a pasted key gets thrown away by someone
        // who thought they were opening a file dialog.
        field().text = ""
        compare(addOrBrowse().text, CommonStrings.browse)

        field().text = rsaKey
        compare(addOrBrowse().text, "Add")
    }

    // ── A key file chosen from the picker ─────────────────────────────

    function test_a_key_file_is_read_and_its_keys_added() {
        var url = TestFiles.write("authorized_keys",
                                  rsaKey + "\n" + edKey + "\n")
        verify(url.length > 0, "the fixture file was written")
        var picker = findChild(mgr, "sshBrowseKeyFileDialog")
        verify(picker, "found the key picker")

        picker.selectedFile = url
        picker.accepted()

        compare(mgr.keys.length, 2, "both keys in the file were added")
        compare(mgr.keys[0], rsaKey)
        compare(mgr.keys[1], edKey)
    }

    function test_a_key_file_that_is_not_there_adds_nothing() {
        // The picker can hand back a path that has since gone.
        //
        // Defended twice: the handler checks the read came back with
        // something, and splitKeys() would produce no keys from an empty
        // string anyway. Removing the handler's check fails nothing, so no
        // claim is made for it -- what this pins is the outcome.
        var picker = findChild(mgr, "sshBrowseKeyFileDialog")
        verify(picker, "found the key picker")

        picker.selectedFile = "file:///tmp/rpi-imager-no-such-key.pub"
        picker.accepted()

        compare(mgr.keys.length, 0)
    }

    function test_choosing_a_file_twice_does_not_double_the_keys() {
        // Browsing again after a mis-click is an ordinary thing to do.
        var url = TestFiles.write("authorized_keys_dup", rsaKey + "\n")
        verify(url.length > 0)
        var picker = findChild(mgr, "sshBrowseKeyFileDialog")

        picker.selectedFile = url
        picker.accepted()
        picker.accepted()

        compare(mgr.keys.length, 1)
    }
}
