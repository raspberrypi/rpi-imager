/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * What choosing a different OS throws away.
 *
 * Customisation settings are not all universal. Pi Connect, secure boot,
 * passwordless sudo and the interface toggles each depend on the chosen image
 * advertising support for them, and several of those settings persist between
 * sessions. So choosing an OS re-derives the capability picture and scrubs
 * everything the new image cannot honour -- from the map handed to the
 * generator and, for the interface settings, from disk as well.
 *
 * The source says why: "so they cannot leak into an image that doesn't
 * advertise the corresponding capability". An I2C setting surviving into an
 * image without it means the generator writes a dtparam the image will not
 * act on, and the summary tells the user an interface was enabled that was
 * not.
 *
 * The scrubbing was being executed by the existing cases -- choosing an OS is
 * what they do -- but none of them asserted it, so every one of those deletes
 * could have been removed without a test noticing. That is the gap this
 * closes: the lines were covered and the behaviour was not.
 *
 * Capability flags are written by selectOSitem from the OS entry's own
 * metadata, so each case here sets up an entry that does or does not advertise
 * the thing under test and lets the step decide.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "OsChangeScrub"
    when: windowShown
    width: 900
    height: 700
    visible: true

    // Every property selectOSitem() writes when a concrete OS is chosen. A
    // stub missing any of them throws part-way through and leaves the step
    // half-configured, with the assertions still passing.
    QtObject {
        id: fakeContainer
        property var customizationSettings: ({})
        property string selectedOsName: ""
        property string networkInfoText: ""
        property bool customizationSupported: true
        property bool ccRpiAvailable: false
        property bool ifAndFeaturesAvailable: false
        property bool piConnectAvailable: false
        property bool piConnectEnabled: false
        property bool secureBootAvailable: false
        property bool secureBootEnabled: false
        property bool passwordlessSudoAvailable: false
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property bool ifI2cEnabled: false
        property bool ifSpiEnabled: false
        property bool if1WireEnabled: false
        property bool featUsbGadgetEnabled: false
        property string ifSerial: ""
        property Item overlayRootRef: testCase
        property int jumpedTo: -1
        function jumpToStep(n) { jumpedTo = n }
    }

    Component {
        id: stepComponent
        OSSelectionStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    function init() {
        fakeContainer.customizationSettings = ({})
        fakeContainer.selectedOsName = ""
        fakeContainer.piConnectAvailable = false
        fakeContainer.piConnectEnabled = false
        fakeContainer.secureBootAvailable = false
        fakeContainer.secureBootEnabled = false
        fakeContainer.passwordlessSudoAvailable = false
        fakeContainer.ifI2cEnabled = false
        fakeContainer.ifSpiEnabled = false
        fakeContainer.if1WireEnabled = false
        fakeContainer.featUsbGadgetEnabled = false
        fakeContainer.ifSerial = ""
        ImageWriterSingleton.clearSavedCustomisationSettings()
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
    }

    function cleanup() {
        if (step) {
            step.destroy()
            step = null
        }
        ImageWriterSingleton.clearSavedCustomisationSettings()
    }

    // A concrete OS entry, as selectOSitem expects to be handed one.
    //
    // The keys matter: several are passed straight to C++, and `capabilities`
    // in particular matches neither overload of setSWCapabilitiesList when it
    // is undefined, so an incomplete row is refused at the boundary rather
    // than quietly selected. What the image advertises there is what decides
    // which settings survive being chosen.
    function osEntry(overrides) {
        const entry = {
            name: "Test OS",
            subitems_json: "",
            subitems_url: "",
            url: "https://example.invalid/test.img.xz",
            image_download_size: 500,
            extract_size: 1000,
            extract_sha256: "abc123",
            contains_multiple_files: false,
            release_date: "2025-01-01",
            init_format: "",
            capabilities: []
        }
        for (const k in overrides)
            entry[k] = overrides[k]
        return entry
    }

    function chooseOs(overrides) {
        step.selectOSitem(osEntry(overrides), false, false)
    }

    // The interface settings, as the generator's keys and the wizard's flags.
    readonly property var interfaceKeys: [
        "enableI2C", "enableSPI", "enable1Wire", "enableSerial",
        "enableUsbGadget"
    ]
    readonly property var interfaceFlags: [
        "ifI2cEnabled", "ifSpiEnabled", "if1WireEnabled",
        "featUsbGadgetEnabled"
    ]

    function stageInterfaceSettings() {
        for (let i = 0; i < interfaceKeys.length; i++) {
            fakeContainer.customizationSettings[interfaceKeys[i]] = true
            // On disk as well, which is where a value from an older version
            // would be sitting.
            ImageWriterSingleton.setPersistedCustomisationSetting(
                interfaceKeys[i], true)
        }
        for (let j = 0; j < interfaceFlags.length; j++)
            fakeContainer[interfaceFlags[j]] = true
        fakeContainer.ifSerial = "Console"
    }

    // -- The interface settings, which are never meant to persist ----------

    function test_choosing_an_os_clears_interface_settings_from_the_session() {
        // These are capability-dependent, so whatever the previous image
        // supported says nothing about this one.
        stageInterfaceSettings()

        chooseOs({})

        const left = []
        for (let i = 0; i < interfaceKeys.length; i++)
            if (fakeContainer.customizationSettings[interfaceKeys[i]] !== undefined)
                left.push(interfaceKeys[i])
        compare(left.length, 0,
                "choosing an OS left these in the settings handed to the "
                + "generator: " + left.join(", ")
                + " -- they would be written into an image that may not "
                + "advertise them")
    }

    function test_choosing_an_os_clears_interface_settings_from_disk() {
        // The comment in the source says older versions saved these, so a
        // stale value can be waiting on disk even for a user who never set
        // one this session.
        stageInterfaceSettings()
        const before = ImageWriterSingleton.getSavedCustomisationSettings()
        verify(before["enableI2C"] !== undefined,
               "there is a persisted setting to scrub")

        chooseOs({})

        const after = ImageWriterSingleton.getSavedCustomisationSettings()
        const left = []
        for (let i = 0; i < interfaceKeys.length; i++)
            if (after[interfaceKeys[i]] !== undefined)
                left.push(interfaceKeys[i])
        compare(left.length, 0,
                "choosing an OS left these on disk: " + left.join(", "))
    }

    function test_choosing_an_os_clears_the_interface_flags_the_summary_reads() {
        // The flags are what the writing and completion screens list. Left
        // set, they would tell the user an interface was enabled on a card
        // that has no such setting written to it.
        stageInterfaceSettings()

        chooseOs({})

        const left = []
        for (let i = 0; i < interfaceFlags.length; i++)
            if (fakeContainer[interfaceFlags[i]])
                left.push(interfaceFlags[i])
        if (fakeContainer.ifSerial !== "" && fakeContainer.ifSerial !== "Disabled")
            left.push("ifSerial=" + fakeContainer.ifSerial)
        compare(left.length, 0,
                "choosing an OS left these claimed: " + left.join(", "))
    }

    // -- Pi Connect ---------------------------------------------------------

    function test_an_os_without_pi_connect_drops_the_enrolment() {
        // Leaving it set would tell the generator to enrol a board with an
        // image that has no Connect agent to do it.
        fakeContainer.piConnectEnabled = true
        fakeContainer.customizationSettings.piConnectEnabled = true

        chooseOs({})

        verify(!fakeContainer.piConnectAvailable,
               "this OS advertises no Connect support")
        verify(!fakeContainer.piConnectEnabled,
               "so the enrolment is dropped")
        verify(fakeContainer.customizationSettings.piConnectEnabled === undefined,
               "and removed from what the generator is given, rather than "
               + "left set to false")
    }

    // -- Secure boot and passwordless sudo ---------------------------------

    function test_an_os_without_secure_boot_drops_it() {
        // Secure boot programmes one-way fuses. A setting surviving onto an
        // image that cannot use it is the worst kind of stale.
        fakeContainer.secureBootEnabled = true
        fakeContainer.customizationSettings.secureBootEnabled = true

        chooseOs({})

        verify(!fakeContainer.secureBootAvailable,
               "this OS advertises no secure boot support")
        verify(!fakeContainer.secureBootEnabled, "so it is dropped")
        verify(fakeContainer.customizationSettings.secureBootEnabled === undefined,
               "and removed from the generator's settings")
    }

    function test_an_os_without_passwordless_sudo_drops_it() {
        // Passwordless sudo lets any process running as that user become
        // root. It is not something to carry between images by accident.
        fakeContainer.customizationSettings.passwordlessSudo = true

        chooseOs({})

        verify(!fakeContainer.passwordlessSudoAvailable,
               "this OS advertises no support for it")
        verify(fakeContainer.customizationSettings.passwordlessSudo === undefined,
               "so it is removed rather than carried over")
    }

    // -- And the other direction: an image that does advertise it ----------
    //
    // Without these the scrubs could clear unconditionally and every case
    // above would still pass, which would mean nobody could configure Connect
    // or secure boot at all.

    function test_an_os_with_pi_connect_keeps_the_enrolment() {
        fakeContainer.piConnectEnabled = true
        fakeContainer.customizationSettings.piConnectEnabled = true

        chooseOs({ capabilities: ["rpi_connect"] })

        verify(fakeContainer.piConnectAvailable,
               "this OS advertises Connect support")
        verify(fakeContainer.piConnectEnabled,
               "so the enrolment the user set up stands")
        compare(fakeContainer.customizationSettings.piConnectEnabled, true,
                "and is still in what the generator is given")
    }

    function test_an_os_with_secure_boot_keeps_it() {
        fakeContainer.secureBootEnabled = true
        fakeContainer.customizationSettings.secureBootEnabled = true

        chooseOs({ capabilities: ["secure_boot"] })

        verify(fakeContainer.secureBootAvailable,
               "this OS advertises secure boot support")
        verify(fakeContainer.secureBootEnabled,
               "so the user's choice stands -- scrubbing unconditionally "
               + "would make secure boot impossible to configure at all")
        compare(fakeContainer.customizationSettings.secureBootEnabled, true,
                "and is still in what the generator is given")
    }

    function test_an_os_with_passwordless_sudo_keeps_it() {
        fakeContainer.customizationSettings.passwordlessSudo = true

        chooseOs({ capabilities: ["passwordless_sudo"] })

        verify(fakeContainer.passwordlessSudoAvailable,
               "this OS advertises support for it")
        compare(fakeContainer.customizationSettings.passwordlessSudo, true,
                "so the user's choice stands")
    }

    // -- What it must not throw away ---------------------------------------

    function test_choosing_an_os_keeps_the_settings_that_apply_to_any_image() {
        // Hostname, locale, the user account, Wi-Fi and SSH do not depend on
        // the image advertising anything, so scrubbing them would silently
        // discard work the user had done on earlier screens.
        fakeContainer.customizationSettings.hostname = "raspberrypi"
        fakeContainer.customizationSettings.sshEnabled = true
        fakeContainer.customizationSettings.wifiSSID = "Pi Towers"
        fakeContainer.hostnameConfigured = true
        fakeContainer.userConfigured = true
        fakeContainer.wifiConfigured = true
        fakeContainer.sshEnabled = true

        chooseOs({})

        compare(fakeContainer.customizationSettings.hostname, "raspberrypi",
                "the hostname is not capability-dependent")
        compare(fakeContainer.customizationSettings.sshEnabled, true)
        compare(fakeContainer.customizationSettings.wifiSSID, "Pi Towers")
        verify(fakeContainer.hostnameConfigured, "and the flags stand")
        verify(fakeContainer.userConfigured)
        verify(fakeContainer.wifiConfigured)
        verify(fakeContainer.sshEnabled)
    }

    function test_choosing_an_os_records_which_one_was_chosen() {
        // The scrubbing is a side effect; this is the point of the call, and
        // asserting it here means a stub that throws part-way through the
        // scrub is not mistaken for a clean run.
        chooseOs({ name: "Raspberry Pi OS Lite (64-bit)" })

        compare(fakeContainer.selectedOsName, "Raspberry Pi OS Lite (64-bit)",
                "the chosen image is recorded")
    }


    // ── Choosing an OS by name ────────────────────────────────────────
    //
    // selectNamedOS() is how a deployment pre-selects an image: the
    // os_list.json an organisation serves carries "default_os", and this
    // walks the list looking for it. It had never run.
    //
    // What it does wrong is quiet. Match too loosely and a fleet gets
    // "Raspberry Pi OS Lite" where the manifest asked for "Raspberry Pi
    // OS"; fall back to the first entry when the name is absent and a typo
    // in the manifest silently images every board with whatever happens to
    // be at the top of the list. Neither shows up as an error anywhere.
    //
    // It takes the model as an argument, so the cases hand it one.

    function fakeModel(names) {
        var rows = []
        for (var i = 0; i < names.length; i++)
            rows.push(osEntry({ name: names[i] }))
        return {
            rowCount: function () { return rows.length },
            get: function (i) { return rows[i] }
        }
    }

    function test_the_named_os_is_the_one_chosen() {
        step.selectNamedOS("Raspberry Pi OS Lite (64-bit)",
                           fakeModel(["Raspberry Pi OS (64-bit)",
                                      "Raspberry Pi OS Lite (64-bit)",
                                      "Raspberry Pi OS (Legacy)"]))

        compare(fakeContainer.selectedOsName, "Raspberry Pi OS Lite (64-bit)")
    }

    function test_a_name_that_is_not_there_chooses_nothing() {
        // Rather than the first entry, which is what a loop without the
        // name check would leave selected.
        step.selectNamedOS("Some OS that is not on the list",
                           fakeModel(["Raspberry Pi OS (64-bit)",
                                      "Raspberry Pi OS Lite (64-bit)"]))

        compare(fakeContainer.selectedOsName, "",
                "nothing was chosen for a name the list does not have")
    }

    function test_the_name_has_to_match_in_full_data() {
        return [
            { tag: "a prefix of it",   asked: "Raspberry Pi OS" },
            { tag: "a superset of it", asked: "Raspberry Pi OS (64-bit) v2" },
            { tag: "the wrong case",   asked: "raspberry pi os (64-bit)" },
            { tag: "with a space",     asked: "Raspberry Pi OS (64-bit) " }
        ]
    }

    function test_the_name_has_to_match_in_full(data) {
        step.selectNamedOS(data.asked,
                           fakeModel(["Raspberry Pi OS (64-bit)",
                                      "Raspberry Pi OS Lite (64-bit)"]))

        compare(fakeContainer.selectedOsName, "", data.tag)
    }

    function test_an_empty_list_chooses_nothing() {
        // The manifest naming a default while the list has not arrived.
        step.selectNamedOS("Raspberry Pi OS (64-bit)", fakeModel([]))

        compare(fakeContainer.selectedOsName, "")
    }

    function test_the_first_entry_with_the_name_wins() {
        // Two entries can share a name across categories. Taking the first
        // and stopping is what the loop does; carrying on would leave the
        // last one selected instead, so which image gets written would
        // depend on the order the server happened to send.
        var rows = [osEntry({ name: "Duplicate", url: "https://example.invalid/first.img.xz" }),
                    osEntry({ name: "Duplicate", url: "https://example.invalid/second.img.xz" })]
        var model = {
            rowCount: function () { return rows.length },
            get: function (i) { return rows[i] }
        }

        step.selectNamedOS("Duplicate", model)

        compare(fakeContainer.selectedOsName, "Duplicate")
        compare(String(ImageWriterSingleton.srcFileName()), "first.img.xz",
                "the first of the two is what will be written")
    }

    // ── The custom image picker used where there is no native one ─────
    //
    // On a machine with no native file dialog -- the embedded build, and
    // any desktop whose portal is not reachable -- this styled picker is
    // how "Use custom" gets an image. Its accepted handler was uncovered,
    // so the whole fallback route was untested: the dialog would open, the
    // user would choose a file, and nothing would happen.
    //
    // Its rejected handler is deliberately empty and is not asserted here;
    // there is nothing it could do wrong that a test could see.
    //
    // The file named has to exist: the C++ side refuses a selection that is
    // not a regular file, which is its own guard and has its own test. Any
    // real file will do, so the cases use one out of the copied module
    // rather than needing a fixture.

    function test_a_file_chosen_from_the_fallback_picker_becomes_the_os() {
        fakeContainer.selectedOsName = ""

        step.customImageFileDialog.selectedFile = __qmlModuleRoot + "Style.qml"
        step.customImageFileDialog.accepted()

        tryVerify(function () {
            return fakeContainer.selectedOsName === "Style.qml"
        }, 3000, "the chosen file is what will be written; got "
           + fakeContainer.selectedOsName)
    }

    function test_a_custom_image_clears_the_customisation_it_cannot_carry() {
        // Same rule as the native path, and worth pinning on this one too:
        // the two routes into the custom-image handler are separate pieces
        // of wiring and only one of them was covered.
        fakeContainer.wifiConfigured = true
        fakeContainer.userConfigured = true
        fakeContainer.sshEnabled = true

        step.customImageFileDialog.selectedFile = __qmlModuleRoot + "Style.qml"
        step.customImageFileDialog.accepted()

        tryVerify(function () { return !fakeContainer.wifiConfigured }, 3000,
                  "the wireless setting went with the image change")
        verify(!fakeContainer.userConfigured)
        verify(!fakeContainer.sshEnabled)
    }
}
