/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef CUSTOMIZATION_GENERATOR_H
#define CUSTOMIZATION_GENERATOR_H

#include <QString>
#include <QByteArray>
#include <QVariantMap>

namespace rpi_imager {

// Raspberry Pi Connect configuration paths
constexpr auto PI_CONNECT_CONFIG_PATH = ".config/com.raspberrypi.connect";
constexpr auto PI_CONNECT_DEPLOY_KEY_FILENAME = "auth.key";

/**
 * @brief Generates firstrun.sh and cloud-init customisation scripts for Raspberry Pi images
 * 
 * This class handles the generation of systemd-based firstrun.sh scripts and
 * cloud-init YAML files that customise Raspberry Pi OS images on first boot. 
 * It supports hostname, SSH keys, user management, WiFi, keyboard layout, 
 * timezone, and more.
 */
class CustomisationGenerator {
public:
    /**
     * @brief Generate a firstrun.sh script from settings
     * 
     * @param settings Map containing customisation settings
     * @param piConnectToken Optional Raspberry Pi Connect token
     * @return QByteArray containing the generated script
     */
    static QByteArray generateSystemdScript(const QVariantMap& settings, 
                                           const QString& piConnectToken = QString());
    
    /**
     * @brief Generate cloud-init user-data YAML from settings
     * 
     * @param settings Map containing customisation settings
     * @param piConnectToken Optional Raspberry Pi Connect token
     * @param hasCcRpi Whether the OS supports the cc_raspberry_pi cloud-init module
     * @param sshEnabled Whether SSH is enabled
     * @param getCurrentUser Function to get current user name
     * @return QByteArray containing the generated user-data YAML
     */
    static QByteArray generateCloudInitUserData(const QVariantMap& settings,
                                               const QString& piConnectToken = QString(),
                                               bool hasCcRpi = false,
                                               bool sshEnabled = false,
                                               const QString& currentUser = QString());
    
    /**
     * @brief Generate cloud-init network-config YAML from settings
     * 
     * @param settings Map containing customisation settings
     * @param hasCcRpi Whether the OS supports the cc_raspberry_pi cloud-init module
     * @return QByteArray containing the generated network-config YAML
     */
    static QByteArray generateCloudInitNetworkConfig(const QVariantMap& settings,
                                                    bool hasCcRpi = false);

private:
    /**
     * @brief Shell-quote a string for safe use in bash scripts
     * 
     * @param value String to quote
     * @return Quoted string safe for shell use
     */
    static QString shellQuote(const QString& value);
    
    /**
     * @brief Generate password hash using pbkdf2
     * 
     * @param password Plain text password
     * @param ssid SSID for WiFi password hashing
     * @return Hashed password
     */
    static QString pbkdf2(const QByteArray& password, const QByteArray& ssid);
    
    /**
     * @brief Escape a string for use in YAML double-quoted strings
     * 
     * Per IEEE 802.11, SSIDs can be 0-32 octets containing ANY byte value,
     * including null, control characters, and non-printable bytes.
     * This function escapes all special characters for safe YAML inclusion.
     * 
     * @param value String to escape
     * @return Escaped string safe for YAML double-quoted context
     */
    static QString yamlEscapeString(const QString& value);
};

} // namespace rpi_imager

#endif // CUSTOMIZATION_GENERATOR_H

