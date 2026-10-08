/* SPDX-License-Identifier: Apache-2.0 */
import QtQuick
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "WifiHotspot"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: container
        property var customizationSettings: ({})
        property bool wifiConfigured: false
        property string networkInfoText: ""
    }
    Component {
        id: stepComponent
        WifiCustomizationStep {
            wizardContainer: container
            hotspotSupported: true
            width: testCase.width
            height: testCase.height
        }
    }
    property var step
    property var previouslySaved
    function init() {
        previouslySaved = ImageWriterSingleton.getSavedCustomisationSettings()
        container.customizationSettings = ({})
        container.wifiConfigured = false
        step = stepComponent.createObject(testCase)
        verify(step)
    }
    function cleanup() {
        step.destroy()
        ImageWriterSingleton.setSavedCustomisationSettings(previouslySaved)
        testCase.height = 700
    }
    function field(name) { return findChild(step, name) }
    function credentials(name, password) {
        field("wifiSsidField").text = name
        field("wifiPasswordField").text = password
        field("wifiPasswordConfirmField").text = password
    }
    function test_switch_does_not_share_home_credentials() {
        credentials("Home Wi-Fi", "home-password")
        field("wifiHiddenToggle").checked = true
        field("wifiHotspotTab").clicked()
        compare(step.wifiNetworkMode, "hotspot")
        compare(field("wifiSsidField").text, "raspberrypi")
        compare(field("wifiPasswordField").text, "")
        compare(field("wifiPasswordConfirmField").text, "")
        verify(!field("wifiHiddenToggle").visible)
        verify(!step.nextButtonEnabled)
    }
    function test_secure_hotspot_commits_only_derived_key() {
        step.selectNetworkMode("hotspot")
        credentials("My Pi", "hotspot-password")
        verify(step.nextButtonEnabled)
        step.nextClicked()
        compare(container.customizationSettings.wifiNetworkMode, "hotspot")
        compare(container.customizationSettings.wifiSSID, "My Pi")
        compare(container.customizationSettings.wifiPasswordCrypt.length, 64)
        verify(container.customizationSettings.wifiPassword === undefined)
        compare(container.customizationSettings.wifiHidden, false)
        verify(container.wifiConfigured)
        var saved = ImageWriterSingleton.getSavedCustomisationSettings()
        compare(saved.wifiNetworkMode, "hotspot")
        compare(saved.wifiPasswordCrypt, container.customizationSettings.wifiPasswordCrypt)
        verify(saved.wifiPassword === undefined)
    }
    function test_saved_hotspot_reopens_without_password_or_keychain() {
        step.destroy()
        container.customizationSettings = ({ wifiNetworkMode: "hotspot", wifiSSID: "My Pi",
            wifiPasswordCrypt: "a".repeat(64), wifiMode: "secure" })
        step = stepComponent.createObject(testCase)
        compare(step.wifiNetworkMode, "hotspot")
        compare(field("wifiPasswordField").text, "")
        verify(step.nextButtonEnabled)
        step.nextClicked()
        compare(container.customizationSettings.wifiPasswordCrypt, "a".repeat(64))
        step.selectNetworkMode("client")
        credentials("My Pi", "")
        verify(!step.nextButtonEnabled, "a saved hotspot key must not be reused for a client network")
    }
    function test_open_hotspot_and_name_byte_limit() {
        step.selectNetworkMode("hotspot")
        field("wifiOpenTab").clicked()
        credentials("", "")
        verify(!step.nextButtonEnabled)
        credentials("é".repeat(16), "")
        verify(step.nextButtonEnabled)
        credentials("é".repeat(17), "")
        verify(!step.nextButtonEnabled)
        credentials("A".repeat(33), "")
        verify(!step.nextButtonEnabled)
        credentials("Open Pi", "")
        step.nextClicked()
        compare(container.customizationSettings.wifiMode, "open")
        verify(container.customizationSettings.wifiPasswordCrypt === undefined)
    }
    function test_unsupported_os_does_not_reinterpret_saved_hotspot() {
        step.destroy()
        container.customizationSettings = ({ wifiNetworkMode: "hotspot", wifiSSID: "My Pi",
            wifiPasswordCrypt: "a".repeat(64), wifiMode: "secure" })
        step = stepComponent.createObject(testCase, { hotspotSupported: false })
        compare(step.wifiNetworkMode, "client")
        verify(field("wifiSsidField").text !== "My Pi")
        verify(!field("wifiHotspotTab").enabled)
        credentials("My Pi", "")
        verify(!step.nextButtonEnabled)
    }
    function test_controls_fit_small_window_and_scroll_into_view() {
        testCase.height = 420
        step.height = 420
        step.selectNetworkMode("hotspot")
        credentials("My Pi", "hotspot-password")
        wait(50)
        var password = field("wifiPasswordConfirmField")
        password.textField.forceActiveFocus()
        wait(50)
        var position = password.mapToItem(step, 0, 0)
        verify(position.y >= 0)
        verify(position.y + password.height < step.height)
        testCase.height = 700
    }
}
