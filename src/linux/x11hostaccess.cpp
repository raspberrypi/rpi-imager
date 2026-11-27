/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * X11 host access for the elevated instance: the in-process equivalent of
 * `xhost +SI:localuser:<user>`.
 *
 * Only three pieces of the X11 protocol are needed to add a user to the X
 * server's host access list: the connection setup, ChangeHosts and ListHosts.
 * Doing them here avoids a runtime dependency on xhost(1), which is an
 * optional package on several distributions.
 */

#include "x11hostaccess.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>

namespace X11HostAccess {

namespace {

constexpr char kByteOrder = 'l';                       /* little-endian */
constexpr unsigned short kProtocolMajor = 11;
constexpr unsigned char kRequestChangeHosts = 109;     /* X_ChangeHosts */
constexpr unsigned char kRequestListHosts = 110;       /* X_ListHosts */
constexpr unsigned char kHostInsert = 0;
constexpr unsigned char kFamilyServerInterpreted = 5;
constexpr unsigned short kAuthFamilyLocal = 256;         /* Xauth family of local connections */
constexpr unsigned short kAuthFamilyWild = 65535;        /* FamilyWild */
constexpr char kCookieName[] = "MIT-MAGIC-COOKIE-1";
constexpr char kLocalUserType[] = "localuser";         /* server-interpreted address type */

constexpr size_t kMaxCookieLength = 64;
constexpr size_t kMaxAuthoritySize = 256 * 1024;
constexpr size_t kMaxTargetUserLength = 32;

const unsigned char kListHostsRequest[4] = {kRequestListHosts, 0, 1, 0};

/* Buffers for what the X server sends.  Static rather than on the stack: the
 * child reuses whatever stack the forking thread had, and may not allocate. */
unsigned char replyBody[256 * 1024];

unsigned readU16(const unsigned char* p) {
    return static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8);
}

unsigned readU32(const unsigned char* p) {
    return readU16(p) | (readU16(p + 2) << 16);
}

/* Xauthority files are big-endian, the connection is not. */
unsigned readNetworkU16(const unsigned char* p) {
    return (static_cast<unsigned>(p[0]) << 8) | static_cast<unsigned>(p[1]);
}

void writeU16(unsigned char* p, unsigned value) {
    p[0] = static_cast<unsigned char>(value);
    p[1] = static_cast<unsigned char>(value >> 8);
}

size_t pad4(size_t n) {
    return (n + 3) & ~size_t(3);
}

/* Accepts the local forms of "[protocol/][host]:display[.screen]": an empty
 * host, "unix" or "unix/".  Anything else names a remote display, and host
 * access lists do not apply to network connections. */
bool parseLocalDisplay(const char* display, int& displayNumber) {
    const char* colon = strrchr(display, ':');
    if (!colon || colon[1] == '\0') {
        return false;
    }

    const char* host = display;
    if (const char* slash = strchr(display, '/'); slash && slash < colon) {
        if (slash - display != 4 || memcmp(display, "unix", 4) != 0) {
            return false;
        }
        host = slash + 1;
    }
    const size_t hostLength = static_cast<size_t>(colon - host);
    if (hostLength != 0 && !(hostLength == 4 && memcmp(host, "unix", 4) == 0)) {
        return false;
    }

    const char* p = colon + 1;
    if (*p < '0' || *p > '9') {
        return false;
    }
    int number = 0;
    for (int digits = 0; *p >= '0' && *p <= '9'; ++p) {
        if (++digits > 4) {
            return false;
        }
        number = number * 10 + (*p - '0');
    }
    if (*p != '\0' && *p != '.') {
        return false;
    }
    displayNumber = number;
    return true;
}

/* Finds the display's MIT-MAGIC-COOKIE-1 record the way libXau does for
 * Xlib: the query is for family FamilyLocal with the host name as address,
 * and a record's empty display number matches every display. */
bool findCookie(const unsigned char* data, size_t size, const char* hostname, const char* displayNumber,
                unsigned char* cookie, size_t& cookieLength) {
    const size_t hostnameLength = strlen(hostname);
    const size_t queryNumberLength = strlen(displayNumber);
    const unsigned char* p = data;
    const unsigned char* const end = data + size;

    /* Reads one length-prefixed field; false when the file is truncated. */
    const auto field = [&](const unsigned char*& value, size_t& length) -> bool {
        if (static_cast<size_t>(end - p) < 2) {
            return false;
        }
        length = readNetworkU16(p);
        p += 2;
        if (static_cast<size_t>(end - p) < length) {
            return false;
        }
        value = p;
        p += length;
        return true;
    };

    while (p != end) {
        if (static_cast<size_t>(end - p) < 2) {
            break;                                      /* corrupt tail */
        }
        const unsigned family = readNetworkU16(p);
        p += 2;
        const unsigned char *address, *number, *name, *value;
        size_t addressLength, numberLength, nameLength, valueLength;
        if (!field(address, addressLength) || !field(number, numberLength) ||
            !field(name, nameLength) || !field(value, valueLength)) {
            break;
        }
        /* The query is for a local connection: its family and address have to
         * match, unless the record is a wildcard. */
        const bool familyMatches = family == kAuthFamilyWild || family == kAuthFamilyLocal;
        const bool addressMatches = family == kAuthFamilyWild ||
                                    (addressLength == hostnameLength &&
                                     memcmp(address, hostname, hostnameLength) == 0);
        const bool numberMatches = numberLength == 0 ||
                                   (numberLength == queryNumberLength &&
                                    memcmp(number, displayNumber, queryNumberLength) == 0);
        const bool nameMatches = nameLength == strlen(kCookieName) &&
                                 memcmp(name, kCookieName, nameLength) == 0;
        if (familyMatches && addressMatches && numberMatches && nameMatches &&
            valueLength > 0 && valueLength <= kMaxCookieLength) {
            memcpy(cookie, value, valueLength);
            cookieLength = valueLength;
            return true;
        }
    }
    return false;
}

/* Only the unprivileged child opens this path. Skip non-regular files (in
 * particular FIFOs) and use the same bounded scratch space as the X replies. */
size_t readAuthority(const char* path) {
    if (!*path) {
        return 0;
    }
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        return 0;
    }
    size_t length = 0;
    struct stat st = {};
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        while (length < kMaxAuthoritySize) {
            const ssize_t n = read(fd, replyBody + length, kMaxAuthoritySize - length);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                break;
            }
            length += static_cast<size_t>(n);
        }
    }
    close(fd);
    return length;
}

