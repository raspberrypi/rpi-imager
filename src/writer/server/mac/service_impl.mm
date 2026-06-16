// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// NSXPC service implementation for rpi-imager-writer. Implements the
// RpiImagerWriterProtocol on the helper side; calls into the Qt-free
// DiskArbitration wrapper for actual work.

#import "RpiImagerWriterProtocol.h"
#import <Security/Security.h>
#import <DiskArbitration/DiskArbitration.h>
#import <IOKit/storage/IOMedia.h>
#import <IOKit/IOBSD.h>
#include <CommonCrypto/CommonDigest.h>

#include "drivelist/drivelist.h"
#include "disk_arbitration.h"
#include <os/log.h>
#include <bsm/libbsm.h>
#include <sys/mount.h>
#include <sys/param.h>

// NSXPCConnection.auditToken is exposed via a private accessor; the
// standard pattern is to declare it via a category. Apple's own
// documentation for the audit-token-based code-signing check uses this
// approach.
@interface NSXPCConnection (PrivateAuditToken)
@property (nonatomic, readonly) audit_token_t auditToken;
@end
#include <string>

#include <fcntl.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <random>
#include <array>
#include <atomic>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

// Per-session resource caps. These are applied at the helper to
// contain damage from a compromised but signed client. Tuned for
// realistic imaging workloads:
//   * Max bytes-written per session: 64 GB - larger than any sensible
//     OS image; stops a runaway client from draining disks.
//   * Max concurrent sessions per client PID: 3 - normal imager use is
//     1; we allow a small headroom for batch-imaging UIs.
constexpr std::uint64_t kMaxSessionBytesWritten = 64ull * 1024 * 1024 * 1024;
constexpr std::size_t   kMaxSessionsPerClient   = 3;

// Physical RAM, queried by the helper itself (never trusted from the
// client). Returns 0 if the query fails.
std::uint64_t physicalMemoryBytes() {
    std::uint64_t mem = 0;
    std::size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) != 0) {
        return 0;
    }
    return mem;
}

// Defense-in-depth ceiling for a single mapped bulk buffer. The client sizes
// its write ring as a fraction of RAM (a slot count bounded by 20% of
// available RAM, and hard-capped at 256 × 16 MB = 4 GB; see
// SystemMemoryManager::getCoordinatedRingBufferConfig). A ceiling of 1/3 of
// physical RAM comfortably covers any legitimate request while still bounding
// a buggy/compromised client's RSS demand, and scales with the machine
// instead of a fixed limit. Floored at 256 MB so low-RAM hosts still allow a
// usable ring even if the RAM query is unavailable.
std::uint64_t maxBulkBufferBytes() {
    constexpr std::uint64_t kFloor = 256ull * 1024 * 1024;
    const std::uint64_t phys = physicalMemoryBytes();
    if (phys == 0) return kFloor;
    const std::uint64_t third = phys / 3;
    return third > kFloor ? third : kFloor;
}

// Maximum number of in-flight bulk pwrite()s per session. Tuned to
// match typical SSD/SD card queue depths; larger values give more
// throughput on flash storage that handles parallel writes well, but
// burn more dispatch threads.
constexpr int kBulkInFlightCap = 8;

// Seconds without an active XPC connection (and no open sessions)
// before the helper voluntarily exits. The connection count only
// drops to zero when the client app actually quits (NSXPC holds
// the connection across method calls), so this is effectively just
// hysteresis against a quit-and-immediately-relaunch sequence.
// launchd's Mach-service-based activation re-spawns us lazily on
// the next client connection, so an exit here is transparent to
// callers - and ensures in-place upgrades take effect on the next
// app launch without a manual `killall`.
constexpr int kIdleExitSeconds = 2;

// All idle-exit timer manipulation happens on this serial queue so
// timer create/cancel races between accept and invalidation are
// impossible. The connection counter itself is atomic so we don't
// need the queue for the hot path.
std::atomic<int>& activeConnections() {
    static std::atomic<int> n{0};
    return n;
}

dispatch_queue_t idleQueue() {
    static dispatch_once_t once;
    static dispatch_queue_t q = nullptr;
    dispatch_once(&once, ^{
        q = dispatch_queue_create("com.raspberrypi.rpi-imager.writer.idle",
                                    DISPATCH_QUEUE_SERIAL);
    });
    return q;
}

// File-scope storage for the idle-exit timer. All access happens on
// idleQueue() so direct assignment is race-free; the variable is
// __strong by ARC default which keeps the dispatch source alive
// across resume/cancel.
static dispatch_source_t g_idle_timer = nullptr;

// Forward-declare; defined below sessionTable() so it can peek at
// the live session count to refuse exit during long writes.
void scheduleIdleExitOnIdleQueue();
void cancelIdleExitOnIdleQueue();

// Forward-declare auditLogf so the idle-exit handler can call it
// (auditLogf's full definition is below alongside the log rotation
// helpers).
void auditLogf(NSString* fmt, ...) __attribute__((format(__NSString__, 1, 2)));

// ---- §5 drive-change subscription -----------------------------------------
//
// A pull-on-push design: subscribers receive a single `driveListChanged`
// notification when DiskArbitration sees any disk appear or disappear,
// and they respond by calling `listDrivesNow` to get the new snapshot.
// No differential payload crosses the boundary, so the protocol stays
// trivial and races (notify + enumerate vs further changes) resolve
// naturally on the next round-trip.
//
// All access to the subscriber set and the DA session happens on the
// dedicated serial queue declared below so a disconnect-mid-callback
// can't race the dispatch.

dispatch_queue_t driveSubQueue() {
    static dispatch_once_t once;
    static dispatch_queue_t q = nullptr;
    dispatch_once(&once, ^{
        q = dispatch_queue_create("com.raspberrypi.rpi-imager.writer.drivesub",
                                    DISPATCH_QUEUE_SERIAL);
    });
    return q;
}

NSMutableSet<NSXPCConnection*>* driveSubscribers() {
    static NSMutableSet<NSXPCConnection*>* s = [NSMutableSet new];
    return s;
}

// DA session is shared across all subscribers (cheap; one CFRunLoop
// scheduling). Allocated lazily on first subscribe; torn down on last
// unsubscribe.
static DASessionRef g_drive_sub_da_session = nullptr;

void notifyDriveListChangedOnDriveSubQueue() {
    NSArray<NSXPCConnection*>* snap;
    snap = [driveSubscribers() allObjects];
    for (NSXPCConnection* conn in snap) {
        @try {
            id<RpiImagerWriterClientProtocol> proxy =
                [conn remoteObjectProxyWithErrorHandler:^(NSError* err) {
                    // Best-effort; if the client died the invalidation
                    // handler will clean its subscription entry up.
                    (void)err;
                }];
            [proxy driveListChanged];
        } @catch (NSException* exc) {
            (void)exc;
        }
    }
}

static void onDADiskAppeared(DADiskRef /*disk*/, void* /*ctx*/) {
    notifyDriveListChangedOnDriveSubQueue();
}
static void onDADiskDisappeared(DADiskRef /*disk*/, void* /*ctx*/) {
    notifyDriveListChangedOnDriveSubQueue();
}

void ensureDriveSubSessionOnDriveSubQueue() {
    if (g_drive_sub_da_session) return;
    g_drive_sub_da_session = DASessionCreate(kCFAllocatorDefault);
    if (!g_drive_sub_da_session) {
        auditLogf(@"FAIL drive-sub DA session creation");
        return;
    }
    DARegisterDiskAppearedCallback(g_drive_sub_da_session, nullptr,
                                   onDADiskAppeared, nullptr);
    DARegisterDiskDisappearedCallback(g_drive_sub_da_session, nullptr,
                                      onDADiskDisappeared, nullptr);
    DASessionScheduleWithRunLoop(g_drive_sub_da_session,
                                 [[NSRunLoop mainRunLoop] getCFRunLoop],
                                 kCFRunLoopDefaultMode);
}

void tearDownDriveSubSessionOnDriveSubQueueIfIdle() {
    if (!g_drive_sub_da_session) return;
    if (driveSubscribers().count > 0) return;
    DASessionUnscheduleFromRunLoop(g_drive_sub_da_session,
                                   [[NSRunLoop mainRunLoop] getCFRunLoop],
                                   kCFRunLoopDefaultMode);
    CFRelease(g_drive_sub_da_session);
    g_drive_sub_da_session = nullptr;
}

// §7a per-write latency histogram bucket edges. Inclusive upper edge in
// microseconds for each bucket; the final bucket is open-ended (sentinel
// UINT64_MAX). Chosen to span fast NVMe (sub-100 µs) through slow SD-card
// stalls (tens of ms). The edges travel on the wire so the client doesn't
// hard-code them.
constexpr std::array<std::uint64_t, 10> kLatencyBucketUpperUs = {
    100ull, 250ull, 500ull, 1000ull, 2000ull, 5000ull,
    10000ull, 25000ull, 50000ull,
    std::numeric_limits<std::uint64_t>::max(),
};
constexpr std::size_t kNumLatencyBuckets = kLatencyBucketUpperUs.size();

inline std::size_t latencyBucketIndex(std::uint64_t sample_us) {
    for (std::size_t i = 0; i < kNumLatencyBuckets; ++i) {
        if (sample_us <= kLatencyBucketUpperUs[i]) return i;
    }
    return kNumLatencyBuckets - 1;
}

struct OpenSession {
    int fd = -1;
    std::string device_path;
    pid_t client_pid = 0;
    std::string client_signing_identifier;
    std::atomic<std::uint64_t> bytes_written{0};

    // Bulk shared-memory buffer (set by mapBulkBuffer:).
    int shm_fd = -1;             // helper's view of the shm fd
    void* shm_base = nullptr;    // helper-side mapping
    std::uint64_t shm_size = 0;

    // Per-session dispatch queue for async bulk writes. Concurrent so
    // multiple pwrites can be in flight; bounded by `bulk_inflight_sem`.
    dispatch_queue_t bulk_queue = nullptr;
    dispatch_semaphore_t bulk_inflight_sem = nullptr;

