/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * The two dialogs that make the user wait before they can agree to something.
 *
 * Both hold their accepting button disabled for a second and a half after
 * appearing, labelled "Please wait…" until then, so a click already travelling
 * towards where the dialog turns up cannot land on it. And both reset that hold
 * when closed, along with the value they were asking about, so a second
 * prompt has to be waited out rather than arriving pre-approved. The mechanism
 * is written out twice, once per dialog, so it is worth checking twice.
 *
 * The first is a second Raspberry Pi Connect token arriving while one is
 * already held.
 *
 * The token comes in over a URL handler, so it does not have to be one the
 * user asked for: a stale redirect, a link clicked twice, or someone else's
 * browser callback can deliver one. The dialog that asks about it is the only
 * thing between that and the wrong Pi being enrolled, and its warning says so
 * -- "only overwrite the token if you initiated this action".
 *
 * Two things protect the user, and neither was covered. The Replace button is
 * disabled for a second and a half after the dialog appears, and says "Please
 * wait…" while it is, so a click already on its way somewhere else cannot land
 * on it. And closing the dialog resets that delay along with the token it was
 * holding, so a second conflict has to be waited out again rather than
 * arriving pre-approved.
 *
 * The Replace handler also has an ordering to get right that is easy to miss:
 * it closes the dialog and then reads the new token to hand on, while the
 * close handler clears that same token. It works because a Popup's closed
 * signal waits for the exit transition rather than firing inside close(), but
 * nothing said so -- and if it ever fired synchronously the user would be
 * replacing their token with an empty string. That is checked by reading back
 * what the writer was actually given.
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

    // -- The other one: a repository link from a deep link -----------------
    //
    // rpi-imager://open?repo=... asks to replace the source of every image on
    // offer, so it gets the same hold. Its opening, its refusal to be accepted
    // at once, its rejection of a malformed link and its guard against a
    // second link changing the URL under the user are covered in
    // tst_wizard_navigation; what is left is the hold expiring and the two
    // ways of saying no.
    //
    // Accepting is not exercised. The Switch handler has the same ordering as
    // the token one -- close, then read the URL the close handler clears --
    // but pressing it starts a real network fetch and resets the wizard. The
    // shared assumption underneath both, that a Popup's closed signal waits
    // for the exit transition rather than firing inside close(), is what the
    // token case above pins.

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
}