/* xConnClientPrefix followed by the authorization strings. Without a cookie
 * the connection relies on the host access list, like Xlib does. */
size_t buildSetup(const GrantPlan& plan, unsigned char* setup) {
    unsigned char cookie[kMaxCookieLength];
    size_t cookieLength = 0;
    const size_t authoritySize = readAuthority(plan.authorityPath);
    if (authoritySize > 0 &&
        !findCookie(replyBody, authoritySize, plan.hostname, plan.displayNumber, cookie, cookieLength)) {
        /* Some display managers write the address as "hostname/unix". */
        char localName[HOST_NAME_MAX + sizeof("/unix")];
        const size_t hostLength = strlen(plan.hostname);
        memcpy(localName, plan.hostname, hostLength);
        memcpy(localName + hostLength, "/unix", sizeof("/unix"));
        findCookie(replyBody, authoritySize, localName, plan.displayNumber, cookie, cookieLength);
    }
    const size_t nameLength = cookieLength > 0 ? strlen(kCookieName) : 0;
    unsigned char* p = setup;
    *p++ = kByteOrder;
    *p++ = 0;
    writeU16(p, kProtocolMajor);
    p += 2;
    writeU16(p, 0);
    p += 2;
    writeU16(p, static_cast<unsigned>(nameLength));
    p += 2;
    writeU16(p, static_cast<unsigned>(cookieLength));
    p += 2;
    writeU16(p, 0);
    p += 2;
    if (cookieLength > 0) {
        memcpy(p, kCookieName, nameLength);
        p += pad4(nameLength);
        memcpy(p, cookie, cookieLength);
        p += pad4(cookieLength);
    }
    return static_cast<size_t>(p - setup);
}

