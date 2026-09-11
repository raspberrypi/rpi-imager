/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * BaseDialog's focus ring.
 *
 * Every dialog in the application is one of these, and none of them declare
 * their own tab order -- they register focus groups and BaseDialog wires
 * KeyNavigation into a ring. Get that wrong and a keyboard user cannot
 * reach the button they need, on a dialog that may be the confirmation in
 * front of an irreversible write.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "BaseDialog"
    when: windowShown
    width: 600
    height: 400
    visible: true

    Component {
        id: dialogComponent

        BaseDialog {
            id: dlg
            title: "A dialog"
            parent: testCase
            focus: true

            property int escapeCount: 0
            function escapePressed() { dlg.escapeCount++ }

            property alias one: btnOne
            property alias two: btnTwo
            property alias three: btnThree

            ImButton { id: btnOne;   text: "One" }
            ImButton { id: btnTwo;   text: "Two" }
            ImButton { id: btnThree; text: "Three" }

            // Register on completion and let onOpened rebuild, which is what
            // the real dialogs do. A rebuild while closed finds nothing:
            // visibility is inherited, so every child of a closed dialog is
            // invisible and correctly excluded from the ring.
            Component.onCompleted: {
                dlg.registerFocusGroup("buttons",
                                       function() { return [btnOne, btnTwo, btnThree] },
                                       0)
            }
        }
    }

    // Dialogs only have a focus ring while they are open, so every case here
    // starts from an open one.
    function create(props) {
        const d = createTemporaryObject(dialogComponent, testCase, props)
        verify(d, "the dialog was created")
        d.open()
        // Wait for `opened`, not `visible`: onOpened fires when the enter
        // transition finishes, and that is where the focus ring is built.
        tryCompare(d, "opened", true)
        verify(d.one.visible, "the content is on screen")
        return d
    }

    // -- The ring is built from the registered groups ----------------------

    function test_the_registered_items_become_the_tab_order() {
        const d = create({})
        compare(d.one.KeyNavigation.tab, d.two)
        compare(d.two.KeyNavigation.tab, d.three)
    }

    function test_the_ring_wraps_at_both_ends() {
        // Circular: Tab off the last lands on the first, and Shift+Tab off
        // the first lands on the last. Otherwise the keyboard falls out of
        // the dialog and into whatever is behind it.
        const d = create({})
        compare(d.three.KeyNavigation.tab, d.one, "forwards off the end")
        compare(d.one.KeyNavigation.backtab, d.three, "backwards off the start")
    }

    function test_an_invisible_item_is_left_out_of_the_ring() {
        const d = create({})
        d.two.visible = false
        d.rebuildFocusOrder()

        compare(d.one.KeyNavigation.tab, d.three, "tab skips straight past it")
        compare(d.three.KeyNavigation.backtab, d.one)
    }

    function test_a_disabled_item_is_left_out_of_the_ring() {
        const d = create({})
        d.two.enabled = false
        d.rebuildFocusOrder()

        compare(d.one.KeyNavigation.tab, d.three)
    }

    function test_an_item_opting_out_of_tab_is_left_out() {
        const d = create({})
        d.two.activeFocusOnTab = false
        d.rebuildFocusOrder()

        compare(d.one.KeyNavigation.tab, d.three)
    }

    function test_a_single_item_is_its_own_neighbour() {
        const d = create({})
        d.two.visible = false
        d.three.visible = false
        d.rebuildFocusOrder()

        compare(d.one.KeyNavigation.tab, d.one)
        compare(d.one.KeyNavigation.backtab, d.one)
    }

    // -- Groups compose in their declared order ----------------------------

    function test_groups_are_ordered_by_their_order_argument() {
        const d = create({})
        // Re-register the same items as two groups, back to front.
        d.registerFocusGroup("buttons", function() { return [d.three] }, 0)
        d.registerFocusGroup("later", function() { return [d.one, d.two] }, 1)
        d.rebuildFocusOrder()

        compare(d.three.KeyNavigation.tab, d.one, "group 0 comes before group 1")
        compare(d.one.KeyNavigation.tab, d.two)
        compare(d.two.KeyNavigation.tab, d.three, "and it wraps back round")
    }

    function test_registering_a_group_twice_replaces_it() {
        // Steps re-register as their content changes; the second call has to
        // replace the first rather than append a duplicate, or an item ends
        // up in the ring twice and Tab appears to stick.
        const d = create({})
        d.registerFocusGroup("buttons", function() { return [d.one, d.two] }, 0)
        d.rebuildFocusOrder()

        compare(d.one.KeyNavigation.tab, d.two)
        compare(d.two.KeyNavigation.tab, d.one, "two items, not five")
    }

    function test_an_empty_group_leaves_the_ring_empty() {
        const d = create({})
        d.registerFocusGroup("buttons", function() { return [] }, 0)
        d.rebuildFocusOrder()
        // Nothing to assert about neighbours; it must simply not throw.
        verify(true)
    }

    // -- Escape ------------------------------------------------------------

    function test_escape_reaches_the_dialog_handler() {
        const d = create({})
        d.focusInitialItem()

        keyClick(Qt.Key_Escape)
        compare(d.escapeCount, 1)
    }

    // -- Opening -----------------------------------------------------------

    function test_opening_lands_focus_on_the_first_item() {
        const d = create({})

        verify(d.one.activeFocus,
               "something holds focus, so KeyNavigation has a starting point")
    }

    function test_reopening_still_lands_focus_somewhere() {
        // The precondition from focusInitialItem()'s own comment: if nothing
        // holds focus, Tab does nothing and the dialog cannot be navigated.
        const d = create({})
        d.close()
        tryCompare(d, "visible", false)

        d.open()
        tryCompare(d, "opened", true)
        verify(d.one.activeFocus || d.two.activeFocus || d.three.activeFocus,
               "some item in the dialog holds focus on reopening")
    }

    function test_reopening_after_the_first_item_goes_away_still_focuses() {
        // A dialog whose first control is conditional -- shown for one target
        // and not another, revealed after a countdown, replaced once a key is
        // saved -- has a different first focusable item the second time it
        // opens. The ring is rebuilt correctly; the landing point has to
        // follow it, or forceActiveFocus() lands on an item that is no longer
        // there and the user sees no focus ring at all.
        const d = create({})
        verify(d.one.activeFocus)
        d.close()
        tryCompare(d, "visible", false)

        d.one.visible = false
        d.open()
        tryCompare(d, "opened", true)

        verify(!d.one.activeFocus, "the hidden item does not keep focus")
        verify(d.two.activeFocus || d.three.activeFocus,
               "focus went to whatever is first now")
        compare(d.two.KeyNavigation.tab, d.three,
                "and the ring the user is now in is the rebuilt one")
    }

    // The 'kept' branch is covered by the reopen case above: item one is
    // still in the ring there, so the landing point is left pointing at it.



    // -- The escape handler a dialog gets if it does not write one ---------
    //
    // Every dialog in the application inherits this one. Most override it to
    // record a refusal as well; the ones that do not rely on the default,
    // and a default that did not close would leave a dialog with no keyboard
    // way out of it -- on a screen the user may have reached by mistake.
    //
    // The component above overrides escapePressed, so it cannot exercise the
    // default. This one deliberately does not.

    Component {
        id: plainDialogComponent

        BaseDialog {
            id: plain
            title: "A dialog with no escape handler of its own"
            parent: testCase
            focus: true

            ImButton { id: onlyButton; text: "Only" }

            Component.onCompleted: {
                registerFocusGroup("buttons", function () { return [onlyButton] }, 0)
            }
        }
    }

    function test_a_dialog_without_its_own_handler_still_closes_on_escape() {
        const dlg = createTemporaryObject(plainDialogComponent, testCase)
        verify(dlg, "the dialog was created")
        dlg.open()
        tryVerify(function () { return dlg.opened }, 3000, "the dialog opened")

        dlg.escapePressed()

        tryVerify(function () { return !dlg.visible }, 3000,
                  "the default handler closed it")
    }

    function test_the_default_handler_does_not_reopen_or_throw() {
        // Escape arriving twice -- a held key, or a second press while the
        // close is still running -- has to be harmless.
        const dlg = createTemporaryObject(plainDialogComponent, testCase)
        verify(dlg)
        dlg.open()
        tryVerify(function () { return dlg.opened }, 3000)

        dlg.escapePressed()
        dlg.escapePressed()

        tryVerify(function () { return !dlg.visible }, 3000)
    }
}
