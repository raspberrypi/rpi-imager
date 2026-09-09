/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * LocaleCustomizationStep: the keyboard layout, mainly.
 *
 * Choosing a capital city fills in the timezone and the keyboard, which is
 * the convenience -- and the guard on it is what matters. Once someone has
 * set the keyboard by hand, a later change of city must not overwrite it.
 * A wrong layout is not a cosmetic problem: the password typed into the
 * account step may not be typeable at the login prompt, and the board is
 * then unreachable by keyboard and by SSH alike.
 *
 * The step was at 0%: nothing had ever instantiated it.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "LocaleStep"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property var customizationSettings: ({})
        property bool localeConfigured: false
        property bool hostnameConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property string networkInfoText: ""
        property int stepWriting: 9
        property int jumpedTo: -1
        function jumpToStep(n) { jumpedTo = n }
    }

    Component {
        id: stepComponent
        LocaleCustomizationStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    // Taken from the shipped list rather than hardcoded, so the case tests
    // the wiring and not a guess about which cities are in the resource.
    property string aCity: ""
    property var itsLocale: null

    function initTestCase() {
        var cities = ImageWriterSingleton.getCapitalCitiesList()
        verify(cities && cities.length > 0, "the capital city list loaded")
        for (var i = 0; i < cities.length; i++) {
            var d = ImageWriterSingleton.getLocaleDataForCapital(cities[i])
            if (d && d.timezone && d.keyboard && d.countryCode) {
                aCity = cities[i]
                itsLocale = d
                break
            }
        }
        verify(aCity.length > 0, "found a city with a full locale entry")
    }

    function init() {
        fakeContainer.customizationSettings = ({})
        fakeContainer.localeConfigured = false
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
    }

    function cleanup() {
        if (step) {
            step.destroy()
            step = null
        }
    }

    function child(name) {
        var c = findChild(step, name)
        verify(c, "found " + name)
        return c
    }

    function chooseCity(name) {
        var combo = child("localeCapitalCityCombo")
        var idx = combo.find(name)
        verify(idx !== -1, "the city is in the list: " + name)
        combo.currentIndex = idx
        step.onCapitalCityChanged()
    }

    // ── Choosing a city fills the rest in ─────────────────────────────

    function test_choosing_a_city_fills_the_timezone_and_keyboard() {
        chooseCity(aCity)

        compare(child("localeTimezoneCombo").editText, itsLocale.timezone)
        compare(child("localeKeyboardCombo").editText, itsLocale.keyboard)
    }

    function test_choosing_a_city_records_the_wifi_country_it_implies() {
        // Kept for the Wi-Fi step: the regulatory domain decides which
        // channels the radio may use, and a wrong one can leave the board
        // unable to see the network at all.
        chooseCity(aCity)

        compare(fakeContainer.customizationSettings.recommendedWifiCountry,
                itsLocale.countryCode)
    }

    // ── A hand-set layout is not overwritten ──────────────────────────

    function test_a_keyboard_set_by_hand_survives_a_later_city_choice() {
        // The case that matters. Someone who fixed their layout by hand and
        // then adjusted the city must not silently get the city's layout
        // back -- they would find out at the login prompt.
        step.userChangedKeyboard = true
        child("localeKeyboardCombo").editText = "dvorak"

        chooseCity(aCity)

        compare(child("localeKeyboardCombo").editText, "dvorak",
                "the hand-set layout was kept")
    }

    function test_a_timezone_set_by_hand_survives_a_later_city_choice() {
        step.userChangedTimezone = true
        child("localeTimezoneCombo").editText = "Etc/UTC"

        chooseCity(aCity)

        compare(child("localeTimezoneCombo").editText, "Etc/UTC")
    }

    function test_an_untouched_field_is_still_filled_when_the_other_is_not() {
        // The two guards are independent: fixing the keyboard by hand must
        // not stop the city filling in the timezone.
        step.userChangedKeyboard = true
        child("localeKeyboardCombo").editText = "dvorak"

        chooseCity(aCity)

        compare(child("localeKeyboardCombo").editText, "dvorak")
        compare(child("localeTimezoneCombo").editText, itsLocale.timezone,
                "the timezone was still filled in")
    }

    // ── What leaving the step writes ──────────────────────────────────

    function test_the_choices_are_written_and_marked_configured() {
        chooseCity(aCity)

        step.nextClicked()

        var s = fakeContainer.customizationSettings
        compare(s.timezone, itsLocale.timezone)
        compare(s.keyboard, itsLocale.keyboard)
        compare(s.capitalCity, aCity)
        verify(fakeContainer.localeConfigured)
    }

    function test_clearing_the_fields_removes_the_settings() {
        chooseCity(aCity)
        step.nextClicked()
        verify(fakeContainer.localeConfigured)

        child("localeTimezoneCombo").editText = ""
        child("localeKeyboardCombo").editText = ""
        child("localeCapitalCityCombo").editText = ""
        step.nextClicked()

        var s = fakeContainer.customizationSettings
        verify(s.timezone === undefined, "the timezone was removed")
        verify(s.keyboard === undefined, "so was the keyboard")
        verify(s.capitalCity === undefined)
        verify(!fakeContainer.localeConfigured)
    }

    function test_a_city_on_its_own_does_not_count_as_configured() {
        // The city is only a way of filling the other two in. With both of
        // them empty there is nothing for the image to apply, so the step
        // must not claim the section was configured.
        child("localeCapitalCityCombo").editText = aCity
        child("localeTimezoneCombo").editText = ""
        child("localeKeyboardCombo").editText = ""

        step.nextClicked()

        compare(fakeContainer.customizationSettings.capitalCity, aCity)
        verify(!fakeContainer.localeConfigured)
    }

    // ── Using the controls, rather than setting the flags ─────────────
    //
    // The cases above set userChangedKeyboard and userChangedTimezone
    // directly, which says what the step does with them and nothing about
    // how they come to be set. The three handlers that set them -- one per
    // combo box -- were uncovered.
    //
    // That is the whole hop the user actually performs. Drop the keyboard
    // combo's handler and every case above still passes, while a real user
    // who picks their layout by hand has it silently replaced the next time
    // they touch the city, and finds out at the login prompt of a board
    // they can no longer reach.
    //
    // Picking from a combo box is the `activated` signal, which Qt reserves
    // for a choice a person made: assigning currentIndex does not raise it,
    // exactly as assigning `checked` is not a click. So the index is set and
    // the signal raised, in that order, which is what the control does.

    function pick(comboName, text) {
        var combo = child(comboName)
        var idx = combo.find(text)
        verify(idx !== -1, "the list offers " + text)
        combo.currentIndex = idx
        combo.activated(idx)
        return combo
    }

    function anotherEntry(comboName, notThis) {
        // Any entry in the list that is not the one already showing, so the
        // choice is a change rather than a no-op.
        var combo = child(comboName)
        for (var i = 0; i < combo.count; i++) {
            var t = combo.textAt(i)
            if (t !== notThis)
                return t
        }
        return ""
    }

    function test_picking_a_city_from_the_list_fills_the_rest_in() {
        // The convenience itself, through the control rather than by
        // calling the handler. Without it the two fields below stay empty
        // however many cities the user tries.
        var combo = child("localeCapitalCityCombo")
        var idx = combo.find(aCity)
        verify(idx !== -1)
        combo.currentIndex = idx

        combo.activated(idx)

        // The handler is deferred with Qt.callLater so the combo's text has
        // settled before it is read.
        tryVerify(function () {
            return child("localeTimezoneCombo").editText === itsLocale.timezone
        }, 3000, "the timezone was filled in")
        compare(child("localeKeyboardCombo").editText, itsLocale.keyboard)
    }

    function test_a_keyboard_picked_from_the_list_is_not_overwritten() {
        // The end-to-end version of the case that matters.
        var other = anotherEntry("localeKeyboardCombo", itsLocale.keyboard)
        verify(other.length > 0, "the list has a layout other than the "
               + "recommended one")

        pick("localeKeyboardCombo", other)
        chooseCity(aCity)

        compare(child("localeKeyboardCombo").editText, other,
                "the layout the user picked survived the city choice")
    }

    function test_a_timezone_picked_from_the_list_is_not_overwritten() {
        var other = anotherEntry("localeTimezoneCombo", itsLocale.timezone)
        verify(other.length > 0, "the list has a zone other than the "
               + "recommended one")

        pick("localeTimezoneCombo", other)
        chooseCity(aCity)

        compare(child("localeTimezoneCombo").editText, other)
    }

    function test_picking_the_keyboard_does_not_pin_the_timezone_too() {
        // The two flags are separate, and a user who fixed one still wants
        // the other filled in for them.
        var other = anotherEntry("localeKeyboardCombo", itsLocale.keyboard)
        verify(other.length > 0)

        pick("localeKeyboardCombo", other)
        chooseCity(aCity)

        compare(child("localeKeyboardCombo").editText, other)
        compare(child("localeTimezoneCombo").editText, itsLocale.timezone,
                "the timezone was still filled in from the city")
    }
}
