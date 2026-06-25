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

    /**
     * @brief Escape IEEE 802.11 SSID octets for YAML double-quoted strings
     */
    static QByteArray yamlEscapeSsidOctets(const QByteArray& ssidOctets);

    /**
     * @brief Extract IEEE 802.11 SSID octets from customization settings
     */
    static QByteArray ssidOctetsFromSettings(const QVariantMap& settings, bool wifiConfigured);

    // -------------------------------------------------------------------
    // Credential derivation
    //
    // This class is the single home for turning a plaintext secret into the
    // form that ends up in the generated artifacts. Keep all password/PSK
    // hashing here so the UI layer never touches crypto and there is exactly
    // one implementation of each algorithm.
    // -------------------------------------------------------------------

    /**
     * @brief Hash a plaintext account password for /etc/shadow-style consumers.
     *
     * Picks yescrypt for OS images released on/after 2023-01-01 and sha256crypt
     * otherwise. CR/LF are stripped first: pasted secrets can carry a trailing
     * newline that PAM discards at login, which would otherwise yield a hash
     * that never matches (issue #1627).
     *
     * @param password Plaintext password (UTF-8 bytes)
     * @param osReleaseDate Target OS release date in "yyyy-MM-dd" form (may be empty)
     * @return crypt(3)-style hash string, or empty on failure
     */
    static QString cryptPassword(const QByteArray& password, const QString& osReleaseDate);

    /**
     * @brief Derive a WPA PSK from a passphrase (PBKDF2-HMAC-SHA1, 4096 iters).
     *
     * @param password Plaintext Wi-Fi passphrase (UTF-8 bytes)
     * @param ssid SSID octets used as the PBKDF2 salt
     * @return Hex-encoded 32-byte PSK
     */
    static QString pbkdf2(const QByteArray& password, const QByteArray& ssid);

    /**
     * @brief Whether the OS image's release date selects yescrypt over sha256crypt.
     *
     * yescrypt is used for images released on/after 2023-01-01; older (or
     * undated) images get sha256crypt, which every target understands. This is
     * the single source of truth for the algorithm decision, shared by
     * cryptPassword() and the hash-compatibility check.
     */
    static bool osUsesYescrypt(const QString& osReleaseDate);

    /**
     * @brief Whether a crypt() hash string is a yescrypt hash.
     *
     * crypt strings are self-describing via their "$id$" prefix, so the
     * algorithm never needs to be stored separately: yescrypt hashes begin with
     * "$y$" (we only ever emit "$y$" or sha256crypt's "$5$"). A yescrypt hash is
     * unusable on images that predate yescrypt support, whereas sha256crypt is
     * accepted everywhere - so only yescrypt hashes need a compatibility check.
     */
    static bool isYescryptHash(const QString& cryptHash);

private:
    /**
     * @brief Shell-quote a string for safe use in bash scripts
     * 
     * @param value String to quote
     * @return Quoted string safe for shell use
     */
    static QString shellQuote(const QString& value);

    /**
     * @brief Resolve the crypted account password from settings.
     *
     * Prefers a freshly entered plaintext password ("sshUserPasswordPlain"),
     * hashing it with cryptPassword() using "osReleaseDate"; otherwise falls
     * back to an already-crypted value carried in "sshUserPassword" (e.g. a
     * password reused from saved settings).
     */
    static QString resolveUserPasswordCrypt(const QVariantMap& settings);

    /**
     * @brief Resolve the crypted Wi-Fi PSK from settings.
     *
     * Prefers an already-derived PSK ("wifiPasswordCrypt"); otherwise derives
     * one from the plaintext "wifiPassword" using the SSID octets as salt. A
     * value that is not a passphrase length (8..63) is treated as a raw PSK and
     * passed through unchanged.
     */
    static QString resolveWifiPskCrypt(const QVariantMap& settings, const QByteArray& ssidOctets, bool wifiConfigured);

    static QString yamlEscapeString(const QString& value);
};

} // namespace rpi_imager

#endif // CUSTOMIZATION_GENERATOR_H