    // ---- §7a SessionStats accumulator -------------------------------------
    //
    // Captures helper-side timings the client can't see directly:
    // per-write pwrite latency, fsync time, prepare-device time. Returned
    // to the client as a single NSDictionary in the closeSession reply.
    // All counters are atomic so the bulk-write dispatched blocks can
    // update them concurrently without a mutex.
    std::chrono::steady_clock::time_point session_start =
        std::chrono::steady_clock::now();
    std::atomic<std::uint32_t> write_count{0};         // pwrite() calls
    std::atomic<std::uint64_t> total_write_latency_us{0};
    std::atomic<std::uint32_t> min_write_latency_us{UINT32_MAX};
    std::atomic<std::uint32_t> max_write_latency_us{0};
    std::atomic<std::uint64_t> total_fsync_us{0};
    std::atomic<std::uint32_t> fsync_count{0};
    std::atomic<std::uint64_t> prepare_device_us{0};
    std::atomic<std::uint64_t> hash_device_us{0};
    // §7a per-write latency histogram; latency_buckets[i] counts samples
    // whose latency fell in bucket i (see kLatencyBucketUpperUs).
    std::array<std::atomic<std::uint64_t>, kNumLatencyBuckets> latency_buckets{};
};

// Lock-free min/max update for the latency stats.
inline void updateLatencyMinMax(std::atomic<std::uint32_t>& min_v,
                                  std::atomic<std::uint32_t>& max_v,
                                  std::uint32_t sample_us) {
    std::uint32_t cur_min = min_v.load();
    while (sample_us < cur_min &&
           !min_v.compare_exchange_weak(cur_min, sample_us)) {}
    std::uint32_t cur_max = max_v.load();
    while (sample_us > cur_max &&
           !max_v.compare_exchange_weak(cur_max, sample_us)) {}
}

// Sessions are heap-allocated so we can reference them across mutex
// boundaries without copying the atomic counter.
std::mutex& sessionMutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<std::uint64_t, std::unique_ptr<OpenSession>>& sessionTable() {
    static std::unordered_map<std::uint64_t, std::unique_ptr<OpenSession>> t;
    return t;
}

std::atomic<std::uint64_t>& nextSessionId() {
    static std::atomic<std::uint64_t> n{1};
    return n;
}

// Idle-exit implementation. Must come after sessionTable() so we can
// peek at the live session count from inside the timer handler.
void cancelIdleExitOnIdleQueue() {
    if (g_idle_timer) {
        dispatch_source_cancel(g_idle_timer);
        g_idle_timer = nullptr;
    }
}

void scheduleIdleExitOnIdleQueue() {
    cancelIdleExitOnIdleQueue();
    if (activeConnections().load() != 0) return;
    dispatch_source_t t = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,
                                                   0, 0, idleQueue());
    dispatch_source_set_timer(t,
        dispatch_time(DISPATCH_TIME_NOW,
                      (int64_t)kIdleExitSeconds * NSEC_PER_SEC),
        DISPATCH_TIME_FOREVER, 1 * NSEC_PER_SEC);
    dispatch_source_set_event_handler(t, ^{
        // Re-check under the queue: a connection might have raced in
        // between scheduling and firing.
        if (activeConnections().load() != 0) return;
        {
            std::lock_guard<std::mutex> lk(sessionMutex());
            if (!sessionTable().empty()) {
                // Sessions outlive their parent connection during the
                // brief AUTOCLOSE window; refuse to exit while any are
                // still open. The invalidationHandler will reschedule
                // us once they're cleaned up.
                return;
            }
        }
        auditLogf(@"EXIT helper idle for %ds; no clients, no sessions",
                  (int)kIdleExitSeconds);
        // Use _exit to bypass atexit handlers that could fight launchd.
        _exit(0);
    });
    g_idle_timer = t;
    dispatch_resume(t);
}

// Count current sessions owned by a specific client PID.
std::size_t sessionsForPid(pid_t pid) {
    std::size_t n = 0;
    for (const auto& [_, sess] : sessionTable()) {
        if (sess && sess->client_pid == pid) ++n;
    }
    return n;
}

// ----- Audit log -----------------------------------------------------------
//
// Every privileged operation is recorded as a single line so an
// administrator (or an incident responder) can reconstruct what the
// helper did and on whose behalf. Format is intentionally simple
// (whitespace-separated key=value pairs) so it greps cleanly without
// needing structured-log tooling.
//
// Path: /Library/Logs/Raspberry Pi/Imager/helper.log per design doc §7.
//
// Note: the helper runs as root. We deliberately use the *system*
// /Library/Logs (not NSLibraryDirectory in NSUserDomainMask, which
// would resolve to /var/root/Library/Logs and require sudo to read).
// The directory is created world-traversable (0755) and log files
// are written 0644 so administrators can tail without sudo.
//
// On error opening the file, we fall back to stderr - which the dev
// launchd plist captures and which CI can scrape too.

NSString* helperLogPath() {
    NSString* dir = @"/Library/Logs/Raspberry Pi/Imager";
    [[NSFileManager defaultManager]
        createDirectoryAtPath:dir
        withIntermediateDirectories:YES
                   attributes:@{NSFilePosixPermissions: @(0755)}
                        error:nil];
    return [dir stringByAppendingPathComponent:@"helper.log"];
}

// Rotate the audit log when it exceeds this size. We keep one rotated
// file alongside (helper.log -> helper.log.1). Total disk: 2x the size
// limit. Tuned generously - a busy session typically writes well under
// 100 KB; 10 MB gives room for unusual workloads without thrashing.
constexpr off_t kAuditLogRotateBytes = 10 * 1024 * 1024;

void rotateAuditLogIfNeeded(NSString* path) {
    struct stat st;
    if (::stat(path.UTF8String, &st) != 0) return;
    if (st.st_size < kAuditLogRotateBytes) return;
    NSString* rotated = [path stringByAppendingString:@".1"];
    // Best-effort: remove any existing .1 first so rename succeeds.
    ::unlink(rotated.UTF8String);
    if (::rename(path.UTF8String, rotated.UTF8String) != 0) {
        // Rotation failed (e.g. permissions) - log to stderr and
        // continue; we'd rather not lose the operational signal even
        // if we can't rotate.
        fprintf(stderr, "rpi-imager-writer: log rotate failed: %s\n",
                strerror(errno));
    }
}

void auditLogf(NSString* fmt, ...) {
    // Format the user-supplied message.
    va_list args;
    va_start(args, fmt);
    NSString* msg = [[NSString alloc] initWithFormat:fmt arguments:args];
    va_end(args);

    // Timestamp via strftime - NSDateFormatter alloc-init is ~1 ms
    // per call on macOS and shows up as a hot path under benchmark
    // load. strftime + localtime_r is microseconds.
    char ts[40];
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", &tm_buf);

    NSString* line = [NSString stringWithFormat:@"%s pid=%d %@\n",
                       ts, getpid(), msg];

    NSData* data = [line dataUsingEncoding:NSUTF8StringEncoding];
    NSString* path = helperLogPath();
    bool wrote_to_file = false;
    if (path) {
        rotateAuditLogIfNeeded(path);
        // O_APPEND is atomic on POSIX so concurrent writers don't tear.
        // 0644 so administrators can tail without sudo (helper runs as root).
        int fd = ::open(path.UTF8String,
                        O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            (void)::write(fd, data.bytes, data.length);
            ::close(fd);
            wrote_to_file = true;
        }
    }
    if (!wrote_to_file) {
        // Fallback to stderr so dev runs (which capture stderr) still
        // see the line.
        fputs(line.UTF8String, stderr);
    }
}

// Returns the BSD disk identifier ("diskN") underlying a /dev/disk*
// or /dev/rdisk* path. Empty string on parse failure.
std::string bsdDiskBaseFromPath(const std::string& path) {
    constexpr std::string_view kPrefix = "/dev/";
    if (path.compare(0, kPrefix.size(), kPrefix) != 0) return {};
    std::string rest = path.substr(kPrefix.size());
    if (rest.size() > 1 && rest.compare(0, 5, "rdisk") == 0) rest = rest.substr(1);
    if (rest.compare(0, 4, "disk") != 0) return {};
    // Take "disk" + digits.
    std::size_t end = 4;
    while (end < rest.size() && rest[end] >= '0' && rest[end] <= '9') ++end;
    if (end == 4) return {};
    return rest.substr(0, end);
}

