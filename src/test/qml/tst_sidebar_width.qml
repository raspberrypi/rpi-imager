/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The width of the navigation sidebar, which the user can drag and which is
 * remembered between sessions.
 *
 * Every width that reaches the sidebar goes through clampSidebarWidth: the
 * drag handler clamps each mouse move, and the saved value is clamped again
 * when it is read back at startup. That is the right shape -- one function to
 * get right -- but it was not covered, and it had a hole.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "SidebarWidth"
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
    }

    function cleanupTestCase() {
        if (wiz) {
            wiz.destroy()
            wiz = null
        }
        ImageWriterSingleton.setSetting("sidebarWidth", "")
    }

    // Built fresh, because the saved width is read once at construction.
    function buildWith(setting) {
        ImageWriterSingleton.setSetting("sidebarWidth", setting)
        const w = containerComponent.createObject(testCase)
        verify(w, "the container was created")
        w.overlayRootRef = testCase
        // Sized, or a synthesised click has no geometry to land on.
        w.width = testCase.width
        w.height = testCase.height
        return w
    }

    // -- The clamp itself --------------------------------------------------

    function test_a_width_inside_the_bounds_is_left_alone() {
        const inside = Math.round((Style.sidebarMinWidth
                                   + Style.sidebarMaxWidth) / 2)
        compare(wiz.clampSidebarWidth(inside), inside)
    }

    function test_a_width_below_the_minimum_comes_back_as_the_minimum() {
        // The floor is what keeps the step labels readable and the splitter
        // reachable.
        compare(wiz.clampSidebarWidth(0), Style.sidebarMinWidth)
        compare(wiz.clampSidebarWidth(-500), Style.sidebarMinWidth)
        compare(wiz.clampSidebarWidth(Style.sidebarMinWidth - 1),
                Style.sidebarMinWidth)
    }

    function test_a_width_above_the_maximum_comes_back_as_the_maximum() {
        // The ceiling stops the sidebar squeezing the step itself out.
        compare(wiz.clampSidebarWidth(100000), Style.sidebarMaxWidth)
        compare(wiz.clampSidebarWidth(Style.sidebarMaxWidth + 1),
                Style.sidebarMaxWidth)
    }

    function test_the_bounds_leave_something_to_drag_between() {
        verify(Style.sidebarMinWidth > 0, "a sidebar always has some width")
        verify(Style.sidebarMaxWidth > Style.sidebarMinWidth,
               "and a range to drag within")
        verify(Style.sidebarWidth >= Style.sidebarMinWidth
               && Style.sidebarWidth <= Style.sidebarMaxWidth,
               "and the default sits inside its own bounds")
    }

    function test_a_width_that_is_not_a_number_comes_back_usable() {
        // The hole. Math.min/max pass NaN through, and an int property turns
        // that into 0 -- a sidebar with no width at all.
        const values = [NaN, undefined, null, Infinity, -Infinity, "wide"]
        for (let i = 0; i < values.length; i++) {
            const got = wiz.clampSidebarWidth(values[i])
            verify(typeof got === "number" && isFinite(got),
                   JSON.stringify(values[i]) + " has to clamp to a real "
                   + "number, got " + JSON.stringify(got))
            verify(got >= Style.sidebarMinWidth && got <= Style.sidebarMaxWidth,
                   JSON.stringify(values[i]) + " has to clamp inside the "
                   + "bounds, got " + got)
        }
    }

    // -- What comes back from the settings ---------------------------------

    function test_a_saved_width_is_restored() {
        const saved = Style.sidebarMinWidth + 10
        const w = buildWith(String(saved))
        compare(w.sidebarWidthValue, saved,
                "the width the user dragged to is remembered")
        w.destroy()
    }

    function test_a_saved_width_outside_the_bounds_is_brought_back_inside() {
        // A width saved under a different text scale can fall outside the
        // bounds this session computes.
        const w = buildWith("100000")
        compare(w.sidebarWidthValue, Style.sidebarMaxWidth)
        w.destroy()
    }

    function test_no_saved_width_leaves_the_default() {
        const w = buildWith("")
        compare(w.sidebarWidthValue, Style.sidebarWidth,
                "with nothing saved, the sidebar opens at its default width")
        w.destroy()
    }

    function test_a_corrupt_saved_width_does_not_take_the_sidebar_away() {
        // The one that matters. A setting that is not a number must not end up
        // as a sidebar of no width: the navigation would be gone on this launch
        // and every launch after, because the setting that caused it is still
        // there. The handle below is the only way back, and only for a user who
        // thinks to double-click a one-pixel separator.
        const w = buildWith("not-a-number")
        verify(w.sidebarWidthValue >= Style.sidebarMinWidth,
               "a setting that cannot be read must not remove the navigation; "
               + "the sidebar came back " + w.sidebarWidthValue + " wide")
        verify(w.sidebarWidthValue <= Style.sidebarMaxWidth)
        w.destroy()
    }

    // -- The way back -------------------------------------------------------

    // The handle is a sibling of the sidebar with its own width, so it is
    // found by the cursor it shows rather than by anything the sidebar owns.
    function findDragHandle(item) {
        if (!item)
            return null
        if (item.cursorShape !== undefined && item.cursorShape === Qt.SplitHCursor)
            return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findDragHandle(kids[i])
            if (found)
                return found
        }
        return null
    }

    function test_the_drag_handle_survives_a_sidebar_of_no_width() {
        // Which is what makes a bad width recoverable at all: the handle has
        // its own width in the layout, so it does not vanish with the sidebar
        // beside it.
        const w = buildWith("")
        w.sidebarWidthValue = 0

        const handle = findDragHandle(w)
        verify(handle !== null, "the resize handle is still there")
        verify(handle.width > 0, "and still has somewhere to be clicked")
        w.destroy()
    }

    function test_double_clicking_the_handle_restores_the_default() {
        // The escape hatch from a sidebar dragged, or restored, to a width
        // the user cannot work with.
        const w = buildWith(String(Style.sidebarMaxWidth))
        compare(w.sidebarWidthValue, Style.sidebarMaxWidth)

        const handle = findDragHandle(w)
        verify(handle !== null)
        // The layout has to settle before a synthesised click has anywhere to
        // land -- without this the click misses and the test reads as though
        // the handler did nothing.
        waitForRendering(w)
        mouseDoubleClickSequence(handle)

        compare(w.sidebarWidthValue, Style.sidebarWidth,
                "double-clicking the handle puts the sidebar back to default")
        compare(ImageWriterSingleton.getStringSetting("sidebarWidth"),
                String(Style.sidebarWidth),
                "and remembers that, so it does not come back wrong")
        w.destroy()
    }

    // -- Saving it ---------------------------------------------------------

    function test_the_width_is_written_where_it_is_read_from() {
        // The drag handler saves on release, and startup reads the same key.
        // A mismatch would silently stop remembering anything.
        const saved = Style.sidebarMinWidth + 20
        wiz.saveSidebarWidth(saved)
        compare(ImageWriterSingleton.getStringSetting("sidebarWidth"),
                String(saved))

        const w = buildWith(ImageWriterSingleton.getStringSetting("sidebarWidth"))
        compare(w.sidebarWidthValue, saved,
                "what was saved is what comes back")
        w.destroy()
    }
}