/* xChangeHostsReq inserting "localuser\0<user>\0" (FamilyServerInterpreted). */
void buildChangeHosts(GrantPlan& plan, const char* targetUser) {
    const size_t userLength = strlen(targetUser);
    const size_t addressLength = sizeof(kLocalUserType) + userLength + 1;
    unsigned char* p = plan.changeHosts;
    *p++ = kRequestChangeHosts;
    *p++ = kHostInsert;
    writeU16(p, static_cast<unsigned>((8 + pad4(addressLength)) / 4));
    p += 2;
    *p++ = kFamilyServerInterpreted;
    *p++ = 0;
    writeU16(p, static_cast<unsigned>(addressLength));
    p += 2;
    memcpy(p, kLocalUserType, sizeof(kLocalUserType));      /* includes the separator */
    p += sizeof(kLocalUserType);
    memcpy(p, targetUser, userLength + 1);                   /* includes the terminator */
    p += userLength + 1;
    memset(p, 0, pad4(addressLength) - addressLength);

    plan.changeHostsLength = 8 + pad4(addressLength);
    memcpy(plan.target, plan.changeHosts + 8, addressLength);
    plan.targetLength = addressLength;
}

int connectToDisplay(const char* path) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    const size_t prefix = path[0] == '\0' ? 1 : 0;          /* leading NUL: abstract socket */
    const size_t length = strlen(path + prefix);
    if (length >= sizeof(address.sun_path) - prefix) {
        close(fd);
        return -1;
    }
    memcpy(address.sun_path, path, prefix + length);
    const socklen_t addressLength =
        static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + prefix + length);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), addressLength) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

enum class ReadResult { Ok, EndOfFile, TimedOut, Failed };

ReadResult readExactly(int fd, unsigned char* buffer, size_t length) {
    while (length > 0) {
        const ssize_t n = recv(fd, buffer, length, 0);
        if (n > 0) {
            buffer += n;
            length -= static_cast<size_t>(n);
        } else if (n == 0) {
            return ReadResult::EndOfFile;
        } else if (errno != EINTR) {
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? ReadResult::TimedOut : ReadResult::Failed;
        }
    }
    return ReadResult::Ok;
}