// Walk an IOMedia object's parents to find every BSD name that
// describes the same physical storage. The boot APFS synthesised disk
// (e.g. disk3) is backed by a physical APFS container (disk0s2) which
// lives on disk0; refusing only "disk3" lets a malicious client open
// disk0 directly and overwrite the system. So we collect every BSD
// name in the parent chain and refuse them all.
void collectBsdAncestors(io_object_t obj, std::vector<std::string>& out) {
    if (!IOObjectConformsTo(obj, kIOMediaClass)) return;

    CFTypeRef bsd_name_ref = IORegistryEntrySearchCFProperty(
        obj, kIOServicePlane,
        CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
    if (bsd_name_ref) {
        char buf[64] = {0};
        if (CFGetTypeID(bsd_name_ref) == CFStringGetTypeID() &&
            CFStringGetCString((CFStringRef)bsd_name_ref, buf,
                                sizeof(buf), kCFStringEncodingUTF8)) {
            out.emplace_back(buf);
        }
        CFRelease(bsd_name_ref);
    }

    io_iterator_t parents = IO_OBJECT_NULL;
    if (IORegistryEntryCreateIterator(obj, kIOServicePlane,
                                        kIORegistryIterateRecursively |
                                        kIORegistryIterateParents,
                                        &parents) == KERN_SUCCESS) {
        io_object_t parent;
        while ((parent = IOIteratorNext(parents))) {
            if (IOObjectConformsTo(parent, kIOMediaClass)) {
                CFTypeRef parent_bsd = IORegistryEntrySearchCFProperty(
                    parent, kIOServicePlane,
                    CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
                if (parent_bsd) {
                    char buf[64] = {0};
                    if (CFGetTypeID(parent_bsd) == CFStringGetTypeID() &&
                        CFStringGetCString((CFStringRef)parent_bsd, buf,
                                            sizeof(buf), kCFStringEncodingUTF8)) {
                        out.emplace_back(buf);
                    }
                    CFRelease(parent_bsd);
                }
            }
            IOObjectRelease(parent);
        }
        IOObjectRelease(parents);
    }
}

// Returns the set of BSD disk identifiers that back the system root
// volume. Includes the synthesised APFS disk, the physical store, and
// the whole disk that holds the physical store.
//
// On a typical Apple Silicon Mac:
//   /                 = /dev/disk3s1s1   (System volume snapshot)
//   physical store    = /dev/disk0s2     (Apple_APFS partition)
//   whole disk        = /dev/disk0       (physical storage)
//
// We refuse openSession against any of these.
const std::vector<std::string>& systemDiskBsdNames() {
    static const std::vector<std::string> cached = []{
        std::vector<std::string> result;
        struct statfs sfs;
        if (statfs("/", &sfs) != 0) {
            // Fail closed - if we can't identify the boot disk, refuse
            // every disk0/disk1 as a last-ditch defence.
            result.emplace_back("disk0");
            return result;
        }
        std::string mount_from = sfs.f_mntfromname;  // e.g. /dev/disk3s1s1
        std::string base = bsdDiskBaseFromPath(mount_from);
        if (!base.empty()) result.push_back(base);

        // Walk IOKit to add the physical store + whole disk.
        std::string bsd = mount_from;
        constexpr std::string_view kPrefix = "/dev/";
        if (bsd.compare(0, kPrefix.size(), kPrefix) == 0) {
            bsd = bsd.substr(kPrefix.size());
        }

        CFMutableDictionaryRef matching = IOBSDNameMatching(
            kIOMainPortDefault, 0, bsd.c_str());
        if (matching) {
            io_service_t media = IOServiceGetMatchingService(
                kIOMainPortDefault, matching);
            if (media != IO_OBJECT_NULL) {
                collectBsdAncestors(media, result);
                IOObjectRelease(media);
            }
        }

        // Reduce each entry to its base "diskN" - we refuse the whole
        // disk and any slice on it.
        std::vector<std::string> bases;
        for (const auto& s : result) {
            std::string b = bsdDiskBaseFromPath("/dev/" + s);
            if (b.empty()) b = s;
            // Dedup
            bool seen = false;
            for (const auto& existing : bases) {
                if (existing == b) { seen = true; break; }
            }
            if (!seen) bases.push_back(b);
        }
        return bases;
    }();
    return cached;
}

bool isSystemDiskPath(const std::string& path) {
    std::string base = bsdDiskBaseFromPath(path);
    if (base.empty()) return false;
    for (const auto& sys : systemDiskBsdNames()) {
        if (base == sys) return true;
    }
    return false;
}

// Defence in depth: after open(), verify the OPENED fd doesn't actually
// resolve to a system-disk ancestor. The pre-open path-string check is
// the first line of defence; this second check uses the kernel's own
// st_rdev to look up the IOMedia and walk the parent chain. If our
// path-string parser was tricked, or a TOCTOU race shifted the device
// underneath us, this catches it.
//
// Returns true iff the fd is safe to use; false means the caller MUST
// close the fd and refuse the operation.
bool fdResolvesToNonSystemDisk(int fd) {
    struct stat st;
    if (::fstat(fd, &st) != 0) return false;
    if (!S_ISCHR(st.st_mode) && !S_ISBLK(st.st_mode)) {
        // Not a device file at all - definitely not a real disk.
        // Refuse rather than trying to interpret.
        return false;
    }

    // IOServiceGetMatchingService against IOMediaBSD using major/minor
    // pair. CFDictionary populated with the dev_t.
    CFMutableDictionaryRef matching = IOServiceMatching(kIOMediaClass);
    if (!matching) return false;

    int32_t major_num = static_cast<int32_t>(major(st.st_rdev));
    int32_t minor_num = static_cast<int32_t>(minor(st.st_rdev));
    CFNumberRef cf_major = CFNumberCreate(kCFAllocatorDefault,
                                            kCFNumberSInt32Type, &major_num);
    CFNumberRef cf_minor = CFNumberCreate(kCFAllocatorDefault,
                                            kCFNumberSInt32Type, &minor_num);
    CFDictionarySetValue(matching, CFSTR(kIOBSDMajorKey), cf_major);
    CFDictionarySetValue(matching, CFSTR(kIOBSDMinorKey), cf_minor);
    CFRelease(cf_major);
    CFRelease(cf_minor);

    io_service_t media = IOServiceGetMatchingService(
        kIOMainPortDefault, matching);
    if (media == IO_OBJECT_NULL) {
        // Couldn't resolve - fail closed.
        return false;
    }

    std::vector<std::string> ancestors;
    collectBsdAncestors(media, ancestors);
    IOObjectRelease(media);

    const auto& sys = systemDiskBsdNames();
    for (const auto& a : ancestors) {
        std::string base = bsdDiskBaseFromPath("/dev/" + a);
        if (base.empty()) base = a;
        for (const auto& s : sys) {
            if (base == s) return false;  // matched system disk
        }
    }
    return true;  // safe
}

// Best-effort: extract the client's signing identifier (a short
// human-readable string like "com.raspberrypi.rpi-imager") for the
// audit log. Quietly returns "(unknown)" on failure.
std::string clientSigningIdentifierForToken(audit_token_t token) {
    NSData* tokenData = [NSData dataWithBytes:&token length:sizeof(token)];
    NSDictionary* attrs = @{
        (__bridge NSString*)kSecGuestAttributeAudit : tokenData
    };
    SecCodeRef code = NULL;
    if (SecCodeCopyGuestWithAttributes(NULL,
                                         (__bridge CFDictionaryRef)attrs,
                                         kSecCSDefaultFlags,
                                         &code) != errSecSuccess || !code) {
        return "(unknown)";
    }
    CFDictionaryRef info = NULL;
    OSStatus s = SecCodeCopySigningInformation(code, kSecCSDefaultFlags, &info);
    CFRelease(code);
    if (s != errSecSuccess || !info) return "(unknown)";
    CFStringRef ident = (CFStringRef)CFDictionaryGetValue(info, kSecCodeInfoIdentifier);
    std::string out = "(unknown)";
    if (ident) {
        char buf[256] = {0};
        if (CFStringGetCString(ident, buf, sizeof(buf), kCFStringEncodingUTF8)) {
            out = buf;
        }
    }
    CFRelease(info);
    return out;
}

// SECURITY: pin the set of clients allowed to connect to this helper.
//
// Without this check, any local process that knows our Mach service
// name could connect and call writeAt against /dev/disk0. Since the
// helper runs as root (when installed via SMAppService.daemon), that
// is a local privilege escalation vector.
//
// We require the connecting client to:
//   * be Apple-signed (anchor apple generic),
//   * use the Developer ID intermediate (cert 1 field 1.2.840.113635.100.6.2.6),
//   * be signed by Raspberry Pi LTD's Team ID (8RDZTRXE62),
//   * have bundle identifier "com.raspberrypi.rpi-imager".
//
// Any of these failing rejects the connection. Dev builds set
// RPI_IMAGER_WRITER_DEV=1 in the launchctl environment to skip this
// check; that path MUST NOT be reachable in production deployments.
NSString* const kClientRequirement =
    @"anchor apple generic"
    @" and identifier \"com.raspberrypi.rpi-imager\""
    @" and certificate 1[field.1.2.840.113635.100.6.2.6] /* Developer ID intermediate */"
    @" and certificate leaf[field.1.2.840.113635.100.6.1.13] /* Developer ID leaf */"
    @" and certificate leaf[subject.OU] = \"8RDZTRXE62\"";

bool clientPassesRequirement(NSXPCConnection* connection) {
    audit_token_t token = connection.auditToken;
    NSData* tokenData = [NSData dataWithBytes:&token length:sizeof(token)];

    NSDictionary* attrs = @{
        (__bridge NSString*)kSecGuestAttributeAudit : tokenData
    };

    SecCodeRef code = NULL;
    OSStatus copyStatus = SecCodeCopyGuestWithAttributes(
        NULL,
        (__bridge CFDictionaryRef)attrs,
        kSecCSDefaultFlags,
        &code);
    if (copyStatus != errSecSuccess || !code) {
        os_log_error(OS_LOG_DEFAULT,
            "rpi-imager-writer: SecCodeCopyGuestWithAttributes failed: %{public}d",
            (int)copyStatus);
        return false;
    }

    SecRequirementRef req = NULL;
    OSStatus reqStatus = SecRequirementCreateWithString(
        (__bridge CFStringRef)kClientRequirement,
        kSecCSDefaultFlags,
        &req);
    if (reqStatus != errSecSuccess || !req) {
        os_log_error(OS_LOG_DEFAULT,
            "rpi-imager-writer: SecRequirementCreateWithString failed: %{public}d",
            (int)reqStatus);
        CFRelease(code);
        return false;
    }

    OSStatus checkStatus = SecCodeCheckValidity(code, kSecCSDefaultFlags, req);
    CFRelease(code);
    CFRelease(req);

    if (checkStatus != errSecSuccess) {
        os_log_error(OS_LOG_DEFAULT,
            "rpi-imager-writer: client failed code-signing requirement: %{public}d",
            (int)checkStatus);
        return false;
    }
    return true;
}

}  // namespace

@interface RpiImagerWriterService : NSObject <RpiImagerWriterProtocol, NSXPCListenerDelegate>
@end

@implementation RpiImagerWriterService

- (BOOL)listener:(NSXPCListener*)listener
shouldAcceptNewConnection:(NSXPCConnection*)connection {
    (void)listener;

    // SECURITY: validate the connecting client's code signature.
    // Dev mode (RPI_IMAGER_WRITER_DEV=1) bypasses this; production
    // builds always validate.
    char* devEnv = getenv("RPI_IMAGER_WRITER_DEV");
    bool dev_mode = devEnv && devEnv[0] == '1';

    pid_t pid = connection.processIdentifier;

    if (!dev_mode) {
        if (!clientPassesRequirement(connection)) {
            auditLogf(@"REJECT connect pid=%d reason=signature-mismatch", pid);
            return NO;
        }
    } else {
        auditLogf(@"DEV-MODE accept pid=%d (signature check skipped)", pid);
    }

    audit_token_t token = connection.auditToken;
    std::string ident = clientSigningIdentifierForToken(token);
    auditLogf(@"ACCEPT connect pid=%d ident=%s", pid, ident.c_str());

    // A live client now exists; cancel any pending idle-exit so we
    // don't terminate during the next operation.
    activeConnections().fetch_add(1);
    dispatch_async(idleQueue(), ^{ cancelIdleExitOnIdleQueue(); });

    connection.exportedInterface =
        [NSXPCInterface interfaceWithProtocol:@protocol(RpiImagerWriterProtocol)];
    connection.exportedObject = self;

    // When the client disconnects, close any sessions it left open.
    // Avoids leaks if the GUI crashes mid-write or the user quits the
    // app without explicit teardown.
    pid_t pid_for_handler = pid;
    connection.invalidationHandler = ^{
        std::vector<std::uint64_t> orphaned;
        {
            std::lock_guard<std::mutex> lk(sessionMutex());
            for (auto& [token_id, sess] : sessionTable()) {
                if (sess && sess->client_pid == pid_for_handler) {
                    orphaned.push_back(token_id);
                }
            }
        }
        for (auto sid : orphaned) {
            int fd = -1;
            std::string device;
            std::uint64_t bytes = 0;
            {
                std::lock_guard<std::mutex> lk(sessionMutex());
                auto it = sessionTable().find(sid);
                if (it != sessionTable().end() && it->second) {
                    fd = it->second->fd;
                    device = it->second->device_path;
                    bytes = it->second->bytes_written.load();
                    sessionTable().erase(it);
                }
            }
            if (fd >= 0) ::close(fd);
            auditLogf(@"AUTOCLOSE session=%llu pid=%d device=%s bytes=%llu reason=connection-invalidated",
                      (unsigned long long)sid, pid_for_handler, device.c_str(),
                      (unsigned long long)bytes);
        }

        // Auto-unsubscribe drive-change push notifications. Connection
        // is dying so we can't call back on it either way; just clean
        // up our tracking and tear down the DA session if this was the
        // last subscriber.
        NSXPCConnection* dying_conn = connection;
        dispatch_async(driveSubQueue(), ^{
            if ([driveSubscribers() containsObject:dying_conn]) {
                [driveSubscribers() removeObject:dying_conn];
                tearDownDriveSubSessionOnDriveSubQueueIfIdle();
                auditLogf(@"AUTOUNSUB drive-change pid=%d remaining=%lu",
                          pid_for_handler,
                          (unsigned long)driveSubscribers().count);
            }
        });

        // Last live client to disconnect arms the idle-exit timer.
        // If another connection accepts before the timer fires, the
        // accept path cancels it.
        if (activeConnections().fetch_sub(1) == 1) {
            dispatch_async(idleQueue(), ^{ scheduleIdleExitOnIdleQueue(); });
        }
    };

    [connection resume];
    return YES;
}

- (void)pingWithReply:(void (^)(NSString*))reply {
    NSLog(@"rpi-imager-writer: ping received");
    NSString* version = @"phase-1b-poc";
    reply(version);
}

- (void)subscribeDriveChangesWithReply:
    (void (^)(BOOL, NSString* _Nullable))reply {
    NSXPCConnection* conn = NSXPCConnection.currentConnection;
    if (!conn) {
        reply(NO, @"subscribeDriveChanges: no current connection");
        return;
    }
    // Configure the client-facing direction of this connection so we
    // can invoke driveListChanged on its exportedObject later.
    conn.remoteObjectInterface = [NSXPCInterface
        interfaceWithProtocol:@protocol(RpiImagerWriterClientProtocol)];

    dispatch_async(driveSubQueue(), ^{
        if ([driveSubscribers() containsObject:conn]) {
            return;  // idempotent
        }
        [driveSubscribers() addObject:conn];
        ensureDriveSubSessionOnDriveSubQueue();
        auditLogf(@"OK subscribeDriveChanges pid=%d total=%lu",
                  conn.processIdentifier,
                  (unsigned long)driveSubscribers().count);
    });
    reply(YES, nil);
}

- (void)unsubscribeDriveChangesWithReply:(void (^)(BOOL))reply {
    NSXPCConnection* conn = NSXPCConnection.currentConnection;
    if (!conn) {
        reply(NO);
        return;
    }
    __block BOOL was_subscribed = NO;
    dispatch_sync(driveSubQueue(), ^{
        was_subscribed = [driveSubscribers() containsObject:conn];
        if (was_subscribed) {
            [driveSubscribers() removeObject:conn];
            tearDownDriveSubSessionOnDriveSubQueueIfIdle();
            auditLogf(@"OK unsubscribeDriveChanges pid=%d remaining=%lu",
                      conn.processIdentifier,
                      (unsigned long)driveSubscribers().count);
        }
    });
    reply(was_subscribed);
}

- (void)listDrivesNow:(void (^)(NSArray<NSDictionary*>* _Nullable,
                                  NSString* _Nullable))reply {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<Drivelist::DeviceDescriptor> devices;
    try {
        devices = Drivelist::ListStorageDevices();
    } catch (const std::exception& e) {
        auditLogf(@"FAIL listDrivesNow reason=%s", e.what());
        reply(nil, [NSString stringWithUTF8String:e.what()]);
        return;
    }

    NSMutableArray<NSDictionary*>* out =
        [NSMutableArray arrayWithCapacity:devices.size()];
    for (const auto& d : devices) {
        if (!d.error.empty()) {
            // Skip enumeration errors at the helper boundary; the
            // client still gets a meaningful (and consistent) list of
            // working devices. Errors stay in the helper audit log
            // for forensics.
            auditLogf(@"WARN listDrivesNow enum-error device=%s err=%s",
                      d.device.c_str(), d.error.c_str());
            continue;
        }

        // Convert std::vector<std::string> to NSArray<NSString*>.
        auto toStrings = [](const std::vector<std::string>& v) {
            NSMutableArray<NSString*>* a =
                [NSMutableArray arrayWithCapacity:v.size()];
            for (const auto& s : v) {
                [a addObject:[NSString stringWithUTF8String:s.c_str()]];
            }
            return [a copy];
        };

        NSDictionary* entry = @{
            @"device":          [NSString stringWithUTF8String:d.device.c_str()],
            @"raw":             [NSString stringWithUTF8String:d.raw.c_str()],
            @"description":     [NSString stringWithUTF8String:d.description.c_str()],
            @"size":            @((unsigned long long)d.size),
            @"blockSize":       @((unsigned int)d.blockSize),
            @"logicalBlockSize":@((unsigned int)d.logicalBlockSize),
            @"busType":         [NSString stringWithUTF8String:d.busType.c_str()],
            @"busVersion":      [NSString stringWithUTF8String:d.busVersion.c_str()],
            @"enumerator":      [NSString stringWithUTF8String:d.enumerator.c_str()],
            @"devicePath":      [NSString stringWithUTF8String:d.devicePath.c_str()],
            @"parentDevice":    [NSString stringWithUTF8String:d.parentDevice.c_str()],
            @"mountpoints":     toStrings(d.mountpoints),
            @"mountpointLabels":toStrings(d.mountpointLabels),
            @"childDevices":    toStrings(d.childDevices),
            @"isReadOnly":      @(d.isReadOnly),
            @"isSystem":        @(d.isSystem),
            @"isVirtual":       @(d.isVirtual),
            @"isRemovable":     @(d.isRemovable),
            @"isCard":          @(d.isCard),
            @"isSCSI":          @(d.isSCSI),
            @"isUSB":           @(d.isUSB),
            @"isUAS":           @(d.isUAS),
            @"busVersionNull":  @(d.busVersionNull),
            @"devicePathNull":  @(d.devicePathNull),
            @"isUASNull":       @(d.isUASNull),
        };
        [out addObject:entry];
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    auditLogf(@"OK listDrivesNow count=%lu elapsed_ms=%lld",
              (unsigned long)out.count, (long long)elapsed_ms);
    reply([out copy], nil);
}

- (void)unmountDevice:(NSString*)devicePath
                reply:(void (^)(BOOL, NSString* _Nullable, int32_t))reply {
    namespace da = rpi_imager::writer::da;

    NSXPCConnection* conn = NSXPCConnection.currentConnection;
    pid_t client_pid = conn ? conn.processIdentifier : 0;

    std::string path = devicePath.UTF8String;
    auto result = da::unmountDisk(path);
    if (result == da::Result::Success) {
        auditLogf(@"OK unmount path=%s pid=%d", path.c_str(), client_pid);
        reply(YES, nil, 0);
        return;
    }

    int32_t status = da::lastKernelStatus();
    auditLogf(@"FAIL unmount path=%s pid=%d kernel_status=%d detail=%s",
              path.c_str(), client_pid, status,
              da::lastErrorDetail().c_str());
    NSString* detail = [NSString stringWithUTF8String:da::lastErrorDetail().c_str()];
    reply(NO, detail, status);
}

- (void)openDevice:(NSString*)devicePath
              reply:(void (^)(uint64_t, NSString* _Nullable, int32_t))reply {
    NSLog(@"rpi-imager-writer: openDevice %@", devicePath);

    std::string path = devicePath.UTF8String;

    // SECURITY: defence in depth. Even with the connecting-client check,
    // a compromised client should not be able to open arbitrary paths.
    // Restrict the helper to /dev/diskN and /dev/rdiskN, and reject any
    // path containing characters that could enable traversal tricks.
    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    auto isValidDiskPath = [&](const std::string& p) -> bool {
        // Must be exactly /dev/disk<digits> or /dev/rdisk<digits>,
        // optionally followed by sN slice; we allow the slice form
        // because BSD names like /dev/disk7s1 are legitimate too.
        std::size_t pos = 0;
        if (p.compare(pos, 5, "/dev/") != 0) return false;
        pos = 5;
        if (p.compare(pos, 5, "rdisk") == 0) pos += 5;
        else if (p.compare(pos, 4, "disk") == 0) pos += 4;
        else return false;
        // require at least one digit after disk/rdisk
        if (pos >= p.size() || !isDigit(p[pos])) return false;
        // remainder must be digits and an optional 's<digits>' suffix
        bool seen_s = false;
        for (; pos < p.size(); ++pos) {
            char c = p[pos];
            if (isDigit(c)) continue;
            if (c == 's' && !seen_s) { seen_s = true; continue; }
            return false;
        }
        return true;
    };

    if (!isValidDiskPath(path)) {
        auditLogf(@"REJECT openDevice path=%s reason=invalid-path", path.c_str());
        reply(0,
              [NSString stringWithFormat:
                  @"openDevice: path %@ is not a valid /dev/disk* or /dev/rdisk* node",
                  devicePath],
              0);
        return;
    }

    // SECURITY: refuse the system disk. Even a properly authorised
    // client should never be able to image over the user's boot volume
    // via this helper. The check covers the synthesised APFS disk,
    // its physical store, and the whole physical storage device, so
    // an attacker cannot bypass by asking for "/dev/disk0" instead of
    // "/dev/disk3".
    if (isSystemDiskPath(path)) {
        auditLogf(@"REJECT openDevice path=%s reason=system-disk", path.c_str());
        reply(0,
              [NSString stringWithFormat:
                  @"openDevice: %@ contains the system root volume; refusing",
                  devicePath],
              0);
        return;
    }

    // Identify the calling client. Used for per-client session caps and
    // for the audit log.
    NSXPCConnection* conn = NSXPCConnection.currentConnection;
    pid_t client_pid = conn ? conn.processIdentifier : 0;
    std::string client_ident = "(unknown)";
    if (conn) {
        audit_token_t tok = conn.auditToken;
        client_ident = clientSigningIdentifierForToken(tok);
    }

    // Enforce max concurrent sessions per client.
    {
        std::lock_guard<std::mutex> lk(sessionMutex());
        if (sessionsForPid(client_pid) >= kMaxSessionsPerClient) {
            auditLogf(@"REJECT openDevice path=%s pid=%d ident=%s reason=too-many-sessions",
                      path.c_str(), client_pid, client_ident.c_str());
            reply(0,
                  [NSString stringWithFormat:
                      @"openDevice: client already has %zu open sessions (limit)",
                      kMaxSessionsPerClient],
                  0);
            return;
        }
    }

    int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) {
        int e = errno;
        char buf[128] = {0};
        snprintf(buf, sizeof(buf), "open(%s) failed: %s",
                 path.c_str(), strerror(e));
        auditLogf(@"FAIL openDevice path=%s pid=%d errno=%d (%s)",
                  path.c_str(), client_pid, e, strerror(e));
        reply(0, [NSString stringWithUTF8String:buf], e);
        return;
    }

    // Post-open verification: re-check via the kernel's own dev_t lookup.
    // Catches path-parse bypasses, TOCTOU races, and any future regression
    // in the pre-open string check.
    if (!fdResolvesToNonSystemDisk(fd)) {
        ::close(fd);
        auditLogf(@"REJECT openDevice path=%s reason=post-open-system-disk-detected",
                  path.c_str());
        reply(0,
              [NSString stringWithFormat:
                  @"openDevice: %@ resolved to system-disk media via fd; refusing",
                  devicePath],
              0);
        return;
    }

    std::uint64_t token = nextSessionId().fetch_add(1);
    {
        auto sess = std::make_unique<OpenSession>();
        sess->fd = fd;
        sess->device_path = path;
        sess->client_pid = client_pid;
        sess->client_signing_identifier = client_ident;
        // Per-session concurrent queue + bounded-parallelism semaphore
        // for the bulk-write async path. Created here so the helper
        // can dispatch_async pwrites onto it; freed in closeSession.
        sess->bulk_queue = dispatch_queue_create(
            "com.raspberrypi.rpi-imager.writer.bulk",
            DISPATCH_QUEUE_CONCURRENT);
        sess->bulk_inflight_sem = dispatch_semaphore_create(kBulkInFlightCap);
        std::lock_guard<std::mutex> lk(sessionMutex());
        sessionTable()[token] = std::move(sess);
    }
    auditLogf(@"OK openDevice session=%llu fd=%d path=%s pid=%d ident=%s",
              (unsigned long long)token, fd, path.c_str(),
              client_pid, client_ident.c_str());
    reply(token, nil, 0);
}

- (void)queryDeviceSize:(uint64_t)sessionToken
                  reply:(void (^)(uint64_t, NSString* _Nullable))reply {
    int fd = -1;
    {
        std::lock_guard<std::mutex> lk(sessionMutex());
        auto it = sessionTable().find(sessionToken);
        if (it == sessionTable().end() || !it->second) {
            reply(0, @"queryDeviceSize: unknown session token");
            return;
        }
        fd = it->second->fd;
    }

    // /dev/disk* and /dev/rdisk* support DKIOCGETBLOCKCOUNT + GETBLOCKSIZE
    uint64_t blocks = 0;
    uint32_t blockSize = 0;
    if (ioctl(fd, DKIOCGETBLOCKCOUNT, &blocks) != 0) {
        int e = errno;
        char buf[128] = {0};
        snprintf(buf, sizeof(buf), "ioctl(DKIOCGETBLOCKCOUNT): %s", strerror(e));
        reply(0, [NSString stringWithUTF8String:buf]);
        return;
    }
    if (ioctl(fd, DKIOCGETBLOCKSIZE, &blockSize) != 0) {
        int e = errno;
        char buf[128] = {0};
        snprintf(buf, sizeof(buf), "ioctl(DKIOCGETBLOCKSIZE): %s", strerror(e));
        reply(0, [NSString stringWithUTF8String:buf]);
        return;
    }
    reply(blocks * static_cast<uint64_t>(blockSize), nil);
}

- (void)readAt:(uint64_t)sessionToken
         offset:(uint64_t)offset
         length:(uint64_t)length
          reply:(void (^)(NSData* _Nullable, NSString* _Nullable, int32_t))reply {
    int fd = -1;
    {
        std::lock_guard<std::mutex> lk(sessionMutex());
        auto it = sessionTable().find(sessionToken);
        if (it == sessionTable().end() || !it->second) {
            reply(nil, @"readAt: unknown session token", 0);
            return;
        }
        fd = it->second->fd;
    }

    // Bound the read size to a sensible upper limit for the synchronous
    // (NSData-over-XPC) path. This path is for small chunks only - the
    // bulk read path uses shared memory.
    constexpr uint64_t kMaxBytes = 4ull * 1024 * 1024;
    if (length == 0 || length > kMaxBytes) {
        reply(nil,
              [NSString stringWithFormat:@"readAt: invalid length %llu (max %llu)",
                                          length, kMaxBytes],
              0);
        return;
    }

    NSMutableData* buf = [NSMutableData dataWithLength:length];
    ssize_t got = ::pread(fd, buf.mutableBytes, length, (off_t)offset);
    if (got < 0) {
        int e = errno;
        char msg[160] = {0};
        snprintf(msg, sizeof(msg),
                 "pread(fd=%d, len=%llu, off=%llu): %s", fd, length, offset,
                 strerror(e));
        reply(nil, [NSString stringWithUTF8String:msg], e);
        return;
    }
    if ((uint64_t)got != length) {
        // Short read - shrink buffer to actual bytes returned.
        [buf setLength:(NSUInteger)got];
    }
    reply(buf, nil, 0);
}

- (void)writeAt:(uint64_t)sessionToken
          offset:(uint64_t)offset
            data:(NSData*)data
           reply:(void (^)(uint64_t, NSString* _Nullable, int32_t))reply {
    int fd = -1;
    OpenSession* sess_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lk(sessionMutex());
        auto it = sessionTable().find(sessionToken);
        if (it == sessionTable().end() || !it->second) {
            reply(0, @"writeAt: unknown session token", 0);
            return;
        }
        fd = it->second->fd;
        sess_ptr = it->second.get();
    }

    constexpr uint64_t kMaxBytes = 4ull * 1024 * 1024;
    if (data.length == 0 || data.length > kMaxBytes) {
        reply(0,
              [NSString stringWithFormat:@"writeAt: invalid length %lu (max %llu)",
                                          (unsigned long)data.length, kMaxBytes],
              0);
        return;
    }

    // Per-session bytes-written cap. Atomic load+CAS would be slightly
    // tidier, but the lifetime guarantees here (session held in
    // sessionTable until closeSession() removes it; we take a raw
    // pointer above only after locking) make the relaxed check safe.
    std::uint64_t already = sess_ptr->bytes_written.load();
    if (already + data.length > kMaxSessionBytesWritten) {
        auditLogf(@"REJECT writeAt session=%llu reason=session-byte-cap "
                   "already=%llu requested=%lu cap=%llu",
                   (unsigned long long)sessionToken,
                   (unsigned long long)already,
                   (unsigned long)data.length,
                   (unsigned long long)kMaxSessionBytesWritten);
        reply(0,
              @"writeAt: session bytes-written cap reached",
              0);
        return;
    }

    auto t_w0 = std::chrono::steady_clock::now();
    ssize_t wrote = ::pwrite(fd, data.bytes, data.length, (off_t)offset);
    auto t_w1 = std::chrono::steady_clock::now();
    const std::uint32_t lat_us = (std::uint32_t)
        std::chrono::duration_cast<std::chrono::microseconds>(t_w1 - t_w0).count();
    if (wrote < 0) {
        int e = errno;
        char msg[160] = {0};
        snprintf(msg, sizeof(msg),
                 "pwrite(fd=%d, len=%lu, off=%llu): %s",
                 fd, (unsigned long)data.length, offset, strerror(e));
        auditLogf(@"FAIL writeAt session=%llu offset=%llu len=%lu errno=%d",
                  (unsigned long long)sessionToken,
                  (unsigned long long)offset,
                  (unsigned long)data.length, e);
        reply(0, [NSString stringWithUTF8String:msg], e);
        return;
    }
    sess_ptr->bytes_written.fetch_add((std::uint64_t)wrote);
    // §7a per-write latency accumulation (writeAt is the sync path,
    // bulkWriteFromBuffer is the async bulk path; same stats bucket).
    sess_ptr->write_count.fetch_add(1);
    sess_ptr->total_write_latency_us.fetch_add(lat_us);
    updateLatencyMinMax(sess_ptr->min_write_latency_us,
                         sess_ptr->max_write_latency_us, lat_us);
    sess_ptr->latency_buckets[latencyBucketIndex(lat_us)].fetch_add(1);
    // No per-write OK log; see bulkWriteFromBuffer for rationale.
    reply((uint64_t)wrote, nil, 0);
}

