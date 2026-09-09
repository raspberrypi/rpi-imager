/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * SelectionListView: telling a screen reader the list changed.
 *
 * A ListView announces nothing of its own when a row appears or
 * disappears, and a changed Accessible.name on an element nobody is
 * focused on is not read out. Every list in this application is filled by
 * something the user is waiting for -- a network fetch, a card being
 * plugged in -- and a sighted user simply watches it happen. Without this
 * an assistive technology user is left pressing Tab to find out whether
 * the thing they were waiting for arrived.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ListAnnouncements"
    when: windowShown
    width: 600
    height: 400
    visible: true

    Component {
        id: listComponent
        SelectionListView {
            width: 400
            height: 300
            itemNoun: "storage device"
            itemNounPlural: "storage devices"
            announceFirstPopulation: true
            model: ListModel {}
            delegate: Item { width: 10; height: 10 }
        }
    }

    property var list: null

    function init() {
        list = listComponent.createObject(testCase)
        verify(list, "the list was created")
    }

    function cleanup() {
        if (list) {
            list.destroy()
            list = null
        }
    }

    function alertText() {
        var a = findChild(list, "populationAnnouncement")
        verify(a, "found the announcement node")
        return a.text
    }

    // Waiting on count is not enough: onCountChanged runs after the count
    // has already changed, and several appends in a row are coalesced into
    // one call. lastKnownCount is set by the handler itself, so waiting on
    // it is waiting for the announcement to have been decided.
    function settleAt(n) {
        tryVerify(function () { return list.count === n }, 3000,
                  "the list holds " + n)
        tryVerify(function () { return list.lastKnownCount === n }, 3000,
                  "and the handler has caught up")
    }

    function add(n) {
        for (var i = 0; i < n; i++) list.model.append({})
        settleAt(n)
    }

    function removeOne(leaving) {
        list.model.remove(0)
        settleAt(leaving)
    }

    // ── The list filling up ───────────────────────────────────────────

    function test_a_fetched_list_is_silent_when_it_loads() {
        // Filled in one go when a fetch lands. Announcing that talks over
        // the heading being read as the screen opens, and the user did not
        // watch anything change -- they watched it appear.
        list.announceFirstPopulation = false

        add(3)

        compare(alertText(), "")
    }

    function test_a_live_list_announces_its_first_arrival() {
        // The case the whole thing exists for: a card plugged into a list
        // that was empty. Staying quiet here is the original problem.
        add(1)

        verify(alertText().indexOf("connected") !== -1,
               "the first card was announced: " + alertText())
    }

    function test_a_fetched_list_still_announces_later_changes() {
        list.announceFirstPopulation = false
        add(2)
        compare(alertText(), "", "the load was silent")

        list.model.append({})
        settleAt(3)

        verify(alertText().indexOf("connected") !== -1,
               "but a later arrival is not: " + alertText())
    }

    function test_something_arriving_afterwards_is_announced() {
        add(1)

        list.model.append({})
        settleAt(2)

        verify(alertText().indexOf("connected") !== -1,
               "it said something arrived: " + alertText())
        verify(alertText().indexOf("storage device") !== -1,
               "and named what kind of thing it was")
        verify(alertText().indexOf("2") !== -1, "and how many there are now")
    }

    function test_something_leaving_is_announced() {
        add(2)
        removeOne(1)

        verify(alertText().indexOf("removed") !== -1, alertText())
        verify(alertText().indexOf("1") !== -1, "one is left")
    }

    function test_the_last_one_leaving_says_there_are_none() {
        // "0 storage devices remaining" reads worse than being told there
        // are none, and this is the state the user has to act on.
        add(1)
        removeOne(0)

        verify(alertText().indexOf("None") !== -1, alertText())
    }

    function test_the_wording_agrees_with_the_count() {
        // "1 storage devices available" is worse than saying nothing.
        add(1)
        list.model.append({})
        settleAt(2)
        verify(alertText().indexOf("2 storage devices") !== -1, alertText())

        removeOne(1)
        verify(alertText().indexOf("1 storage device ") !== -1,
               "singular when there is one: " + alertText())
    }

    // ── Staying out of the way ────────────────────────────────────────

    function test_the_node_is_ignored_until_there_is_something_to_say() {
        var a = findChild(list, "populationAnnouncement")
        verify(a, "found the announcement node")

        verify(a.Accessible.ignored,
               "nothing is exposed while there is nothing to announce")

        add(1)
        tryVerify(function () { return !a.Accessible.ignored }, 3000,
                  "and it is exposed once there is")
    }

    function test_a_list_can_opt_out() {
        // For a list whose contents never change after loading, the node is
        // noise in the accessibility tree.
        list.announcePopulationChanges = false
        add(1)

        list.model.append({})
        settleAt(2)

        compare(alertText(), "")
    }
}
