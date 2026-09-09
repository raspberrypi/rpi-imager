/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * OSSelectionListView: step two of the flow, choosing what to write.
 *
 * The OS list is not flat. Entries either write an image or open a
 * sublist -- "Raspberry Pi OS (other)", "Emulation and game OS" -- and each
 * sublist carries a back entry. Three decisions hang off telling those
 * apart:
 *
 *   - whether the scroll position is preserved. Navigating into a sublist
 *     replaces the whole list, so holding the old contentY would land the
 *     user somewhere arbitrary in the new one.
 *   - whether the wizard auto-advances. Picking with the keyboard moves on;
 *     picking with the mouse does not, because the pointer is already on
 *     the next control. A double-click counts as keyboard.
 *   - what the handler is handed, which is what actually gets written.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "OSSelectionListView"
    when: windowShown
    width: 400
    height: 300
    visible: true

    Component {
        id: listComponent

        OSSelectionListView {
            id: list
            width: 300
            height: 200

            // What the selection handler was last given.
            property var lastData: null
            property bool lastFromKeyboard: false
            property bool lastFromMouse: false
            property int handlerCalls: 0

            property int rightCount: 0
            property int leftCount: 0
            property var rightData: null

            model: ListModel {
                ListElement {
                    name: "Raspberry Pi OS (64-bit)"
                    subitems_json: ""
                    subitems_url: ""
                }
                ListElement {
                    name: "Raspberry Pi OS (other)"
                    subitems_json: ""
                    subitems_url: "https://example.invalid/other.json"
                }
                ListElement {
                    name: "Go back"
                    subitems_json: ""
                    subitems_url: "internal://back"
                }
                ListElement {
                    name: "Emulation and game OS"
                    subitems_json: "[{\"name\":\"nested\"}]"
                    subitems_url: ""
                }
                ListElement {
                    name: "Erase"
                    subitems_json: ""
                    subitems_url: ""
                }
            }

            delegate: Item {
                required property string name
                width: 300
                height: 30
                Text { text: parent.name }
            }

            osSelectionHandler: function(modelData, fromKeyboard, fromMouse) {
                list.lastData = modelData
                list.lastFromKeyboard = fromKeyboard
                list.lastFromMouse = fromMouse
                list.handlerCalls++
            }

            onRightPressed: function(index, item, modelData) {
                list.rightCount++
                list.rightData = modelData
            }
            onLeftPressed: list.leftCount++
        }
    }

    function create(props) {
        const list = createTemporaryObject(listComponent, testCase, props)
        verify(list, "the list was created")
        list.forceActiveFocus()
        return list
    }

    // -- What the handler is handed ----------------------------------------

    function test_selecting_an_entry_hands_over_its_model_row() {
        // The handler receives the row, not the index: it is what decides
        // which image gets written.
        const list = create({ currentIndex: 0 })
        keyClick(Qt.Key_Return)

        compare(list.handlerCalls, 1)
        verify(list.lastData, "the handler was given a row")
        compare(list.lastData.name, "Raspberry Pi OS (64-bit)")
    }

    function test_selecting_a_different_entry_hands_over_that_one() {
        const list = create({ currentIndex: 4 })
        keyClick(Qt.Key_Return)
        compare(list.lastData.name, "Erase")
    }

    function test_the_highlight_follows_the_selection() {
        const list = create({ currentIndex: 1 })
        keyClick(Qt.Key_Return)
        compare(list.currentIndex, 1)
    }

    // -- Keyboard versus mouse ---------------------------------------------

    function test_a_keyboard_selection_reports_as_keyboard() {
        // This is what lets the wizard move on by itself: the user's hands
        // are on the keys, so the next step should take focus.
        const list = create({ currentIndex: 0 })
        keyClick(Qt.Key_Return)

        compare(list.lastFromKeyboard, true)
        compare(list.lastFromMouse, false)
    }

    function test_a_mouse_selection_reports_as_mouse() {
        // No auto-advance: the pointer is already where the user wants it.
        const list = create({ currentIndex: 0, currentSelectionIsFromMouse: true })
        list.itemSelected(0, list.itemAtIndex(0))

        compare(list.lastFromMouse, true)
        compare(list.lastFromKeyboard, false)
    }

    function test_the_mouse_flag_is_cleared_after_use() {
        // Otherwise the next keyboard selection is misreported as a mouse
        // one and the wizard stops advancing for the rest of the session.
        const list = create({ currentIndex: 0, currentSelectionIsFromMouse: true })
        list.itemSelected(0, list.itemAtIndex(0))
        compare(list.lastFromMouse, true)
        compare(list.currentSelectionIsFromMouse, false)

        list.itemSelected(1, list.itemAtIndex(1))
        compare(list.lastFromMouse, false, "the flag did not persist")
        compare(list.lastFromKeyboard, true)
    }

    function test_a_double_click_advances_like_the_keyboard() {
        // A double-click is a deliberate "this one, go on" -- it reports as
        // keyboard so the wizard advances, while still saying it came from
        // the mouse so the scroll position is held.
        const list = create({ currentIndex: 0 })
        list.itemDoubleClicked(2, list.itemAtIndex(2))

        compare(list.lastFromKeyboard, true, "advances")
        compare(list.lastFromMouse, true, "and still holds the scroll")
    }

    // -- Sublists ----------------------------------------------------------

    function test_an_entry_with_a_sublist_url_is_still_handed_over() {
        const list = create({ currentIndex: 1 })
        keyClick(Qt.Key_Return)
        compare(list.lastData.subitems_url, "https://example.invalid/other.json")
    }

    function test_an_entry_with_inline_subitems_is_still_handed_over() {
        const list = create({ currentIndex: 3 })
        keyClick(Qt.Key_Return)
        compare(list.lastData.name, "Emulation and game OS")
        verify(list.lastData.subitems_json.length > 0)
    }

    function test_the_back_entry_is_handed_over_like_any_other() {
        const list = create({ currentIndex: 2 })
        keyClick(Qt.Key_Return)
        compare(list.lastData.subitems_url, "internal://back")
    }

    // -- Sideways navigation -----------------------------------------------

    function test_right_opens_the_highlighted_entry() {
        // Right is how a keyboard user descends into a sublist.
        const list = create({ currentIndex: 1 })
        keyClick(Qt.Key_Right)

        compare(list.rightCount, 1)
        verify(list.rightData, "the row is passed along, not just the index")
        compare(list.rightData.name, "Raspberry Pi OS (other)")
    }

    function test_right_with_nothing_highlighted_does_nothing() {
        const list = create({})
        compare(list.currentIndex, -1)

        keyClick(Qt.Key_Right)
        compare(list.rightCount, 0)
    }

    function test_left_goes_back_whether_or_not_anything_is_highlighted() {
        const list = create({})
        keyClick(Qt.Key_Left)
        compare(list.leftCount, 1)

        list.currentIndex = 2
        keyClick(Qt.Key_Left)
        compare(list.leftCount, 2)
    }

    function test_right_does_not_select_the_entry() {
        // Descending into a sublist is not choosing an OS to write.
        const list = create({ currentIndex: 1 })
        keyClick(Qt.Key_Right)

        compare(list.rightCount, 1)
        compare(list.handlerCalls, 0)
    }

    // -- Arrow navigation still works --------------------------------------

    function test_up_and_down_still_move_the_highlight() {
        const list = create({})
        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 0)

        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 2)

        keyClick(Qt.Key_Up)
        compare(list.currentIndex, 1)
    }
}