- (void)mapBulkBuffer:(uint64_t)sessionToken
            sizeBytes:(uint64_t)sizeBytes
                reply:(void (^)(NSFileHandle* _Nullable, NSString* _Nullable))reply {
    OpenSession* sess = nullptr;
    {
        std::lock_guard<std::mutex> lk(sessionMutex());
        auto it = sessionTable().find(sessionToken);
        if (it == sessionTable().end() || !it->second) {
            reply(nil, @"mapBulkBuffer: unknown session token");
            return;
        }
        sess = it->second.get();
    }

    // size==0 releases any previously-mapped buffer (idempotent cleanup).
    if (sizeBytes == 0) {
        if (sess->shm_base) {
            munmap(sess->shm_base, sess->shm_size);
            sess->shm_base = nullptr;
            sess->shm_size = 0;
        }
        if (sess->shm_fd >= 0) {
            ::close(sess->shm_fd);
            sess->shm_fd = -1;
        }
        reply(nil, nil);
        return;
    }

    // Bound the size to a RAM-proportional ceiling (1/3 of physical RAM,
    // floored at 256 MB; see maxBulkBufferBytes). This scales with the
    // machine so it covers the client's RAM-aware write-ring sizing instead
    // of fighting it with a fixed limit, while still bounding RSS exposure
    // from a buggy/compromised client.
    const std::uint64_t kMaxBulkBuffer = maxBulkBufferBytes();
    if (sizeBytes > kMaxBulkBuffer) {
        reply(nil,
              [NSString stringWithFormat:@"mapBulkBuffer: size %llu > cap %llu",
                                          (unsigned long long)sizeBytes,
                                          (unsigned long long)kMaxBulkBuffer]);
        return;
    }

    // If the session has already burned its byte budget, refuse the
    // ring allocation outright. The check is duplicated at submit time
    // for correctness, but failing fast here is more honest UX -
    // the client doesn't allocate, mmap, and then discover it can't
    // use the buffer.
    if (sess->bytes_written.load() >= kMaxSessionBytesWritten) {
        auditLogf(@"REJECT mapBulkBuffer session=%llu reason=session-byte-cap-exhausted",
                  (unsigned long long)sessionToken);
        reply(nil, @"mapBulkBuffer: session bytes-written cap already reached");
        return;
    }

    // Create an anonymous shm: shm_open + immediate shm_unlink so the
    // name is gone from the namespace; only the fd retains access. We
    // use a random name to avoid collisions across concurrent sessions.
    //
    // macOS limits shared-memory names to ~31 chars (PSHMNAMLEN); the
    // previous "/rpi-imager-writer-shm-<pid>-<rand>" overran that and
    // shm_open returned ENAMETOOLONG. The session token plus a small
    // random suffix is more than sufficient to be unique (the name is
    // only live for the duration of this call - we unlink immediately).
    std::random_device rd;
    char name[32];
    snprintf(name, sizeof(name), "/rpii-%llx-%x",
             (unsigned long long)sessionToken,
             (unsigned)rd() & 0xffffu);

    int shm_fd = ::shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (shm_fd < 0) {
        reply(nil, [NSString stringWithFormat:@"shm_open: %s", strerror(errno)]);
        return;
    }
    ::shm_unlink(name);  // detach from namespace immediately

    if (::ftruncate(shm_fd, (off_t)sizeBytes) != 0) {
        ::close(shm_fd);
        reply(nil, [NSString stringWithFormat:@"ftruncate(%llu): %s",
                                                (unsigned long long)sizeBytes,
                                                strerror(errno)]);
        return;
    }

    void* base = ::mmap(NULL, (size_t)sizeBytes,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED, shm_fd, 0);
    if (base == MAP_FAILED) {
        int e = errno;
        ::close(shm_fd);
        reply(nil, [NSString stringWithFormat:@"mmap: %s", strerror(e)]);
        return;
    }

    // Replace any prior buffer.
    if (sess->shm_base) munmap(sess->shm_base, sess->shm_size);
    if (sess->shm_fd >= 0) ::close(sess->shm_fd);

    sess->shm_fd = shm_fd;
    sess->shm_base = base;
    sess->shm_size = sizeBytes;

    auditLogf(@"OK mapBulkBuffer session=%llu size=%llu",
              (unsigned long long)sessionToken,
              (unsigned long long)sizeBytes);

    // Hand the fd to the client; NSFileHandle keeps it alive for the
    // duration of the XPC reply. The client mmaps it on its end.
    NSFileHandle* fh = [[NSFileHandle alloc] initWithFileDescriptor:shm_fd
                                                       closeOnDealloc:NO];
    reply(fh, nil);
}

