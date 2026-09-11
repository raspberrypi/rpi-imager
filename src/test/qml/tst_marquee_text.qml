/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * MarqueeText: how a name too long for its column is shown.
 *
 * It carries the storage device description on the picker and the device
 * and OS names on the write summary -- the strings that tell someone what
 * is about to be erased. A row that cuts off mid-word with no indication it
 * has been cut is a row two similar drives look identical in.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "MarqueeText"
    when: windowShown
    width: 500
    height: 200
    visible: true

    readonly property string longName: "Generic Mass-Storage USB Device 0123456789 ABCDEFGH"
    readonly property string shortName: "SD"

    Component {
        id: marqueeComponent
        MarqueeText {
            width: 120
            height: 24
        }
    }

    function create(props) {
        const m = createTemporaryObject(marqueeComponent, testCase, props)
        verify(m, "the label was created")
        return m
    }

    // -- Knowing when the text does not fit --------------------------------

    function test_short_text_is_not_truncated() {
        const m = create({ text: testCase.shortName })
        verify(!m.truncated)
    }

    function test_long_text_is_truncated() {
        const m = create({ text: testCase.longName })
        verify(m.truncated, "a device name wider than its column")
    }

    function test_truncation_follows_the_column_getting_wider() {
        // These sit in fillWidth layouts, so the answer changes when the
        // window is resized rather than being decided once.
        const m = create({ text: testCase.longName })
        verify(m.truncated)

        m.width = m.implicitWidth + 50
        verify(!m.truncated, "given room, it is no longer cut off")

        m.width = 60
        verify(m.truncated, "and cut off again when the room goes")
    }

    function test_truncation_follows_the_text_changing() {
        // The drive list is repopulated as devices come and go.
        const m = create({ text: testCase.shortName })
        verify(!m.truncated)

        m.text = testCase.longName
        verify(m.truncated)
    }

    function test_empty_text_is_not_truncated() {
        const m = create({ text: "" })
        verify(!m.truncated)
    }

    // -- What it looks like sitting still ----------------------------------

    function test_text_that_does_not_fit_is_elided_rather_than_cut() {
        // ElideRight puts an ellipsis on the end. A hard clip would leave two
        // similar drive names looking identical up to the column edge.
        const m = create({ text: testCase.longName })
        verify(!m.scrolling)

        const label = m.children[0]
        verify(label, "the rendered label")
        compare(label.elide, Text.ElideRight)
    }

    function test_the_requested_alignment_is_used_when_static() {
        const m = create({
            text: testCase.shortName,
            horizontalAlignment: Text.AlignHCenter
        })
        compare(m.children[0].horizontalAlignment, Text.AlignHCenter)
    }

    function test_the_label_reports_the_width_the_whole_text_needs() {
        // implicitWidth is what a layout uses to decide how much to give it,
        // so it has to be the unelided width rather than what fits today.
        const m = create({ text: testCase.longName })
        verify(m.implicitWidth > m.width)
    }

    // -- Scrolling is hover-only -------------------------------------------

    function test_nothing_scrolls_without_a_pointer_over_it() {
        const m = create({ text: testCase.longName })
        verify(m.truncated)
        verify(!m.scrolling, "truncated is not enough on its own")
    }

    function test_short_text_never_scrolls_even_hovered() {
        const m = create({ text: testCase.shortName })
        mouseMove(m, m.width / 2, m.height / 2)

        verify(!m.scrolling, "there is nothing hidden to reveal")
    }

    function test_hovering_truncated_text_scrolls_it() {
        const m = create({ text: testCase.longName })
        verify(m.truncated)

        mouseMove(m, m.width / 2, m.height / 2)

        if (PlatformHelper.prefersReducedMotion) {
            verify(!m.scrolling,
                   "the desktop asked for reduced motion, so it stays still")
        } else {
            tryCompare(m, "scrolling", true)
            compare(m.children[0].elide, Text.ElideNone,
                    "the ellipsis goes while the full text is being revealed")
            compare(m.children[0].horizontalAlignment, Text.AlignLeft,
                    "and the reveal starts from the beginning")
        }
    }

    function test_the_text_returns_to_its_start_when_the_pointer_leaves() {
        // Otherwise the row is left showing the middle of a name.
        const m = create({ text: testCase.longName })
        mouseMove(m, m.width / 2, m.height / 2)
        mouseMove(m, -50, -50)

        tryCompare(m, "scrolling", false)
        tryCompare(m.children[0], "x", 0)
    }

    // -- What a screen reader is given -------------------------------------

    function test_the_whole_name_is_available_however_it_is_displayed() {
        // Eliding is a visual compromise. The accessible name has to be the
        // full string, or the one user who cannot see the column width is
        // the one told the least about which drive they picked.
        const m = create({ text: testCase.longName })
        verify(m.truncated)
        compare(m.Accessible.name, testCase.longName)
    }

    function test_the_rendered_label_is_not_announced_separately() {
        // Two accessible nodes for one string reads it out twice.
        const m = create({ text: testCase.longName })
        compare(m.children[0].Accessible.ignored, true)
    }
}
