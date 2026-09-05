/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * ImComboBox: the timezone, keyboard layout and Wi-Fi country dropdowns.
 *
 * These lists are long -- hundreds of timezones -- so the box is not really
 * scrolled, it is typed at. Someone in London types "lon" and expects
 * Europe/London. What they must not get is every entry with those three
 * letters buried somewhere inside it, which is why the match is anchored to
 * word boundaries rather than being a plain substring search.
 *
 * Getting this wrong sets the clock on a machine the user may never attach
 * a screen to.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ImComboBox"
    when: windowShown
    width: 400
    height: 300
    visible: true

    // Shaped like the real timezone list: region/city, some with underscores,
    // some with parenthesised qualifiers.
    readonly property var zones: [
        "Africa/Abidjan",
        "America/Argentina/Buenos_Aires",
        "America/New_York",
        "Atlantic/Reykjavik",
        "Europe/London",
        "Europe/Lisbon",
        "Europe/Paris",
        "Pacific/Auckland",
        "US/Pacific (deprecated)"
    ]

    Component {
        id: boxComponent
        ImComboBox {
            width: 300
            model: testCase.zones
            fullModelData: testCase.zones
        }
    }

    Component {
        id: spyComponent
        SignalSpy {}
    }

    function create(props) {
        const box = createTemporaryObject(boxComponent, testCase, props)
        verify(box, "the combo box was created")
        return box
    }

    function search(box, text) {
        box.searchString = ""
        box.performSearch(text)
        return box.filteredCount
    }

    // Walk the filtered list by picking each row in turn and reading back
    // which entry of the full list it mapped to. Going through the real
    // selection path rather than the private model, so the mapping from
    // filtered row to original index is exercised at the same time.
    function matchedZones(box, text) {
        const total = search(box, text)
        const out = []
        for (let i = 0; i < total; i++) {
            search(box, text)
            box.selectFilteredItem(i)
            out.push(testCase.zones[box.currentIndex])
        }
        return out
    }

    // -- Word-boundary matching --------------------------------------------

    function test_a_prefix_matches() {
        const box = create({})
        verify(box.wordBoundaryMatch("europe/london", "eur"))
    }

    function test_the_start_of_a_word_after_a_slash_matches() {
        // The whole point: "lon" has to find Europe/London even though the
        // string does not begin with it.
        const box = create({})
        verify(box.wordBoundaryMatch("Europe/London", "lon"))
    }

    function test_the_middle_of_a_word_does_not_match() {
        // "kja" sits inside Reykjavik. Matching it would bury the entry the
        // user actually wants under noise.
        const box = create({})
        verify(!box.wordBoundaryMatch("Atlantic/Reykjavik", "kja"))
    }

    function test_matching_ignores_the_case_of_the_entry() {
        // The search text arrives already lowercased by performSearch.
        const box = create({})
        verify(box.wordBoundaryMatch("EUROPE/LONDON", "lon"))
    }

    function test_a_word_after_a_bracket_or_slash_matches() {
        const box = create({})
        verify(box.wordBoundaryMatch("US/Pacific (deprecated)", "dep"),
               "after an opening bracket")
        verify(box.wordBoundaryMatch("US/Pacific (deprecated)", "pac"),
               "after a slash")
    }

    function test_a_search_longer_than_the_entry_does_not_match() {
        const box = create({})
        verify(!box.wordBoundaryMatch("Europe/London", "europe/london/extra"))
    }

    function test_an_underscore_separates_words() {
        // The IANA names spell spaces as underscores. Someone in New York
        // types "york"; without underscore in the boundary class the box
        // says "No matches", and 65 zones are affected -- Los_Angeles,
        // Hong_Kong, Sao_Paulo, Mexico_City among them.
        const box = create({})
        verify(box.wordBoundaryMatch("America/New_York", "york"))
        verify(box.wordBoundaryMatch("America/Los_Angeles", "angeles"))
        verify(box.wordBoundaryMatch("Asia/Hong_Kong", "kong"))
        verify(box.wordBoundaryMatch("America/Argentina/Buenos_Aires", "aires"))
    }

    function test_a_later_word_boundary_is_found_after_a_mid_word_hit() {
        // The loop has to keep looking past a hit that is not on a boundary
        // rather than giving up at the first one it finds. "an" appears
        // mid-word in "Argentina" before it starts a word in "Antarctica".
        const box = create({})
        verify(box.wordBoundaryMatch("America/Argentina/Antarctica", "antar"))
    }

    // -- Filtering the list ------------------------------------------------

    function test_an_empty_search_shows_everything() {
        const box = create({})
        box.searchString = ""
        box.rebuildFilteredModel()
        compare(box.filteredCount, testCase.zones.length)
    }

    function test_typing_narrows_the_list() {
        const box = create({})
        compare(matchedZones(box, "lon"), ["Europe/London"])
    }

    function test_typing_a_city_name_after_an_underscore_finds_it() {
        // The end-to-end version of the boundary rule, through the filter
        // rather than the matcher: this is what the person typing sees.
        const box = create({})
        compare(matchedZones(box, "york"), ["America/New_York"])
    }

    function test_a_search_matching_several_entries_keeps_them_all() {
        const box = create({})
        compare(matchedZones(box, "europe"),
                ["Europe/London", "Europe/Lisbon", "Europe/Paris"])
    }

    function test_a_search_matching_nothing_empties_the_list() {
        const box = create({})
        compare(search(box, "atlantis"), 0)
    }

    function test_typing_accumulates_rather_than_replacing() {
        // Each keystroke appends: "l", then "i", then "s" -- the list narrows
        // at each step rather than searching for the last letter alone.
        const box = create({})
        box.searchString = ""

        box.performSearch("l")
        const afterL = box.filteredCount
        verify(afterL >= 2, "several entries have a word starting with l")

        box.performSearch("i")
        box.performSearch("s")
        compare(box.searchString, "lis")
        compare(box.filteredCount, 1)

        box.selectFilteredItem(0)
        compare(testCase.zones[box.currentIndex], "Europe/Lisbon")
    }

    function test_a_search_is_lowercased_as_it_is_typed() {
        const box = create({})
        box.searchString = ""
        box.performSearch("LON")
        compare(box.searchString, "lon")
        compare(box.filteredCount, 1)
    }

    function test_backspace_widens_the_list_again() {
        const box = create({})
        box.searchString = ""
        box.performSearch("lis")
        compare(box.filteredCount, 1)

        box.handleBackspace()
        box.handleBackspace()
        compare(box.searchString, "l")
        verify(box.filteredCount > 1, "backing out shows the wider list again")
    }

    function test_backspace_on_an_empty_search_is_harmless() {
        const box = create({})
        box.searchString = ""
        box.rebuildFilteredModel()
        const before = box.filteredCount

        box.handleBackspace()
        compare(box.searchString, "")
        compare(box.filteredCount, before)
    }

    // -- Choosing an entry -------------------------------------------------

    function test_choosing_a_filtered_entry_selects_the_right_original() {
        // The filtered list is renumbered from zero, so picking row 0 of a
        // filtered list has to map back to the entry's index in the full
        // list, not to entry 0. Getting this wrong sets a timezone the user
        // never saw, with nothing on screen afterwards to reveal it.
        const box = create({})
        compare(search(box, "auck"), 1)

        const spy = spyComponent.createObject(box, {
            target: box, signalName: "activated"
        })

        box.selectFilteredItem(0)
        compare(box.currentIndex, testCase.zones.indexOf("Pacific/Auckland"))
        compare(spy.count, 1, "the step is told the selection changed")
        compare(spy.signalArguments[0][0], box.currentIndex)
        spy.destroy()
    }

    function test_choosing_the_second_of_several_matches() {
        const box = create({})
        compare(search(box, "europe"), 3)

        box.selectFilteredItem(1)
        compare(testCase.zones[box.currentIndex], "Europe/Lisbon")
    }

    // -- Opening the dropdown ----------------------------------------------

    function test_opening_the_dropdown_snapshots_the_model() {
        // fullModelData is filled from the model each time the popup opens,
        // so a box whose list is built after construction -- the timezone
        // list is fetched, not hardcoded -- still filters over the real
        // entries rather than over an empty snapshot.
        const box = create({ fullModelData: [] })
        compare(box.fullModelData.length, 0)

        box.popup.open()
        tryCompare(box.popup, "visible", true)
        compare(box.fullModelData.length, testCase.zones.length)
        compare(box.filteredCount, testCase.zones.length)

        box.popup.close()
    }

    function test_reopening_the_dropdown_clears_the_previous_search() {
        // Otherwise the list comes back still filtered by whatever was typed
        // last time, showing one entry out of hundreds with no visible reason.
        const box = create({})
        box.popup.open()
        tryCompare(box.popup, "visible", true)

        box.performSearch("lis")
        compare(box.searchString, "lis")
        compare(box.filteredCount, 1)

        box.popup.close()
        tryCompare(box.popup, "visible", false)
        box.popup.open()
        tryCompare(box.popup, "visible", true)

        compare(box.searchString, "", "the search box starts empty again")
        compare(box.filteredCount, testCase.zones.length,
                "and the whole list is back")
        box.popup.close()
    }

    function test_opening_the_dropdown_remembers_what_was_selected() {
        // originalIndex is what Escape restores to.
        const box = create({ currentIndex: 4 })
        box.popup.open()
        tryCompare(box.popup, "visible", true)
        compare(box.originalIndex, 4)
        box.popup.close()
    }

    function test_choosing_a_row_that_is_not_there_selects_nothing() {
        const box = create({ currentIndex: 2 })
        compare(search(box, "atlantis"), 0)

        box.selectFilteredItem(0)
        compare(box.currentIndex, 2, "the previous selection is left alone")
    }
}