- (void)bulkWriteFromBuffer:(uint64_t)sessionToken
               bufferOffset:(uint64_t)bufferOffset
               deviceOffset:(uint64_t)deviceOffset
                     length:(uint64_t)length
                      reply:(void (^)(uint64_t, NSString* _Nullable, int32_t))reply {
    // Capture session state under the lock. After dispatching, the
    // session may be torn down concurrently; closeSession does a
    // dispatch_barrier_sync on bulk_queue to wait for all in-flight
    // blocks to complete before munmap-ing shm_base, so the captured
    // pointers stay valid for the duration of the async pwrite.
    int fd = -1;
    void* shm_base = nullptr;
    std::uint64_t shm_size = 0;
    std::atomic<std::uint64_t>* bytes_written_ptr = nullptr;
    std::atomic<std::uint32_t>* write_count_ptr = nullptr;
    std::atomic<std::uint64_t>* total_write_lat_ptr = nullptr;
    std::atomic<std::uint32_t>* min_lat_ptr = nullptr;
    std::atomic<std::uint32_t>* max_lat_ptr = nullptr;
    std::array<std::atomic<std::uint64_t>, kNumLatencyBuckets>* latency_buckets_ptr = nullptr;
    dispatch_queue_t bulk_queue = nullptr;
    dispatch_semaphore_t inflight_sem = nullptr;
    {
        std::lock_guard<std::mutex> lk(sessionMutex());
        auto it = sessionTable().find(sessionToken);
        if (it == sessionTable().end() || !it->second) {
            reply(0, @"bulkWriteFromBuffer: unknown session token", 0);
            return;
        }
        OpenSession* sess = it->second.get();
        fd = sess->fd;
        shm_base = sess->shm_base;
        shm_size = sess->shm_size;
        bytes_written_ptr = &sess->bytes_written;
        write_count_ptr = &sess->write_count;
        total_write_lat_ptr = &sess->total_write_latency_us;
        min_lat_ptr = &sess->min_write_latency_us;
        max_lat_ptr = &sess->max_write_latency_us;
        latency_buckets_ptr = &sess->latency_buckets;
        bulk_queue = sess->bulk_queue;
        inflight_sem = sess->bulk_inflight_sem;
    }

    if (!shm_base) {
        reply(0, @"bulkWriteFromBuffer: no bulk buffer mapped (call mapBulkBuffer first)", 0);
        return;
    }
    // Bounds check against the shm region.
    if (length == 0 || bufferOffset > shm_size || length > shm_size - bufferOffset) {
        reply(0,
              [NSString stringWithFormat:
                  @"bulkWriteFromBuffer: out of bounds (offset=%llu len=%llu shm_size=%llu)",
                  (unsigned long long)bufferOffset,
                  (unsigned long long)length,
                  (unsigned long long)shm_size],
              0);
        return;
    }
    // Pre-flight bytes-written cap (the dispatched block re-checks
    // atomically since multiple in-flight writes can race the cap).
    std::uint64_t already = bytes_written_ptr->load();
    if (already + length > kMaxSessionBytesWritten) {
        auditLogf(@"REJECT bulkWriteFromBuffer session=%llu reason=session-byte-cap",
                  (unsigned long long)sessionToken);
        reply(0, @"bulkWriteFromBuffer: session bytes-written cap reached", 0);
        return;
    }

    // Async dispatch: handler returns immediately so the connection
    // can process the next message; the actual pwrite happens on a
    // worker thread, the reply block is captured and invoked on
    // completion. The semaphore bounds in-flight pwrites at
    // kBulkInFlightCap regardless of how many writes the client has
    // queued up.
    void* src = static_cast<char*>(shm_base) + bufferOffset;
    uint64_t session_for_log = sessionToken;
    uint64_t dev_off_for_log = deviceOffset;
    uint64_t len_for_log = length;

    dispatch_async(bulk_queue, ^{
        dispatch_semaphore_wait(inflight_sem, DISPATCH_TIME_FOREVER);

        // Atomic CAS on bytes_written to enforce the per-session cap
        // even with multiple in-flight writes. If the cap is exceeded
        // by the time we run, refuse the write.
        std::uint64_t current = bytes_written_ptr->load();
        bool cap_ok = false;
        while (current + len_for_log <= kMaxSessionBytesWritten) {
            if (bytes_written_ptr->compare_exchange_weak(
                    current, current + len_for_log)) {
                cap_ok = true;
                break;
            }
        }
        if (!cap_ok) {
            dispatch_semaphore_signal(inflight_sem);
            auditLogf(@"REJECT bulkWriteFromBuffer-async session=%llu reason=session-byte-cap",
                      session_for_log);
            reply(0, @"bulkWriteFromBuffer: session bytes-written cap reached", 0);
            return;
        }

        auto t_pwrite_start = std::chrono::steady_clock::now();
        ssize_t wrote = ::pwrite(fd, src, (size_t)len_for_log,
                                  (off_t)dev_off_for_log);
        auto t_pwrite_end = std::chrono::steady_clock::now();
        dispatch_semaphore_signal(inflight_sem);

        // §7a per-write latency accumulation. Capture even on failure
        // so the histogram includes pathological tail latencies (a slow
        // failing write is informative).
        const std::uint32_t lat_us = (std::uint32_t)
            std::chrono::duration_cast<std::chrono::microseconds>(
                t_pwrite_end - t_pwrite_start).count();
        write_count_ptr->fetch_add(1);
        total_write_lat_ptr->fetch_add(lat_us);
        updateLatencyMinMax(*min_lat_ptr, *max_lat_ptr, lat_us);
        (*latency_buckets_ptr)[latencyBucketIndex(lat_us)].fetch_add(1);

        if (wrote < 0) {
            // Roll back the CAS on failure.
            bytes_written_ptr->fetch_sub(len_for_log);
            int e = errno;
            char msg[160] = {0};
            snprintf(msg, sizeof(msg),
                     "pwrite(fd=%d, len=%llu, off=%llu): %s",
                     fd, (unsigned long long)len_for_log,
                     (unsigned long long)dev_off_for_log, strerror(e));
            auditLogf(@"FAIL bulkWriteFromBuffer session=%llu errno=%d",
                      session_for_log, e);
            reply(0, [NSString stringWithUTF8String:msg], e);
            return;
        }
        // Adjust the CAS if pwrite was short.
        if ((std::uint64_t)wrote < len_for_log) {
            bytes_written_ptr->fetch_sub(len_for_log - (std::uint64_t)wrote);
        }
        // Intentionally NO per-write OK audit log here. At 4 MB chunks
        // a 5 GB image is 1250 writes; logging each costs ~1 ms (fopen
        // + NSDateFormatter + fclose) and dominates the hot path. The
        // closeSession line records final bytes_written for forensics;
        // FAIL/REJECT paths above still log because errors are rare and
        // diagnostically valuable.
        reply((uint64_t)wrote, nil, 0);
    });
    // Handler returns now; reply will fire from the worker block.
}