/* For the pipe back to the parent. */
bool writeExactly(int fd, const unsigned char* buffer, size_t length) {
    while (length > 0) {
        const ssize_t n = write(fd, buffer, length);
        if (n > 0) {
            buffer += n;
            length -= static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

/* For the connection to the X server. */
bool sendExactly(int fd, const unsigned char* buffer, size_t length) {
    while (length > 0) {
        const ssize_t n = send(fd, buffer, length, MSG_NOSIGNAL);
        if (n > 0) {
            buffer += n;
            length -= static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

/* Reads a reply body into replyBody, discarding anything beyond its size. */
ReadResult readBody(int fd, size_t length, size_t& kept) {
    kept = std::min(length, sizeof(replyBody));
    const ReadResult result = readExactly(fd, replyBody, kept);
    if (result != ReadResult::Ok) {
        return result;
    }
    unsigned char discard[4096];
    for (length -= kept; length > 0;) {
        const size_t chunk = std::min(length, sizeof(discard));
        const ReadResult skipped = readExactly(fd, discard, chunk);
        if (skipped != ReadResult::Ok) {
            return skipped;
        }
        length -= chunk;
    }
    return ReadResult::Ok;
}

ExitCode exitCodeFor(ReadResult result) {
    switch (result) {
        case ReadResult::EndOfFile: return ProtocolError;
        case ReadResult::TimedOut: return Timeout;
        default: return IoError;
    }
}

/* Single-line report for the parent.  Memory operations and write() only:
 * the child may not use stdio or allocate. */
struct Report {
    char text[kMaxReason];
    size_t length = 0;

    void append(const char* s, size_t n) {
        if (n >= kMaxReason - length) {
            n = kMaxReason - length - 1;
        }
        memcpy(text + length, s, n);
        length += n;
    }
    void add(const char* s) {
        append(s, strlen(s));
    }
    void add(unsigned number) {
        char digits[10];
        size_t n = 0;
        do {
            digits[n++] = static_cast<char>('0' + number % 10);
            number /= 10;
        } while (number != 0);
        while (n > 0) {
            append(digits + --n, 1);
        }
    }
};

/* Writes the NUL-terminated report, if wanted, and exits. */
[[noreturn]] void finish(Report& report, int reportFd, ExitCode code) {
    report.text[report.length] = '\0';
    if (reportFd >= 0) {
        writeExactly(reportFd, reinterpret_cast<const unsigned char*>(report.text), report.length + 1);
    }
    _exit(code);
}

void describeReadFailure(Report& report, ReadResult result) {
    switch (result) {
        case ReadResult::EndOfFile: report.add("the X server closed the connection"); break;
        case ReadResult::TimedOut: report.add("timed out waiting for the X server"); break;
        default: report.add("I/O error while talking to the X server"); break;
    }
}

/* One xHostEntry in human-readable form: "si:<type>:<value>" for the
 * server-interpreted family, otherwise just the family number. */
void addHost(Report& report, const unsigned char* entry, size_t length) {
    const unsigned char* address = entry + 4;
    if (entry[0] == kFamilyServerInterpreted) {
        if (const void* separator = memchr(address, '\0', length)) {
            const size_t typeLength = static_cast<size_t>(static_cast<const unsigned char*>(separator) - address) + 1;
            report.add("si:");
            report.append(reinterpret_cast<const char*>(address), typeLength - 1);
            report.add(":");
            size_t valueLength = length - typeLength;
            if (valueLength > 0 && address[length - 1] == '\0') {
                --valueLength;
            }
            report.append(reinterpret_cast<const char*>(address) + typeLength, valueLength);
            return;
        }
    }
    report.add("family ");
    report.add(static_cast<unsigned>(entry[0]));
}

/* The user asked for, as "si:localuser:<user>". */
void addTarget(Report& report, const GrantPlan& plan) {
    report.add("si:");
    report.add(kLocalUserType);
    report.add(":");
    report.append(plan.target + sizeof(kLocalUserType), plan.targetLength - sizeof(kLocalUserType) - 1);
}

}   // namespace

bool prepareGrant(const char* display, const char* xauthority, const char* homeDirectory,
                  const char* targetUser, GrantPlan& plan, char* reason, size_t reasonCapacity) {
    plan = GrantPlan{};
    reason[0] = '\0';
    const auto decline = [&](const char* why) {
        snprintf(reason, reasonCapacity, "%s (DISPLAY=%s)", why, display ? display : "<unset>");
        return false;
    };

    if (!display) {
        return decline("$DISPLAY is not set, no X11 display to grant access to");
    }
    int displayNumber = 0;
    if (!parseLocalDisplay(display, displayNumber)) {
        return decline("$DISPLAY is not a local display, host access lists do not apply");
    }
    if (!targetUser || *targetUser == '\0' || strlen(targetUser) > kMaxTargetUserLength) {
        return decline("no valid user to grant access to");
    }

    /* The filesystem socket first: every X server provides it, and the
     * abstract socket is a Linux extra. */
    snprintf(plan.socketPath[0], sizeof(plan.socketPath[0]), "/tmp/.X11-unix/X%d", displayNumber);
    snprintf(plan.socketPath[1] + 1, sizeof(plan.socketPath[1]) - 1, "/tmp/.X11-unix/X%d", displayNumber);
    plan.socketCount = kMaxSocketPaths;

    /* Record the path without opening it in the privileged parent. */
    int pathLength = 0;
    if (xauthority && *xauthority) {
        pathLength = snprintf(plan.authorityPath, sizeof(plan.authorityPath), "%s", xauthority);
    } else if (homeDirectory && *homeDirectory) {
        pathLength = snprintf(plan.authorityPath, sizeof(plan.authorityPath), "%s/.Xauthority", homeDirectory);
    }
    if (pathLength < 0 || static_cast<size_t>(pathLength) >= sizeof(plan.authorityPath)) {
        plan.authorityPath[0] = '\0';
    }
    gethostname(plan.hostname, sizeof(plan.hostname) - 1);
    snprintf(plan.displayNumber, sizeof(plan.displayNumber), "%d", displayNumber);
    buildChangeHosts(plan, targetUser);
    return true;
}

[[noreturn]] void runGrant(const GrantPlan& plan, int reportFd) {
    Report report;
    /* Bound the whole helper, including open(), connect() and a server that
     * trickles bytes to keep every individual read alive. */
    struct sigaction action = {};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(SIGALRM, &action, nullptr);
    sigset_t unblock;
    sigemptyset(&unblock);
    sigaddset(&unblock, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &unblock, nullptr);
    alarm(plan.timeoutSeconds * 2);

    /* Paths are stored the abstract way, with a leading NUL. */
    const auto printable = [&plan](int i) {
        return plan.socketPath[i][0] == '\0' ? plan.socketPath[i] + 1 : plan.socketPath[i];
    };

    int fd = -1;
    for (int i = 0; i < plan.socketCount && fd < 0; ++i) {
        fd = connectToDisplay(plan.socketPath[i]);
    }
    if (fd < 0) {
        report.add("could not connect to the X display socket ");
        for (int i = 0; i < plan.socketCount; ++i) {
            report.add(i ? " or " : "");
            report.add(printable(i));
        }
        finish(report, reportFd, CouldNotConnect);
    }

    timeval timeout = {};
    timeout.tv_sec = static_cast<time_t>(plan.timeoutSeconds);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    unsigned char setup[256] = {};
    const size_t setupLength = buildSetup(plan, setup);
    if (!sendExactly(fd, setup, setupLength)) {
        report.add("failed to send the connection setup to the X server");
        finish(report, reportFd, IoError);
    }

    /* xConnSetupPrefix: success, reasonLength, majorVersion, minorVersion (2
     * bytes each), then additionalLength in 4-byte units at offset 6. The
     * additionalLength*4 bytes of setup information follow and have to be
     * consumed before the first reply can be read. */
    unsigned char prefix[8];
    ReadResult result = readExactly(fd, prefix, sizeof(prefix));
    size_t bodyLength = 0;
    if (result == ReadResult::Ok) {
        result = readBody(fd, static_cast<size_t>(readU16(prefix + 6)) * 4u, bodyLength);
    }
    if (result != ReadResult::Ok) {
        describeReadFailure(report, result);
        finish(report, reportFd, exitCodeFor(result));
    }
    if (prefix[0] != 1) {
        report.add(prefix[0] == 0 ? "the X server rejected the connection: "
                                  : "the X server requested an unsupported authentication exchange: ");
        const size_t reasonLength = std::min<size_t>(prefix[1], bodyLength);
        report.append(reinterpret_cast<const char*>(replyBody), reasonLength);
        if (reasonLength == 0) {
            report.add("(no reason given)");
        }
        finish(report, reportFd, SetupRejected);
    }

    if (!plan.listOnly && !sendExactly(fd, plan.changeHosts, plan.changeHostsLength)) {
        report.add("failed to send the ChangeHosts request to the X server");
        finish(report, reportFd, IoError);
    }
    if (!sendExactly(fd, kListHostsRequest, sizeof(kListHostsRequest))) {
        report.add("failed to send the ListHosts request to the X server");
        finish(report, reportFd, IoError);
    }

    for (;;) {
        unsigned char header[32];
        result = readExactly(fd, header, sizeof(header));
        if (result == ReadResult::Ok && header[0] == 1) {
            result = readBody(fd, static_cast<size_t>(readU32(header + 4)) * 4u, bodyLength);
        }
        if (result != ReadResult::Ok) {
            report.add("no reply to ListHosts: ");
            describeReadFailure(report, result);
            finish(report, reportFd, exitCodeFor(result));
        }

        if (header[0] == 0) {                          /* XError: type, code, ..., minor, major */
            if (header[10] == kRequestChangeHosts) {
                report.add("the X server refused the ChangeHosts request (error ");
                report.add(static_cast<unsigned>(header[1]));
                report.add(")");
                finish(report, reportFd, ChangeHostsRefused);
            }
            if (header[10] == kRequestListHosts) {
                report.add("the X server refused to list its host access list (error ");
                report.add(static_cast<unsigned>(header[1]));
                report.add(")");
                finish(report, reportFd, ListHostsRefused);
            }
            continue;                                  /* not about our requests */
        }
        if (header[0] != 1) {
            continue;                                  /* no events are expected here */
        }

        /* The only request of ours that gets a reply is ListHosts:
         * xListHostsReply has "enabled" in header[1], nHosts at offset 8,
         * followed by xHostEntry records: family, pad, length, address. */
        const bool accessControlEnabled = header[1] != 0;
        const bool truncated = static_cast<size_t>(readU32(header + 4)) * 4u > sizeof(replyBody);
        const unsigned hostCount = readU16(header + 8);
        bool found = false;
        Report list;
        size_t offset = 0;
        for (unsigned i = 0; i < hostCount; ++i) {
            if (bodyLength - offset < 4) {
                break;
            }
            const unsigned char* entry = replyBody + offset;
            const size_t length = readU16(entry + 2);
            if (length > bodyLength - offset - 4) {
                break;
            }
            if (entry[0] == kFamilyServerInterpreted && length == plan.targetLength &&
                memcmp(entry + 4, plan.target, length) == 0) {
                found = true;
            }
            list.add(offset ? ", " : "");
            addHost(list, entry, length);
            offset = std::min(bodyLength, offset + 4 + pad4(length));
        }

        addTarget(report, plan);
        report.add(accessControlEnabled ? (found ? " is on the host access list (" : " is not on the host access list (")
                                        : " may connect, access control is disabled (host access list: ");
        if (offset == 0) {
            list.add("empty");
        }
        report.append(list.text, list.length);
        report.add(")");
        if (truncated && !(found || !accessControlEnabled)) {
            report.add("; the list was too large to inspect in full");
        }
        finish(report, reportFd, found || !accessControlEnabled ? Ok : NotGranted);
    }
}

bool describeOutcome(int waitStatus, const char* report, char* reason, size_t reasonCapacity) {
    reason[0] = '\0';
    if (WIFEXITED(waitStatus)) {
        const int code = WEXITSTATUS(waitStatus);
        if (code == Ok) {
            snprintf(reason, reasonCapacity, "%s", report);
            return true;
        }
        const char* what = "unexpected failure";
        switch (code) {
            case PrivilegeDropFailed: what = "could not drop back to the original user"; break;
            case CouldNotConnect: what = "the original user cannot connect to the X display"; break;
            case SetupRejected: what = "the X server refused the connection"; break;
            case IoError: what = "I/O error while talking to the X server"; break;
            case Timeout: what = "timeout while talking to the X server"; break;
            case ChangeHostsRefused: what = "the X server refused to change its host access list"; break;
            case ListHostsRefused: what = "the X server refused to report its host access list"; break;
            case ProtocolError: what = "unexpected reply from the X server"; break;
            case NotGranted: what = "the user is not on the X server's host access list"; break;
            default: break;
        }
        snprintf(reason, reasonCapacity, "%s", report && *report ? report : what);
        return false;
    }
    if (WIFSIGNALED(waitStatus)) {
        snprintf(reason, reasonCapacity, "the grant helper was killed by signal %d", WTERMSIG(waitStatus));
    } else {
        snprintf(reason, reasonCapacity, "the grant helper did not exit normally");
    }
    return false;
}

}   // namespace X11HostAccess
