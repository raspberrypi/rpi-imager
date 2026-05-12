// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// NSXPC interface between the unprivileged client (rpi-imager) and the
// privileged helper (rpi-imager-writer).
//
// This protocol is the wire-level contract for macOS. The IPrivilegedWriter
// C++ interface is the *internal* abstraction; this protocol is what flows
// between processes. Adding methods here is a versioned change - see §8a /
// §9 of the design doc for evolution rules.
//
// Phase 1b proof-of-architecture: only `pingWithReply:` (handshake) and
// `unmountDevice:reply:` (one real method) are implemented. The protocol
// is deliberately small while we validate the boundary; remaining methods
// are added as the bulk-write/drive-subscription/etc. paths are migrated.

#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// The Mach service name registered by the helper. Production code will
// install this via SMAppService; phase 1b development uses a launchctl
// bootstrap of a hand-written plist (see writer/dev/<name>.plist).
extern NSString* const RpiImagerWriterServiceName;

// Helper -> client one-way notification surface. The helper invokes
// this on the connection's exportedObject when DiskArbitration sees
// a drive appear or disappear. Client responds with a fresh
// listDrivesNow call - pull-on-push, no differential payload.
@protocol RpiImagerWriterClientProtocol <NSObject>
- (void)driveListChanged;
@end

@protocol RpiImagerWriterProtocol <NSObject>

// Handshake. Returns the helper's version string. Used at connection
// setup to detect helper/client mismatches and surface "please reinstall"
// errors before any real operations are attempted.
- (void)pingWithReply:(void (^)(NSString* helperVersion))reply;

// Enumerate attached block devices. The helper uses the same
// DiskArbitration / IOKit code path as the client today; running it
// here unifies the privilege story (so that on platforms where
// drivelist actually requires elevation - Windows - the helper is
// the natural place for it) and gives us a single point that the
// audit log can record drive-enumeration access at.
//
// `devices` is an NSArray of NSDictionary, one per device, with keys
// matching the fields of Drivelist::DeviceDescriptor used today by
// the imager UI: see service_impl.mm's listDrivesNow implementation
// for the exact key/value layout. Empty array on enumeration success
// with no devices attached.
- (void)listDrivesNow:(void (^)(NSArray<NSDictionary*>* _Nullable devices,
                                NSString* _Nullable errorDetail))reply;

// Subscribe the calling connection to drive-change push notifications.
// The connection's `exportedObject` must implement
// RpiImagerWriterClientProtocol; the helper invokes
// `[remoteObjectProxy driveListChanged]` on it whenever DiskArbitration
// reports an appearance or disappearance.
//
// Idempotent: subscribing twice on the same connection is a no-op.
// Connection invalidation auto-unsubscribes; no leak window.
- (void)subscribeDriveChangesWithReply:
    (void (^)(BOOL ok, NSString* _Nullable errorDetail))reply;

// Unsubscribe (idempotent). Returns YES if the subscription was active
// and is now torn down, NO if it wasn't subscribed to begin with.
- (void)unsubscribeDriveChangesWithReply:(void (^)(BOOL wasSubscribed))reply;

// Unmount all volumes belonging to the given block device.
//
// `devicePath` is the BSD name with the /dev/ prefix (e.g. "/dev/disk7").
// `success` is YES iff the unmount completed without dissent.
// `errorDetail` is nil on success; on failure it contains a short
// English description suitable for inclusion in a log line.
// `kernelStatus` is the underlying DAReturn / errno value when available;
// 0 if no underlying kernel error (e.g. "device not found").
- (void)unmountDevice:(NSString*)devicePath
                reply:(void (^)(BOOL success,
                                NSString* _Nullable errorDetail,
                                int32_t kernelStatus))reply;

// Open a raw block device for read+write.
//
// The returned `sessionToken` is opaque to the caller; pass it back to
// queryDeviceSize: / closeSession: to operate on the same FD. The FD
// itself never leaves the helper - that's the whole point of the
// privilege boundary.
//
// On failure, sessionToken is 0 and errorDetail describes why.
//
// On macOS Tahoe, this is the path that DOES NOT trigger the
// authopen-FD regression we set out to fix: the helper opens the
// device with its own ::open() call, holds the FD privately, and
// the kernel's per-write policy check does not see a borrowed FD.
- (void)openDevice:(NSString*)devicePath
              reply:(void (^)(uint64_t sessionToken,
                              NSString* _Nullable errorDetail,
                              int32_t kernelErrno))reply;

// Returns the device's size in bytes for the given session.
- (void)queryDeviceSize:(uint64_t)sessionToken
                  reply:(void (^)(uint64_t sizeBytes,
                                  NSString* _Nullable errorDetail))reply;

// Synchronous read of `length` bytes from `offset` on the FD held for
// the given session.
//
// Phase 1b: this is intentionally simple and slow - data crosses the
// XPC boundary as NSData. Suitable for verify-after-write and small
// metadata reads (a few MB at most). The bulk verify path will be
// re-routed through the shared-memory ring once that lands.
- (void)readAt:(uint64_t)sessionToken
         offset:(uint64_t)offset
         length:(uint64_t)length
          reply:(void (^)(NSData* _Nullable data,
                          NSString* _Nullable errorDetail,
                          int32_t kernelErrno))reply;

