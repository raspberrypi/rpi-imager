/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * LanguageSelectionStep: the first screen, and the only one whose combo box
 * shows something different from what it sends to the writer.
 *
 * The step was at 0%. It keeps two parallel lists: the internal translation
 * names the writer understands, and the display names shown to the user --
 * identical except that "English" is shown as "English (British)", to
 * distinguish it from the "American English" that sits next to it in the
 * list. Everything the user sees comes from the second list; everything
 * handed to changeLanguage and stored as savedLanguage has to come from the
 * first. Send a display name to changeLanguage and there is no translation by
 * that name, so the language silently does not change.
 *
 * The other thing worth pinning is the preselection. The combo has to open on
 * the language the application is already running in, because pressing Next
 * persists whatever the combo happens to be showing. If it opened on English
 * regardless, a user who had chosen Deutsch would find the wizard in German
 * with "English (British)" in the box, and moving forward would quietly
 * change their language back.
 *
 * The singleton is not injectable here -- the step calls it directly -- but
 * the test harness points QSettings at an "rpi-imager-tests" organisation, so
 * what onNextClicked persists can be read straight back. The entry it is made
 * to choose is the language the application is already running in, so
 * changeLanguage is inert and the rest of the suite is not left running in
 * another language.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "LanguageSelectionStep"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        // WizardStepBase reads this on every step; undefined assigns nothing
        // and warns on each construction.
        property string networkInfoText: ""
        property int steps: 0
        function nextStep() { steps++ }
    }

    Component {
        id: stepComponent
        LanguageSelectionStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    function init() {
        fakeContainer.steps = 0
        step = createTemporaryObject(stepComponent, testCase)
        verify(step !== null, "the step has to instantiate")
    }

    function cleanup() {
        step = null
    }

    function combo() {
        const c = findCombo(step)
        verify(c !== null, "the step has a language combo box")
        return c
    }

    function findCombo(root) {
        if (!root)
            return null
        if (root.currentIndex !== undefined && root.model !== undefined
                && root.popup !== undefined)
            return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findCombo(kids[i])
            if (found)
                return found
        }
        return null
    }

    // -- The screen comes up with something to choose from -----------------

    function test_the_first_screen_offers_the_languages_the_app_has() {
        const available = ImageWriterSingleton.getTranslations()
        verify(available.length > 1,
               "this test is meaningless without translations installed")

        const c = combo()
        compare(c.model.length, available.length,
                "every translation the application has is offered")
        compare(step.showBackButton, false,
                "there is nothing before the first screen")
    }

    // -- The two lists, and which one the writer is given ------------------

    function test_british_english_is_labelled_but_not_renamed() {
        const c = combo()

        // What the user sees.
        verify(c.model.indexOf("English (British)") >= 0,
               "the plain 'English' translation is labelled British, so it is "
               + "not confused with the American English beside it")
        compare(c.model.indexOf("English"), -1,
                "and the bare label is not also shown")

        // What the writer would be given for that entry. This is the whole
        // point of keeping two lists: there is no translation called
        // "English (British)", so sending the label would change nothing.
        const shown = c.model.indexOf("English (British)")
        compare(step._internalLanguages[shown], "English",
                "the writer is given the translation's real name")
    }

    function test_every_other_language_is_shown_under_its_own_name() {
        const c = combo()

        for (let i = 0; i < c.model.length; i++) {
            const internal = step._internalLanguages[i]
            if (internal === "English")
                continue
            compare(c.model[i], internal,
                    "only English is relabelled; " + internal
                    + " is shown as itself")
        }
    }

    function test_the_two_lists_stay_the_same_length_and_in_step() {
        // They are indexed by the same number, so a mismatch would hand the
        // writer the name of a different language than the one chosen.
        const c = combo()
        compare(step._internalLanguages.length, c.model.length,
                "one internal name per entry shown")

        const available = ImageWriterSingleton.getTranslations()
        for (let i = 0; i < available.length; i++)
            compare(step._internalLanguages[i], available[i],
                    "and in the order the application gave them")
    }

    // -- Opening on the language already in use ----------------------------

    function test_the_combo_opens_on_the_language_already_running() {
        const current = ImageWriterSingleton.getCurrentLanguage()
        verify(current && current.length > 0,
               "this test needs a current language to check against")

        const c = combo()
        verify(c.currentIndex >= 0, "something has to be selected")
        compare(step._internalLanguages[c.currentIndex], current,
                "the box opens on the language the application is running in, "
                + "because moving forward persists whatever it shows")
    }

    function test_the_selection_is_never_left_unset() {
        // Whatever the current language turns out to be, including a value
        // that is not in the list at all, the combo must land on a real entry
        // rather than on -1. A combo at -1 shows an empty box, and Next would
        // then read a negative index.
        const c = combo()
        verify(c.currentIndex >= 0)
        verify(c.currentIndex < c.model.length)
        verify(String(c.currentText).length > 0,
               "and the box is not blank")
    }

    // -- Moving on ---------------------------------------------------------

    function test_moving_on_with_no_valid_selection_changes_nothing() {
        // An index outside the list must not be used to look up a language:
        // doing so would hand undefined to changeLanguage and store it as the
        // saved language, so the next launch would come up with none.
        //
        // onNextClicked guards this twice over -- on the index and again on
        // the name it looks up -- so removing either one alone leaves the
        // behaviour unchanged. What is asserted here is the behaviour: an
        // invalid selection changes nothing.
        const c = combo()
        const before = ImageWriterSingleton.getCurrentLanguage()

        c.currentIndex = -1
        step.nextClicked()
        compare(ImageWriterSingleton.getCurrentLanguage(), before,
                "an unset selection leaves the language alone")

        c.currentIndex = step._internalLanguages.length + 5
        step.nextClicked()
        compare(ImageWriterSingleton.getCurrentLanguage(), before,
                "and so does an index past the end of the list")
    }

    function test_moving_on_saves_the_internal_name_not_the_label() {
        // The one that matters. savedLanguage is read back on the next launch
        // and handed to the translator, so storing the display name there
        // would come up with no language loaded -- there is no translation
        // called "English (British)".
        //
        // The entry chosen is the one the application is already running in,
        // so changeLanguage is a no-op and the rest of the suite is not left
        // running in another language. Settings are isolated to the
        // "rpi-imager-tests" organisation, so what this writes is a test file.
        const c = combo()
        const shown = c.model.indexOf("English (British)")
        verify(shown >= 0, "this test is about the relabelled entry")
        compare(step._internalLanguages[shown], "English",
                "and the current language, so nothing actually changes")

        ImageWriterSingleton.setSetting("savedLanguage", "")
        c.currentIndex = shown
        step.nextClicked()

        compare(ImageWriterSingleton.getStringSetting("savedLanguage"), "English",
                "the translation's real name is what gets remembered, not the "
                + "label the user was shown")
    }

    function test_every_entry_has_a_name_the_application_recognises() {
        // Whichever entry is chosen, the name moving on would use has to be
        // one the translator will accept.
        const c = combo()
        const available = ImageWriterSingleton.getTranslations()

        for (let i = 0; i < c.model.length; i++) {
            const internal = step._internalLanguages[i]
            verify(internal !== undefined && String(internal).length > 0,
                   "entry " + i + " (" + c.model[i]
                   + ") has an internal name to send")
            verify(available.indexOf(internal) >= 0,
                   internal + " is a translation the application recognises")
        }
    }

    // -- Choosing from the list --------------------------------------------

    function test_choosing_a_language_is_described_for_a_screen_reader() {
        const c = combo()
        verify(String(c.Accessible.description).length > 0,
               "the combo says what it is for, since its label is only "
               + "focusable when a screen reader is running")
    }
}
