/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * SelectionListView: the device, OS and storage pickers.
 *
 * The storage picker is the one control in the application where choosing
 * the wrong row destroys somebody's data, and it is the only caller that
 * supplies isItemSelectableFunction -- a drive is unselectable when it is
 * read-only, or when it is a system drive and the filter is on.
 *
 * Arrow navigation has always honoured that. Enter, Space and Return did
 * not: they emitted for whatever currentIndex happened to be, and the
 * highlight does not only move by arrow key. The drive list is polled while
 * the page is open, so a row can stop being selectable underneath a
 * highlight already sitting on it -- a card whose lock switch is set, or one
 * that goes read-only after a media error. The row greys out and Enter still
 * picks it.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "SelectionListView"
    when: windowShown
    width: 400
    height: 300
    visible: true

    Component {
        id: listComponent

        SelectionListView {
            id: list
            width: 300
            height: 200

            // Indices named here are refused by isItemSelectableFunction, the
            // way a read-only or filtered-out drive is.
            property var blocked: []

            property int selectedIndex: -1
            property int selectedCount: 0
            property int spaceCount: 0
            property int enterCount: 0
            property int returnCount: 0

            model: ListModel {
                ListElement { label: "row0" }
                ListElement { label: "row1" }
                ListElement { label: "row2" }
                ListElement { label: "row3" }
                ListElement { label: "row4" }
            }

            delegate: Item {
                required property string label
                width: 300
                height: 30
                Text { text: parent.label }
            }

            isItemSelectableFunction: function(index) {
                return list.blocked.indexOf(index) === -1
            }

            onItemSelected: function(index, item) {
                list.selectedIndex = index
                list.selectedCount++
            }
            onSpacePressed: list.spaceCount++
            onEnterPressed: list.enterCount++
            onReturnPressed: list.returnCount++
        }
    }

    function create(props) {
        const list = createTemporaryObject(listComponent, testCase, props)
        verify(list, "the list was created")
        list.forceActiveFocus()
        verify(list.activeFocus, "the list took focus")
        return list
    }

    // -- Arrow navigation --------------------------------------------------

    function test_down_from_nothing_selects_the_first_row() {
        const list = create({})
        compare(list.currentIndex, -1)

        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 0)
    }

    function test_down_walks_the_list() {
        const list = create({})
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 2)
    }

    function test_up_walks_back() {
        const list = create({ currentIndex: 3 })
        keyClick(Qt.Key_Up)
        compare(list.currentIndex, 2)
    }

    function test_navigation_stops_at_the_ends() {
        const list = create({ currentIndex: 0 })
        keyClick(Qt.Key_Up)
        compare(list.currentIndex, 0, "up at the top stays put")

        list.currentIndex = 4
        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 4, "down at the bottom stays put")
    }

    function test_down_steps_over_an_unselectable_row() {
        const list = create({ blocked: [1], currentIndex: 0 })
        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 2, "row 1 is skipped, not landed on")
    }

    function test_down_steps_over_a_run_of_unselectable_rows() {
        const list = create({ blocked: [1, 2, 3], currentIndex: 0 })
        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 4)
    }

    function test_up_steps_over_an_unselectable_row() {
        const list = create({ blocked: [2], currentIndex: 3 })
        keyClick(Qt.Key_Up)
        compare(list.currentIndex, 1)
    }

    function test_navigation_stays_put_when_everything_beyond_is_blocked() {
        const list = create({ blocked: [1, 2, 3, 4], currentIndex: 0 })
        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 0,
                "no selectable row below, so the highlight does not move")
    }

    function test_first_arrow_press_skips_a_blocked_first_row() {
        const list = create({ blocked: [0] })
        compare(list.currentIndex, -1)

        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 1,
                "the list opens onto something the user may actually pick")
    }

    // -- Activating the highlighted row ------------------------------------

    function test_enter_selects_the_highlighted_row() {
        const list = create({ currentIndex: 2 })
        keyClick(Qt.Key_Return)
        compare(list.selectedCount, 1)
        compare(list.selectedIndex, 2)
    }

    function test_space_selects_the_highlighted_row() {
        const list = create({ currentIndex: 1 })
        keyClick(Qt.Key_Space)
        compare(list.selectedCount, 1)
        compare(list.selectedIndex, 1)
        compare(list.spaceCount, 1)
    }

    function test_enter_with_nothing_highlighted_selects_nothing() {
        const list = create({})
        compare(list.currentIndex, -1)

        keyClick(Qt.Key_Return)
        compare(list.selectedCount, 0)
    }

    // -- The row that stopped being selectable under the highlight ---------

    function test_enter_refuses_a_row_that_became_unselectable() {
        // The highlight is already on row 2 when the poll marks it read-only.
        const list = create({ currentIndex: 2 })
        list.blocked = [2]

        keyClick(Qt.Key_Return)
        compare(list.selectedCount, 0,
                "a greyed-out row is not selected by pressing Enter on it")
        compare(list.returnCount, 0,
                "and the step is not told the row was activated either")
    }

    function test_space_refuses_a_row_that_became_unselectable() {
        const list = create({ currentIndex: 2 })
        list.blocked = [2]

        keyClick(Qt.Key_Space)
        compare(list.selectedCount, 0)
        compare(list.spaceCount, 0)
    }

    function test_the_user_can_move_off_a_blocked_row_and_select() {
        // Refusing must not strand them: arrow keys still work from a row
        // that has gone unselectable underneath the highlight.
        const list = create({ currentIndex: 2 })
        list.blocked = [2]

        keyClick(Qt.Key_Return)
        compare(list.selectedCount, 0)

        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 3)

        keyClick(Qt.Key_Return)
        compare(list.selectedCount, 1)
        compare(list.selectedIndex, 3)
    }

    // -- Programmatic selection --------------------------------------------

    function test_selectItem_picks_a_selectable_row() {
        const list = create({})
        list.selectItem(3)
        compare(list.currentIndex, 3)
        compare(list.selectedCount, 1)
        compare(list.selectedIndex, 3)
    }

    function test_selectItem_refuses_an_unselectable_row() {
        const list = create({ blocked: [3] })
        list.selectItem(3)
        compare(list.selectedCount, 0)
        compare(list.currentIndex, -1, "and does not move the highlight there")
    }

    function test_selectItem_refuses_an_index_off_the_end() {
        const list = create({})
        list.selectItem(99)
        list.selectItem(-1)
        compare(list.selectedCount, 0)
    }

    // -- No selectability function at all ----------------------------------

    function test_every_row_is_selectable_when_no_rule_is_given() {
        // Every picker except storage leaves isItemSelectableFunction unset,
        // and must behave exactly as it did before the guard existed.
        const list = create({ isItemSelectableFunction: null, currentIndex: 2 })
        verify(list.isItemSelectable(0))
        verify(list.isItemSelectable(4))

        keyClick(Qt.Key_Return)
        compare(list.selectedCount, 1)
        compare(list.selectedIndex, 2)

        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 3, "and nothing is skipped")
    }
}