// Synchronous write of `data` at `offset` on the FD held for the given
// session.
//
// Same caveat as readAt: small chunks only in phase 1b. The full
// write pipeline routes through a shared-memory ring in subsequent
// phases.
- (void)writeAt:(uint64_t)sessionToken
          offset:(uint64_t)offset
            data:(NSData*)data
           reply:(void (^)(uint64_t bytesWritten,
                           NSString* _Nullable errorDetail,
                           int32_t kernelErrno))reply;

// Allocate and map a shared-memory region used for bulk write/read
// data transfer. The helper creates an anonymous, unnamed shm region
// of the requested size, returns a file-handle the client can mmap on
// its side, and remembers the mapping internally.
//
// Subsequent bulkWriteFromBuffer / bulkReadIntoBuffer calls reference
// the same region: the client populates bytes at `bufferOffset`, the
// helper pwrites them to the device. No data crosses the XPC boundary
// per write - just slot metadata.
//
// Pass 0 to release a previously-mapped buffer for the session.
- (void)mapBulkBuffer:(uint64_t)sessionToken
            sizeBytes:(uint64_t)sizeBytes
                reply:(void (^)(NSFileHandle* _Nullable fh,
                                NSString* _Nullable errorDetail))reply;

// Write `length` bytes from offset `bufferOffset` of the shared bulk
// buffer to device offset `deviceOffset`. Same per-session bytes-
// written cap applies.
- (void)bulkWriteFromBuffer:(uint64_t)sessionToken
               bufferOffset:(uint64_t)bufferOffset
               deviceOffset:(uint64_t)deviceOffset
                     length:(uint64_t)length
                      reply:(void (^)(uint64_t bytesWritten,
                                      NSString* _Nullable errorDetail,
                                      int32_t kernelErrno))reply;

// Compute a SHA-256 digest of `length` bytes from the device,
// starting at `deviceOffset`, optionally prefixed by `prefixBytes`
// (typically the imager's "first block" - the partition table -
// which is hashed during write but written to disk last and so isn't
// readable during verify).
//
// The helper does all the I/O and hashing internally; only the 32-
// byte digest crosses the XPC boundary. This is the fast path for
// verify-after-write: zero data transfer back to the client. For a
// 5 GB image at 100 MB/s flash read it saves ~5 GB of NSData
// marshalling and the corresponding allocator pressure.
//
// `digest` is a 32-byte SHA-256 hash on success; nil on failure with
// `errorDetail` populated. `bytesHashed` reports how many device
// bytes were actually read (may be < length at end-of-device).
- (void)hashDeviceSha256:(uint64_t)sessionToken
            prefixBytes:(NSData* _Nullable)prefixBytes
            deviceOffset:(uint64_t)deviceOffset
                 length:(uint64_t)length
                   reply:(void (^)(NSData* _Nullable digest,
                                   uint64_t bytesHashed,
                                   NSString* _Nullable errorDetail,
                                   int32_t kernelErrno))reply;

// Force all pending writes for the session's fd to physical storage
// via fsync(2). Returns NO with an error string if fsync failed.
- (void)syncSession:(uint64_t)sessionToken
              reply:(void (^)(BOOL ok,
                              NSString* _Nullable errorDetail,
                              int32_t kernelErrno))reply;

// Prepare a freshly-opened device for imaging by zeroing the first
// and/or last 1 MB and fsync'ing.
//
// This is the moral equivalent of the imager's prepareDevice MBR-zero
// step, hoisted into the privileged unit so that the partition-table
// scrub + any device unmount-fence + fsync happen behind the privilege
// boundary as a single transaction. On Tahoe this matters: doing it as
// four separate XPC round-trips (seek/write/flush/sync) lets other
// kernel actors (re-mount attempts, periodic sync, etc.) interleave
// with the imager's prepare phase and the first write can fail with
// EIO; doing it inside the helper keeps the FD exclusively in our
// hands for the whole prepare window.
- (void)prepareDevice:(uint64_t)sessionToken
          zeroFirstMb:(BOOL)zeroFirstMb
           zeroLastMb:(BOOL)zeroLastMb
                reply:(void (^)(BOOL ok,
                                NSString* _Nullable errorDetail,
                                int32_t kernelErrno))reply;

// Closes the FD held for the given session. Idempotent.
//
// `stats` is a per-session telemetry payload (§7a) - never nil on
// success, may be nil on failure. Keys are stable strings, values
// NSNumber boxes; see MacOSXpcBackend::closeSession for the
// dictionary -> proto::SessionStats translation.
- (void)closeSession:(uint64_t)sessionToken
                reply:(void (^)(BOOL ok, NSDictionary* _Nullable stats))reply;

@end

NS_ASSUME_NONNULL_END
