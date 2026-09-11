/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The two dialogs that make the user wait before they can agree to something.
 *
 * Both hold their accepting button disabled for a second and a half after
 * appearing, labelled "Please wait…" until then, so a click already travelling
 * towards where the dialog turns up cannot land on it. And both reset that hold
 * when closed, along with the value they were asking about, so a second
 * prompt has to be waited out rather than arriving pre-approved. The mechanism
 * is written out twice, once per dialog, so it is worth checking twice.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ConfirmationDelays"
    when: windowShown
    width: 800
    height: 600
    visible: true

    Component {
        id: containerComponent
        WizardContainer {}
    }

    property var wiz: null

    function initTestCase() {
        wiz = containerComponent.createObject(testCase)
        verify(wiz, "the wizard container was created")
        wiz.overlayRootRef = testCase
        wiz.width = testCase.width
        wiz.height = testCase.height
    }

    function cleanupTestCase() {
        if (wiz) {
            wiz.destroy()
            wiz = null
        }
        ImageWriterSingleton.clearConnectToken()
    }

    function init() {
        ImageWriterSingleton.clearConnectToken()
        ImageWriterSingleton.overwriteConnectToken("the-token-already-held")
        compare(ImageWriterSingleton.getRuntimeConnectToken(),
                "the-token-already-held", "there is a token to conflict with")
    }

    function cleanup() {
        const d = dialog()
        if (d.visible) {
            d.close()
            tryVerify(function () { return !d.visible }, 3000)
        }
        ImageWriterSingleton.clearConnectToken()
    }

    function dialog() {
        const d = findChild(wiz, "tokenConflictDialog")
        verify(d, "the token conflict dialog was found")
        return d
    }

    // The buttons are ids inside the dialog, and a Popup keeps its content
    // under contentItem rather than children.
    function walkForButton(item, label) {
        if (!item)
            return null
        if (item.text !== undefined && String(item.text) === label
                && item.clicked !== undefined)
            return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = walkForButton(kids[i], label)
            if (found)
                return found
        }
        return null
    }

    function button(label) {
        return walkForButton(dialog().contentItem, label)
    }

    function raiseConflict(token) {
        ImageWriterSingleton.connectTokenConflictDetected(token)
        const d = dialog()
        tryVerify(function () { return d.opened }, 3000,
                  "a conflicting token raises the dialog")
        return d
    }

    // -- It is asked about at all ------------------------------------------

    function test_a_conflicting_token_is_put_to_the_user() {
        const d = raiseConflict("a-second-token")

        compare(String(d.newToken), "a-second-token",
                "the dialog holds the token it is asking about")
        compare(ImageWriterSingleton.getRuntimeConnectToken(),
                "the-token-already-held",
                "and nothing has been replaced yet")
    }

    function test_the_warning_says_why_this_is_being_asked() {
        // The wording is the protection for anyone who reads it: a token can
        // arrive without being asked for, and only the user knows whether
        // they started it.
        raiseConflict("a-second-token")

        const body = findChild(wiz, "tokenConflictDialog")
        const text = collectText(body.contentItem)
        verify(text.indexOf("initiated this action") >= 0,
               "the warning names the thing only the user can know: " + text)
    }

    function collectText(item) {
        if (!item)
            return ""
        let out = item.text !== undefined ? String(item.text) + " " : ""
        const kids = item.children || []
        for (let i = 0; i < kids.length; i++)
            out += collectText(kids[i])
        return out
    }

    // -- The delay before it can be accepted -------------------------------

    function test_replacing_cannot_be_clicked_straight_away() {
        // A click already travelling towards where this dialog appears must
        // not land on the button that overwrites the token.
        const d = raiseConflict("a-second-token")

        verify(!d.allowAccept, "accepting is held back")
        const waiting = button("Please wait…")
        verify(waiting !== null,
               "and the button says so rather than looking clickable")
        verify(!waiting.enabled, "and is not clickable")
    }

    function test_the_delay_ends_and_replacing_becomes_possible() {
        const d = raiseConflict("a-second-token")

        tryVerify(function () { return d.allowAccept }, 5000,
                  "the hold is released after the delay")

        const replace = button("Replace token")
        verify(replace !== null, "and the button now says what it does")
        verify(replace.enabled)
    }

    function test_declining_is_available_immediately() {
        // Only accepting is held back. Making the user wait to say no would
        // be the wrong way round.
        raiseConflict("a-second-token")

        const keep = button("Keep existing")
        verify(keep !== null, "there is a way to decline")
        verify(keep.enabled, "and it works at once")
    }

    // -- Answering it ------------------------------------------------------

    function test_replacing_hands_on_the_new_token_and_not_an_empty_one() {
        // The ordering. Replace closes the dialog and then reads newToken,
        // while the close handler clears newToken -- so if closed ever fired
        // inside close(), the user's token would be replaced with "".
        const d = raiseConflict("a-second-token")
        tryVerify(function () { return d.allowAccept }, 5000)

        mouseClick(button("Replace token"))

        tryVerify(function () { return !d.visible }, 3000, "the dialog closes")
        compare(ImageWriterSingleton.getRuntimeConnectToken(), "a-second-token",
                "the token the user agreed to is the one that was stored")
    }

    function test_declining_leaves_the_existing_token_alone() {
        const d = raiseConflict("a-second-token")

        mouseClick(button("Keep existing"))

        tryVerify(function () { return !d.visible }, 3000)
        compare(ImageWriterSingleton.getRuntimeConnectToken(),
                "the-token-already-held",
                "keeping means keeping")
    }

    function test_dismissing_with_escape_counts_as_declining() {
        // Escape is the reflex, and on this dialog it has to mean no.
        const d = raiseConflict("a-second-token")

        keyClick(Qt.Key_Escape)

        tryVerify(function () { return !d.visible }, 3000)
        compare(ImageWriterSingleton.getRuntimeConnectToken(),
                "the-token-already-held",
                "dismissing is not accepting")
    }

    // -- And the next one starts over ---------------------------------------

    function test_closing_forgets_the_token_and_the_hold_it_released() {
        // Otherwise a second conflict would arrive with the hold already
        // lifted and the previous token still in hand.
        const d = raiseConflict("a-second-token")
        tryVerify(function () { return d.allowAccept }, 5000)

        mouseClick(button("Keep existing"))
        tryVerify(function () { return !d.visible }, 3000)

        verify(!d.allowAccept, "the hold is back on")
        compare(String(d.newToken), "",
                "and the token it was holding is not kept around")
    }

    function test_a_second_conflict_has_to_be_waited_out_again() {
        const d = raiseConflict("a-second-token")
        tryVerify(function () { return d.allowAccept }, 5000)
        mouseClick(button("Keep existing"))
        tryVerify(function () { return !d.visible }, 3000)

        raiseConflict("a-third-token")

        verify(!d.allowAccept,
               "the second one is held back like the first, rather than "
               + "arriving pre-approved")
        compare(String(d.newToken), "a-third-token",
                "and it is asking about the new token")
    }


    function repoDialog() {
        const d = findChild(wiz, "repositoryUrlDialog")
        verify(d, "the repository link dialog was found")
        return d
    }

    function repoButton(label) {
        return walkForButton(repoDialog().contentItem, label)
    }

    function raiseRepoLink(url) {
        ImageWriterSingleton.repositoryUrlReceived(url)
        const d = repoDialog()
        tryVerify(function () { return d.opened }, 3000,
                  "a repository link raises the dialog")
        return d
    }

    function closeRepoDialog() {
        const d = repoDialog()
        if (d.visible) {
            d.close()
            tryVerify(function () { return !d.visible }, 3000)
        }
    }

    function test_a_repository_link_cannot_be_switched_to_straight_away() {
        const d = raiseRepoLink("https://example.invalid/os_list.json")

        verify(!d.allowAccept, "accepting is held back")
        const waiting = repoButton("Please wait…")
        verify(waiting !== null && !waiting.enabled,
               "and the button says so rather than looking clickable")

        closeRepoDialog()
    }

    function test_the_repository_hold_ends_and_switching_becomes_possible() {
        const d = raiseRepoLink("https://example.invalid/os_list.json")

        tryVerify(function () { return d.allowAccept }, 5000,
                  "the hold is released after the delay")

        const sw = repoButton("Switch repository")
        verify(sw !== null, "and the button now says what it does")
        verify(sw.enabled)
        verify(String(sw.accessibleDescription).length > 0,
               "and says it to a screen reader too")

        closeRepoDialog()
    }

    function test_declining_a_repository_link_is_available_immediately() {
        raiseRepoLink("https://example.invalid/os_list.json")

        const cancel = repoButton("Cancel")
        verify(cancel !== null, "there is a way to decline")
        verify(cancel.enabled, "and it works at once, unlike accepting")

        closeRepoDialog()
    }

    function test_cancelling_a_repository_link_closes_it_and_keeps_nothing() {
        const d = raiseRepoLink("https://example.invalid/os_list.json")
        tryVerify(function () { return d.allowAccept }, 5000)

        mouseClick(repoButton("Cancel"))

        tryVerify(function () { return !d.visible }, 3000, "the dialog closes")
        verify(!d.allowAccept, "the hold is back on")
        compare(String(d.repoUrl), "",
                "and the URL it was asking about is not kept around")
    }

    function test_dismissing_a_repository_link_with_escape_declines_it() {
        const d = raiseRepoLink("https://example.invalid/os_list.json")

        keyClick(Qt.Key_Escape)

        tryVerify(function () { return !d.visible }, 3000)
        compare(String(d.repoUrl), "",
                "dismissing leaves nothing staged to switch to")
    }

    function test_a_second_repository_link_has_to_be_waited_out_again() {
        const d = raiseRepoLink("https://example.invalid/first.json")
        tryVerify(function () { return d.allowAccept }, 5000)
        mouseClick(repoButton("Cancel"))
        tryVerify(function () { return !d.visible }, 3000)

        raiseRepoLink("https://example.invalid/second.json")

        verify(!d.allowAccept,
               "the second link is held back like the first")
        compare(String(d.repoUrl), "https://example.invalid/second.json",
                "and it is asking about the new one")

        closeRepoDialog()
    }

    function test_switching_to_a_repository_link_switches_and_starts_over() {
        const before = ImageWriterSingleton.osListUrl()
        // Accepting a repository link replaces the source of every image on
        // offer, so anything already chosen against the old list has to go.
        // A wizard left holding an OS from the previous repository would
        // carry it into a write whose image no longer comes from anywhere
        // the user can see.
        const json = JSON.stringify({ "os_list": [] })
        const repo = TestFiles.write("switched_repo.json", json)
        verify(repo !== "", "wrote a repository to switch to")

        // State for the reset to clear, so "it was reset" is not true by
        // default.
        wiz.selectedOsName = "Something from the old list"
        wiz.selectedStorageName = "Some card"
        wiz.markStepPermissible(wiz.stepStorageSelection)
        verify(wiz.permissibleStepsBitmap !== 1, "there is state to lose")

        const d = raiseRepoLink(repo)
        tryVerify(function () { return d.allowAccept }, 5000,
                  "the hold is released")
        verify(d.isLocalFile, "a file:// link is recognised as a local one")

        // Labelled for what it does to a local file rather than "Switch
        // repository", which is the remote wording.
        const open = repoButton("Open")
        verify(open !== null, "there is a button to accept with")
        verify(open.enabled)

        mouseClick(open)

        tryVerify(function () { return !d.opened }, 3000, "the dialog closed")

        // The URL survived the close. If closed() fired inside close() this
        // would be the default repository, because the close handler clears
        // repoUrl before the handler reads it.
        tryVerify(function () {
            return ImageWriterSingleton.customRepoHost() === "switched_repo.json"
        }, 5000, "the writer is on the repository from the link; it is on '"
                 + ImageWriterSingleton.customRepoHost() + "'")

        // And the wizard started over rather than keeping choices made
        // against a list that is no longer on offer.
        compare(wiz.selectedOsName, "", "the chosen OS was cleared")
        compare(wiz.selectedStorageName, "", "and the chosen card")
        compare(wiz.permissibleStepsBitmap, 1,
                "and every step past the first is closed again")

        ImageWriterSingleton.refreshOsListFromDefaultUrl()
        tryVerify(function () {
            return ImageWriterSingleton.customRepoHost() === ""
        }, 10000, "put back for whatever runs next")

        leaveAnOsListBehind(before)
    }

    // Leave a populated OS list behind.
    //
    // Several files later in the run assume there is one -- their cases fail
    // rather than skip without it -- and a case here that switched
    // repositories has just emptied it. Going back to the shipped URL starts
    // a real fetch whose success depends on the machine, so the list comes
    // back from a local file and only the repository URL is restored, with
    // setCustomRepo, which does not start a fetch that could empty it again.
    function leaveAnOsListBehind(previousRepo) {
        const restore = TestFiles.write("restored_os_list.json", JSON.stringify({
            "os_list": [{
                "name": "Restored entry",
                "description": "So the files after this one have a list",
                "url": "https://example.invalid/restored.img.xz",
                "icon": "",
                "release_date": "2026-01-01",
                "extract_size": 1048576,
                "image_download_size": 524288,
                "extract_sha256": "ee55"
            }]
        }))
        ImageWriterSingleton.refreshOsListFrom(restore)
        tryVerify(function () {
            return !ImageWriterSingleton.isOsListUnavailable
        }, 10000, "a list is in place for whatever runs next")
        ImageWriterSingleton.setCustomRepo(previousRepo)
    }
}
