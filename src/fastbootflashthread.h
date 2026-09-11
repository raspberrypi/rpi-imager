/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * QThread that flashes an OS image to a device via the fastboot protocol.
 *
 * Uses a three-thread pipeline:
 *   Thread 1 (download):   curl → _compressedRing
 *   Thread 2 (decompress): _compressedRing → libarchive → _decompressedRing
 *   Main thread (flash):   _decompressedRing → fastboot download+flash → USB
 */

#ifndef FASTBOOTFLASHTHREAD_H
#define FASTBOOTFLASHTHREAD_H

#include <QThread>
#include <QString>
#include <QUrl>

#include <string>

#include <memory>

class RingBuffer;
class AcceleratedCryptographicHash;
namespace fastboot { class FastbootProtocol; }
namespace rpiboot { class IUsbTransport; }

namespace rpiboot { class IUsbTransport; class LibusbContext; struct UsbDeviceInfo; }

class FastbootFlashThread : public QThread
{
    Q_OBJECT
public:
    explicit FastbootFlashThread(const QString& fastbootId,
                                  const QString& blockDevice,
                                  const QUrl& imageUrl,
                                  quint64 downloadLen,
                                  quint64 extractLen,
                                  const QByteArray& expectedHash,
                                  QObject* parent = nullptr);
    ~FastbootFlashThread() override;

    void cancel();

    // Set OS customization data to apply after flashing.
    // Mirrors the DownloadThread::setImageCustomisation() interface.
    void setImageCustomisation(const QByteArray &config,
                                const QByteArray &cmdline,
                                const QByteArray &firstrun,
                                const QByteArray &cloudinit,
                                const QByteArray &cloudinitNetwork,
                                const QByteArray &initFormat);

    // Set optional bmap URL for DONT_CARE block optimisation.
    // When set, unmapped blocks are skipped during fastboot flash.
    void setBmapUrl(const QUrl &url) { _bmapUrl = url; }

    // Configure Raspberry Pi Connect Device Identity registration.
    // When apiKey is non-empty, the device will be registered with
    // the Connect management API after flashing and before reboot.
    // Failures are non-fatal and will not block successful flash.
    // baseUrl overrides the Connect API address, and defaults to the
    // production one. Without it this path can only be exercised against the
    // live API: ConnectDeviceRegistrar takes an injectable base URL, but the
    // caller below did not pass one through.
    void setConnectRegistration(const QString &apiKey,
                                 const QString &descriptionPrefix,
                                 const QString &baseUrl = QString());

    // Turn the device's reported max-download-size into a segment size we are
    // willing to allocate against.
    //
    // The value arrives as a decimal or 0x-prefixed string from the device, so
    // it is attacker-shaped in the same sense any USB descriptor is: whatever
    // happens to be plugged in chooses it. Two buffers of this size are
    // reserved up front by SparseEncoder, so an absurd value is an absurd
    // allocation.
    //
    // `reported` is null when the device answered nothing, in which case the
    // default is used. `availableBytes` is the memory budget to size against;
    // pass 0 to skip the memory-derived ceiling.
    static uint32_t resolveMaxDownloadSize(const std::string *reported,
                                           quint64 availableBytes);

signals:
    void writing();   // Emitted when download+flash pipeline starts
    void success();
    void error(QString msg);
    void finalizing();
    void preparationStatusUpdate(QString msg);
    void downloadProgress(quint64 dlnow, quint64 dltotal);
    void writeProgress(quint64 now, quint64 total);
    void eventFastbootDeviceOpen(quint32 durationMs, bool success, QString metadata);

protected:
    void run() override;

// Protected rather than private so a test can subclass and drive the parts
// that already take their transport as a parameter.
protected:
    void runImpl();
    void downloadProducer();
    void decompressConsumerProducer();
    bool applyCustomisation(class fastboot::FastbootProtocol& fb,
                             class rpiboot::IUsbTransport& transport);

    // True when this is the special "Erase" OS-list entry
    // (_imageUrl == "internal://format") rather than a real image flash.
    bool isEraseOperation() const;

    // Handle the "Erase" case for a fastboot device: bare-wipe the whole
    // block device, then lay down a fresh MBR with a single FAT32-typed
    // partition spanning the device, and reboot.  NB: rpi-fastbootd has no
    // mkfs command, so the partition is *typed* FAT32 but left without a
    // filesystem --- unlike the SD-card Erase path (DriveFormatThread),
    // which writes a mountable FAT32.  Emits success()/error() itself.
    bool performErase(class fastboot::FastbootProtocol& fb,
                      class rpiboot::IUsbTransport& transport);

    // How the fastboot device is opened.
    //
    // Everything past this point drives the device through IUsbTransport,
    // which is why the individual steps -- erase, customisation, boot order
    // -- were already testable against the mock. Constructing a concrete
    // LibusbTransport was the single thing that kept runImpl(), the whole
    // flash sequence, reachable only with hardware attached. Behind a
    // virtual it is reachable without.
    //
    // The context is passed in rather than created here so its lifetime
    // stays exactly where it was: it has to outlive the transport.
    virtual std::unique_ptr<class rpiboot::IUsbTransport> openFastbootTransport(
        class rpiboot::LibusbContext& ctx, const struct rpiboot::UsbDeviceInfo& target);

    // Best-effort: after the OS image has been flashed, set the
    // EEPROM's BOOT_ORDER so the chosen storage device boots first on
    // the next power cycle. Reads the device's existing EEPROM, edits
    // bootconf.txt in place, re-signs if the device requires it, and
    // writes back via `oem eeprom-update`. Failures are logged and
    // swallowed --- a successful image flash should not be reported as
    // a failure just because the boot-order tweak couldn't run (e.g.
    // older fastbootd without the eeprom commands, signed-eeprom board
    // with no key configured, etc.).
    void applyBootOrderUpdate(class fastboot::FastbootProtocol& fb,
                               class rpiboot::IUsbTransport& transport);

    // Best-effort: register the device's identity with Raspberry Pi Connect
    // while it is still in fastboot mode, which is the only time its public
    // key can be queried and signed by the firmware crypto engine. Failures
    // are logged and swallowed: the image is already written.
    //
    // Split out of runImpl() so it can be driven against the mock transport
    // and a stub API, in the same way as applyBootOrderUpdate() above.
    void registerWithConnect(class fastboot::FastbootProtocol& fb,
                              class rpiboot::IUsbTransport& transport);

    QString _fastbootId;
    QString _blockDevice;
    QUrl _imageUrl;
    QUrl _bmapUrl;
    quint64 _downloadLen;
    quint64 _extractLen;
    QByteArray _expectedHash;
    std::atomic<bool> _cancelled{false};

    // Pipeline ring buffers
    std::unique_ptr<RingBuffer> _compressedRing;
    std::unique_ptr<RingBuffer> _decompressedRing;

    // Incremental hash for verification
    std::unique_ptr<AcceleratedCryptographicHash> _imageHash;

    // Track download progress from curl callback
    std::atomic<quint64> _dlnow{0};
    std::atomic<quint64> _dltotal{0};

    // Error from pipeline threads
    QString _downloadError;
    QString _decompressError;

    // OS customization (applied post-flash via fastboot file transfer)
    QByteArray _config;
    QByteArray _cmdline;
    QByteArray _firstrun;
    QByteArray _cloudinit;
    QByteArray _cloudinitNetwork;
    QByteArray _initFormat;

    // Raspberry Pi Connect Device Identity registration (optional)
    QString _connectApiKey;
    QString _connectDescriptionPrefix;
    QString _connectBaseUrl;
};

#endif // FASTBOOTFLASHTHREAD_H