- (void)hashDeviceSha256:(uint64_t)sessionToken
             prefixBytes:(NSData* _Nullable)prefixBytes
            deviceOffset:(uint64_t)deviceOffset
                  length:(uint64_t)length
                   reply:(void (^)(NSData* _Nullable,
                                   uint64_t,
                                   NSString* _Nullable,
                                   int32_t))reply {
    auto t_hash_start = std::chrono::steady_clock::now();
    int fd = -1;
    OpenSession* sess_for_stats = nullptr;
    {
        std::lock_guard<std::mutex> lk(sessionMutex());
        auto it = sessionTable().find(sessionToken);
        if (it == sessionTable().end() || !it->second) {
            reply(nil, 0, @"hashDeviceSha256: unknown session token", 0);
            return;
        }
        fd = it->second->fd;
        sess_for_stats = it->second.get();
    }

    // RAII duration capture for §7a hash_device_us regardless of exit path.
    struct HashTimer {
        std::atomic<std::uint64_t>* target;
        std::chrono::steady_clock::time_point start;
        ~HashTimer() {
            auto end = std::chrono::steady_clock::now();
            const std::uint64_t us = (std::uint64_t)
                std::chrono::duration_cast<std::chrono::microseconds>(
                    end - start).count();
            target->store(us);
        }
    };
    HashTimer _hash_timer{&sess_for_stats->hash_device_us, t_hash_start};
    (void)_hash_timer;

    // Hash buffer sized to amortise read syscall overhead while staying
    // page-cache friendly. 16 MB matches the bulk-write slot default.
    constexpr std::size_t kReadChunk = 16ull * 1024 * 1024;
    std::vector<std::uint8_t> buf(kReadChunk);

    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);

    // Prefix is the imager's first block - written to disk last so it
    // isn't on the device during verify, but it IS part of the
    // logical hash the client computed during write. Fold it into the
    // hasher up-front so we end up with a single combined digest.
    if (prefixBytes && prefixBytes.length > 0) {
        CC_SHA256_Update(&ctx,
                         prefixBytes.bytes,
                         (CC_LONG)prefixBytes.length);
    }

    std::uint64_t remaining = length;
    std::uint64_t cursor = deviceOffset;
    std::uint64_t total_hashed = prefixBytes ? prefixBytes.length : 0;

    while (remaining > 0) {
        const std::size_t want =
            (std::size_t)std::min<std::uint64_t>(remaining, kReadChunk);
        ssize_t got = ::pread(fd, buf.data(), want, (off_t)cursor);
        if (got < 0) {
            int e = errno;
            char msg[160] = {0};
            snprintf(msg, sizeof(msg),
                     "pread(fd=%d, len=%zu, off=%llu): %s",
                     fd, want, (unsigned long long)cursor, strerror(e));
            auditLogf(@"FAIL hashDeviceSha256 session=%llu errno=%d",
                      (unsigned long long)sessionToken, e);
            reply(nil, total_hashed,
                  [NSString stringWithUTF8String:msg], e);
            return;
        }
        if (got == 0) break;  // EOF
        CC_SHA256_Update(&ctx, buf.data(), (CC_LONG)got);
        cursor += (std::uint64_t)got;
        remaining -= (std::uint64_t)got;
        total_hashed += (std::uint64_t)got;
    }

    std::uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &ctx);

    auditLogf(@"OK hashDeviceSha256 session=%llu bytes=%llu",
              (unsigned long long)sessionToken,
              (unsigned long long)total_hashed);
    NSData* d = [NSData dataWithBytes:digest length:CC_SHA256_DIGEST_LENGTH];
    reply(d, total_hashed, nil, 0);
}

