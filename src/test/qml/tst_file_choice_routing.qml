/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Which chooser a chosen file belongs to.
 *
 * There is one native file dialog in this application and one signal
 * reporting what came back from it, and more than one place asks. The OS
 * selection step asks for a custom image; the repository dialog, which lives
 * inside the options dialog and is therefore reachable from the OS step,
 * asks for a repository json file. Both listened to the same broadcast and
 * neither checked whether it was the one that asked.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "FileChoiceRouting"
    when: windowShown
    width: 900
    height: 700
    visible: true

    // The real arrangement: the wizard sitting on the OS selection step,
    // with a repository dialog alive at the same time. Both are listening to
    // the singleton, so one emit reaches both -- which is the whole point.
    Component {
        id: containerComponent
        WizardContainer {}
    }

    QtObject {
        id: fakeContainer
        property bool disableWarnings: false
    }

    Component {
        id: repoDialogComponent
        RepositoryDialog {
            wizardContainer: fakeContainer
        }
    }

    property var wiz: null
    property var repoDialog: null

    readonly property string osName: "Raspberry Pi OS (64-bit)"
    readonly property string repoFile: "file:///tmp/imager-test/my-repo.json"
    readonly property string imageFile: "file:///tmp/imager-test/my-image.img"

    function init() {
        wiz = containerComponent.createObject(testCase)
        verify(wiz, "the wizard container was created")
        wiz.overlayRootRef = testCase
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
        wiz.jumpToStep(wiz.stepOSSelection)
        wiz.customizationSupported = true
        wiz.selectedOsName = osName
        wiz.wifiConfigured = true
        wiz.userConfigured = true
        wiz.sshEnabled = true

        repoDialog = repoDialogComponent.createObject(testCase)
        verify(repoDialog, "the repository dialog was created")
        repoDialog.selectedRepo = ""
        wait(100)
    }

    function cleanup() {
        if (repoDialog) {
            repoDialog.destroy()
            repoDialog = null
        }
        if (wiz) {
            wiz.destroy()
            wiz = null
        }
    }

    function choose(url, purpose) {
        ImageWriterSingleton.fileSelected(url, purpose)
        wait(200)
    }

    // -- A repository file is not an operating system ----------------------

    function test_choosing_a_repository_leaves_the_chosen_os_alone() {
        choose(repoFile, "repository")

        compare(wiz.selectedOsName, osName,
                "the OS the user picked is still the OS")
    }

    function test_choosing_a_repository_leaves_the_customisation_alone() {
        // The part that costs the user work rather than a second click.
        choose(repoFile, "repository")

        verify(wiz.wifiConfigured, "the wireless network is still configured")
        verify(wiz.userConfigured, "and the user account")
        verify(wiz.sshEnabled, "and SSH")
        verify(wiz.customizationSupported,
               "and the image is still one that can carry it")
    }

    function test_choosing_a_repository_reaches_the_repository_dialog() {
        // The other half: it has to arrive somewhere.
        choose(repoFile, "repository")

        compare(String(repoDialog.selectedRepo), repoFile)
    }

    // -- A custom image is not a repository -------------------------------

    function test_choosing_a_custom_image_becomes_the_chosen_os() {
        choose(imageFile, "customImage")

        compare(wiz.selectedOsName, "my-image.img",
                "the file the user chose is what will be written")
    }

    function test_choosing_a_custom_image_clears_what_it_cannot_carry() {
        // Correct behaviour, and the reason the misrouting above was
        // destructive: this handler is meant to throw the customisation
        // away, because a custom image has nowhere to put it.
        choose(imageFile, "customImage")

        verify(!wiz.customizationSupported,
               "a custom image does not advertise customisation")
        verify(!wiz.wifiConfigured, "so the staged settings go")
        verify(!wiz.userConfigured)
        verify(!wiz.sshEnabled)
    }

    function test_choosing_a_custom_image_leaves_the_repository_alone() {
        choose(imageFile, "customImage")

        compare(String(repoDialog.selectedRepo), "",
                "picking an image is not picking a repository")
    }
}