- (void)syncSession:(uint64_t)sessionToken
              reply:(void (^)(BOOL, NSString* _Nullable, int32_t))reply {
    int fd = -1;
    OpenSession* sess_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lk(sessionMutex());
        auto it = sessionTable().find(sessionToken);
        if (it == sessionTable().end() || !it->second) {
            reply(NO, @"syncSession: unknown session token", 0);
            return;
        }
        fd = it->second->fd;
        sess_ptr = it->second.get();
    }
    auto t_s0 = std::chrono::steady_clock::now();
    int rc = ::fsync(fd);
    auto t_s1 = std::chrono::steady_clock::now();
    const std::uint64_t fsync_us = (std::uint64_t)
        std::chrono::duration_cast<std::chrono::microseconds>(t_s1 - t_s0).count();
    sess_ptr->total_fsync_us.fetch_add(fsync_us);
    sess_ptr->fsync_count.fetch_add(1);
    if (rc != 0) {
        int e = errno;
        char msg[128] = {0};
        snprintf(msg, sizeof(msg), "fsync(fd=%d): %s", fd, strerror(e));
        auditLogf(@"FAIL syncSession session=%llu errno=%d",
                  (unsigned long long)sessionToken, e);
        reply(NO, [NSString stringWithUTF8String:msg], e);
        return;
    }
    auditLogf(@"OK syncSession session=%llu",
              (unsigned long long)sessionToken);
    reply(YES, nil, 0);
}

- (void)prepareDevice:(uint64_t)sessionToken
          zeroFirstMb:(BOOL)zeroFirstMb
           zeroLastMb:(BOOL)zeroLastMb
                reply:(void (^)(BOOL, NSString* _Nullable, int32_t))reply {
    auto t_prep_start = std::chrono::steady_clock::now();
    int fd = -1;
    OpenSession* sess_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lk(sessionMutex());
        auto it = sessionTable().find(sessionToken);
        if (it == sessionTable().end() || !it->second) {
            reply(NO, @"prepareDevice: unknown session token", 0);
            return;
        }
        fd = it->second->fd;
        sess_ptr = it->second.get();
    }

    // RAII-style duration capture so the stat updates regardless of
    // which return path the method takes below.
    struct PrepareTimer {
        std::atomic<std::uint64_t>* target;
        std::chrono::steady_clock::time_point start;
        ~PrepareTimer() {
            auto end = std::chrono::steady_clock::now();
            const std::uint64_t us = (std::uint64_t)
                std::chrono::duration_cast<std::chrono::microseconds>(
                    end - start).count();
            target->store(us);
        }
    };
    PrepareTimer _prep_timer{&sess_ptr->prepare_device_us, t_prep_start};
    (void)_prep_timer;

    constexpr std::size_t kOneMB = 1024ull * 1024ull;

    // Discover the device size up-front; we need it to compute the
    // last-MB offset. Failing here is fatal for the last-MB request but
    // tolerable when only zeroFirstMb is set.
    std::uint64_t deviceBytes = 0;
    if (zeroLastMb) {
        std::uint64_t blocks = 0;
        std::uint32_t blockSize = 0;
        if (ioctl(fd, DKIOCGETBLOCKCOUNT, &blocks) != 0) {
            int e = errno;
            char msg[160] = {0};
            snprintf(msg, sizeof(msg),
                     "prepareDevice: DKIOCGETBLOCKCOUNT: %s", strerror(e));
            auditLogf(@"FAIL prepareDevice session=%llu reason=blockcount errno=%d",
                      (unsigned long long)sessionToken, e);
            reply(NO, [NSString stringWithUTF8String:msg], e);
            return;
        }
        if (ioctl(fd, DKIOCGETBLOCKSIZE, &blockSize) != 0) {
            int e = errno;
            char msg[160] = {0};
            snprintf(msg, sizeof(msg),
                     "prepareDevice: DKIOCGETBLOCKSIZE: %s", strerror(e));
            auditLogf(@"FAIL prepareDevice session=%llu reason=blocksize errno=%d",
                      (unsigned long long)sessionToken, e);
            reply(NO, [NSString stringWithUTF8String:msg], e);
            return;
        }
        deviceBytes = blocks * static_cast<std::uint64_t>(blockSize);
        if (deviceBytes < kOneMB) {
            // Device too small for last-MB scrub; silently skip rather
            // than fail - this matches downloadthread.cpp's own guard.
            zeroLastMb = NO;
        }
    }

    // Per-session bytes-written cap accounting. zero_first + zero_last
    // is at most 2 MB.
    const std::uint64_t need_bytes =
        (zeroFirstMb ? kOneMB : 0) + (zeroLastMb ? kOneMB : 0);
    std::uint64_t already = sess_ptr->bytes_written.load();
    if (already + need_bytes > kMaxSessionBytesWritten) {
        auditLogf(@"REJECT prepareDevice session=%llu reason=session-byte-cap "
                   "already=%llu need=%llu",
                   (unsigned long long)sessionToken,
                   (unsigned long long)already,
                   (unsigned long long)need_bytes);
        reply(NO, @"prepareDevice: session bytes-written cap reached", 0);
        return;
    }

    // Zero buffer is reused for both writes.
    std::vector<std::uint8_t> zeros(kOneMB, 0);

    auto pwriteAll = [fd](const void* buf, std::size_t len, off_t off,
                          std::string& err, int& kerrno) -> bool {
        std::size_t remaining = len;
        const std::uint8_t* p = static_cast<const std::uint8_t*>(buf);
        off_t cur = off;
        while (remaining > 0) {
            ssize_t wrote = ::pwrite(fd, p, remaining, cur);
            if (wrote <= 0) {
                kerrno = errno;
                char msg[160] = {0};
                snprintf(msg, sizeof(msg),
                         "pwrite(fd=%d, len=%zu, off=%lld): %s",
                         fd, remaining, (long long)cur, strerror(kerrno));
                err = msg;
                return false;
            }
            cur += wrote;
            p += wrote;
            remaining -= static_cast<std::size_t>(wrote);
        }
        return true;
    };

    if (zeroFirstMb) {
        std::string err;
        int kerrno = 0;
        if (!pwriteAll(zeros.data(), kOneMB, 0, err, kerrno)) {
            auditLogf(@"FAIL prepareDevice session=%llu phase=first-mb errno=%d",
                      (unsigned long long)sessionToken, kerrno);
            reply(NO, [NSString stringWithUTF8String:err.c_str()], kerrno);
            return;
        }
        sess_ptr->bytes_written.fetch_add(kOneMB);
    }

    if (zeroLastMb) {
        std::string err;
        int kerrno = 0;
        off_t off = static_cast<off_t>(deviceBytes - kOneMB);
        if (!pwriteAll(zeros.data(), kOneMB, off, err, kerrno)) {
            auditLogf(@"FAIL prepareDevice session=%llu phase=last-mb "
                       "offset=%lld errno=%d",
                       (unsigned long long)sessionToken,
                       (long long)off, kerrno);
            reply(NO, [NSString stringWithUTF8String:err.c_str()], kerrno);
            return;
        }
        sess_ptr->bytes_written.fetch_add(kOneMB);
    }

    // fsync so the kernel's notion of the partition table is committed
    // before the imager starts pushing the OS payload through bulk-write.
    if (::fsync(fd) != 0) {
        int e = errno;
        char msg[160] = {0};
        snprintf(msg, sizeof(msg),
                 "prepareDevice: fsync(fd=%d): %s", fd, strerror(e));
        auditLogf(@"FAIL prepareDevice session=%llu phase=fsync errno=%d",
                  (unsigned long long)sessionToken, e);
        reply(NO, [NSString stringWithUTF8String:msg], e);
        return;
    }

    auditLogf(@"OK prepareDevice session=%llu zero_first=%d zero_last=%d",
              (unsigned long long)sessionToken,
              (int)zeroFirstMb, (int)zeroLastMb);
    reply(YES, nil, 0);
}

- (void)closeSession:(uint64_t)sessionToken
                reply:(void (^)(BOOL, NSDictionary* _Nullable))reply {
    int fd = -1;
    int shm_fd = -1;
    void* shm_base = nullptr;
    std::uint64_t shm_size = 0;
    std::string device;
    std::uint64_t bytes = 0;
    pid_t pid = 0;
    dispatch_queue_t bulk_queue = nullptr;
    std::unique_ptr<OpenSession> drained;
    {
        std::lock_guard<std::mutex> lk(sessionMutex());
        auto it = sessionTable().find(sessionToken);
        if (it == sessionTable().end() || !it->second) {
            reply(NO, nil);
            return;
        }
        // Move the session OUT of the global table so subsequent calls
        // (including in-flight bulk writes finishing up) get a clean
        // "unknown session" rather than seeing partially-torn-down
        // state. Keep the unique_ptr alive locally until the drain
        // completes - that's what keeps shm_base / fd valid for the
        // in-flight writes still to drain.
        drained = std::move(it->second);
        sessionTable().erase(it);
        fd = drained->fd;
        shm_fd = drained->shm_fd;
        shm_base = drained->shm_base;
        shm_size = drained->shm_size;
        device = drained->device_path;
        bytes = drained->bytes_written.load();
        pid = drained->client_pid;
        bulk_queue = drained->bulk_queue;
    }
    // Drop sessionMutex BEFORE draining - in-flight writes may still
    // try to look up the session via the mutex, and we'd deadlock if
    // we held it through the barrier wait. They'll find it absent and
    // return cleanly, but we still need to wait for any pwrite already
    // executing to finish before we munmap and close the fd.
    if (bulk_queue) {
        dispatch_barrier_sync(bulk_queue, ^{ /* drain in-flight writes */ });
    }
    if (shm_base) munmap(shm_base, shm_size);
    if (shm_fd >= 0) ::close(shm_fd);
    if (fd >= 0) ::close(fd);

    // §7a SessionStats. Build the payload AFTER the dispatch barrier
    // above so per-write counters are quiescent.
    const auto now = std::chrono::steady_clock::now();
    const std::uint64_t duration_ms = (std::uint64_t)
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - drained->session_start).count();
    const std::uint32_t wcount = drained->write_count.load();
    const std::uint64_t total_lat = drained->total_write_latency_us.load();
    const std::uint32_t min_lat = wcount == 0 ? 0 : drained->min_write_latency_us.load();
    const std::uint32_t max_lat = drained->max_write_latency_us.load();
    const std::uint32_t avg_lat = wcount == 0 ? 0
                                              : (std::uint32_t)(total_lat / wcount);
    const std::uint64_t fsync_us = drained->total_fsync_us.load();
    const std::uint32_t fsync_n = drained->fsync_count.load();
    const std::uint64_t prep_us = drained->prepare_device_us.load();
    const std::uint64_t hash_us = drained->hash_device_us.load();

    // §7a per-write latency histogram. Edges travel alongside counts so the
    // client doesn't hard-code them.
    NSMutableArray<NSNumber*>* bucketEdges =
        [NSMutableArray arrayWithCapacity:kNumLatencyBuckets];
    NSMutableArray<NSNumber*>* bucketCounts =
        [NSMutableArray arrayWithCapacity:kNumLatencyBuckets];
    for (std::size_t i = 0; i < kNumLatencyBuckets; ++i) {
        [bucketEdges addObject:@((unsigned long long)kLatencyBucketUpperUs[i])];
        [bucketCounts addObject:@((unsigned long long)drained->latency_buckets[i].load())];
    }

    NSDictionary* stats = @{
        @"schemaVersion":     @(1),
        @"bytesWritten":      @((unsigned long long)bytes),
        @"durationMs":        @((unsigned long long)duration_ms),
        @"writeCount":        @((unsigned int)wcount),
        @"minWriteLatencyUs": @((unsigned int)min_lat),
        @"avgWriteLatencyUs": @((unsigned int)avg_lat),
        @"maxWriteLatencyUs": @((unsigned int)max_lat),
        @"totalFsyncUs":      @((unsigned long long)fsync_us),
        @"fsyncCount":        @((unsigned int)fsync_n),
        @"prepareDeviceUs":   @((unsigned long long)prep_us),
        @"hashDeviceUs":      @((unsigned long long)hash_us),
        @"writeLatencyBucketUpperUs": bucketEdges,
        @"writeLatencyBucketCounts":  bucketCounts,
    };

    auditLogf(@"OK closeSession session=%llu pid=%d device=%s "
               @"bytes_written=%llu writes=%u avg_lat_us=%u max_lat_us=%u "
               @"fsync_us=%llu prepare_us=%llu hash_us=%llu",
              (unsigned long long)sessionToken, pid, device.c_str(),
              (unsigned long long)bytes,
              wcount, avg_lat, max_lat,
              (unsigned long long)fsync_us,
              (unsigned long long)prep_us,
              (unsigned long long)hash_us);
    reply(YES, stats);
}

@end

extern "C" int RpiImagerWriterServiceMain(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    NSLog(@"rpi-imager-writer: main() starting; service=%@", RpiImagerWriterServiceName);
    os_log_t log = os_log_create("com.raspberrypi.rpi-imager.writer", "main");
    os_log(log, "rpi-imager-writer starting; service name=%{public}@",
           RpiImagerWriterServiceName);

    RpiImagerWriterService* service = [[RpiImagerWriterService alloc] init];
    NSXPCListener* listener =
        [[NSXPCListener alloc] initWithMachServiceName:RpiImagerWriterServiceName];
    listener.delegate = service;
    [listener resume];

    // Arm the idle-exit timer at startup so a speculatively-launched
    // helper (or one whose only client crashes before connecting)
    // doesn't stick around. The accept path cancels this timer as
    // soon as a real client connects.
    dispatch_async(idleQueue(), ^{ scheduleIdleExitOnIdleQueue(); });

    // launchd-managed Mach services are expected to run their main run loop
    // until terminated. NSXPC dispatches incoming connections onto secondary
    // queues, so the main thread just parks on the run loop.
    [[NSRunLoop currentRunLoop] run];
    return 0;
}
