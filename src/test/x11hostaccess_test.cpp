/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests for the X11 host access exchange in linux/x11hostaccess.cpp.
 *
 * The "X server" is a fake that speaks just enough of the protocol for that
 * exchange - connection setup, ChangeHosts, ListHosts - so the tests run
 * everywhere, including CI, without a display.
 */

#include <catch2/catch_test_macros.hpp>

#include "../linux/x11hostaccess.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using X11HostAccess::ExitCode;
using X11HostAccess::GrantPlan;

constexpr unsigned char kChangeHosts = 109;
constexpr unsigned char kListHosts = 110;
constexpr unsigned char kFamilyServerInterpreted = 5;
constexpr char kCookieName[] = "MIT-MAGIC-COOKIE-1";

/* "si:localuser:<user>" as the server-interpreted address: "localuser\0<user>\0" */
std::string localUser(const std::string& user) {
    return std::string("localuser\0", 10) + user + std::string("\0", 1);
}

size_t pad4(size_t n) {
    return (n + 3) & ~size_t(3);
}

bool readExactly(int fd, unsigned char* buffer, size_t length) {
    while (length > 0) {
        const ssize_t n = read(fd, buffer, length);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        buffer += n;
        length -= static_cast<size_t>(n);
    }
    return true;
}

void writeAll(int fd, const std::vector<unsigned char>& bytes) {
    size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t n = send(fd, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return;                 // the client gave up: fine for a fake
        }
        offset += static_cast<size_t>(n);
    }
}

void putU16(std::vector<unsigned char>& bytes, size_t at, unsigned value) {
    bytes[at] = static_cast<unsigned char>(value);
    bytes[at + 1] = static_cast<unsigned char>(value >> 8);
}

/* One record of an Xauthority file: five big-endian length-prefixed fields. */
void appendAuthorityRecord(std::vector<unsigned char>& file, unsigned short family, const std::string& address,
                           const std::string& number, const std::string& name, const std::string& value) {
    const auto field = [&](unsigned short length, const std::string& value) {
        file.push_back(static_cast<unsigned char>(length >> 8));
        file.push_back(static_cast<unsigned char>(length));
        file.insert(file.end(), value.begin(), value.end());
    };
    field(family, "");
    field(static_cast<unsigned short>(address.size()), address);
    field(static_cast<unsigned short>(number.size()), number);
    field(static_cast<unsigned short>(name.size()), name);
    field(static_cast<unsigned short>(value.size()), value);
}

/* A private directory under /tmp that removes its files when done. */
class TempDirectory {
public:
    TempDirectory() {
        char path[] = "/tmp/x11hostaccess-auth-XXXXXX";
        REQUIRE(mkdtemp(path) != nullptr);
        base_ = path;
    }

    ~TempDirectory() {
        for (const std::string& name : names_) {
            unlink((base_ + "/" + name).c_str());
        }
        rmdir(base_.c_str());
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::string& base() const {
        return base_;
    }

    /* Path of a file about to be created in the directory. */
    std::string path(const char* name) {
        names_.emplace_back(name);
        return base_ + "/" + name;
    }

private:
    std::string base_;
    std::vector<std::string> names_;
};

class FakeXServer {
public:
    enum class Behaviour {
        InsertAndList,          // honour ChangeHosts and list the entry back
        ListWithoutUser,        // swallow ChangeHosts, list the initial entries
        AccessControlDisabled,  // swallow ChangeHosts, report access control disabled
        RefuseChangeHosts,      // XError (BadValue) in reply to ChangeHosts
        RefuseConnection,       // fail the connection setup with a reason
        CloseAfterSetup,        // stop answering once the setup reply was sent
        Stall,                  // accept the requests, never answer
        Trickle,                // answer one byte at a time, never finishing
    };

    struct Observed {
        std::string authorizationName;
        std::string authorizationData;
        unsigned char insertedFamily = 0;
        std::string insertedAddress;
        bool listHostsRequested = false;
    };

    explicit FakeXServer(std::vector<std::string> initialHosts = {}) : hosts_(std::move(initialHosts)) {
        char directory[] = "/tmp/x11hostaccess-XXXXXX";
        REQUIRE(mkdtemp(directory) != nullptr);
        directory_ = directory;
        socketPath_ = directory_ + "/X0";

        listenFd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        REQUIRE(listenFd_ >= 0);
        sockaddr_un address = {};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, socketPath_.c_str(), sizeof(address.sun_path) - 1);
        REQUIRE(bind(listenFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        REQUIRE(listen(listenFd_, 1) == 0);
    }

    ~FakeXServer() {
        if (thread_.joinable()) {
            shutdown(listenFd_, SHUT_RDWR);
            thread_.join();
        }
        close(listenFd_);
        unlink(socketPath_.c_str());
        rmdir(directory_.c_str());
    }

    FakeXServer(const FakeXServer&) = delete;
    FakeXServer& operator=(const FakeXServer&) = delete;

    const std::string& socketPath() const {
        return socketPath_;
    }

    void start(Behaviour behaviour) {
        thread_ = std::thread([this, behaviour] { serve(behaviour); });
    }

    /* Waits for the server side of the exchange to end. */
    bool wait(unsigned seconds = 10) {
        std::unique_lock<std::mutex> lock(mutex_);
        return done_.wait_for(lock, std::chrono::seconds(seconds), [this] { return finished_; });
    }

    Observed observed() {
        std::lock_guard<std::mutex> lock(mutex_);
        return observed_;
    }

private:
    void serve(Behaviour behaviour) {
        const int fd = accept(listenFd_, nullptr, nullptr);
        if (fd < 0) {
            return;
        }
        const timeval timeout = {5, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        Observed observed;
        unsigned char prefix[12];
        if (readExactly(fd, prefix, sizeof(prefix))) {
            const size_t nameLength = prefix[6] | (prefix[7] << 8);
            const size_t dataLength = prefix[8] | (prefix[9] << 8);
            std::vector<unsigned char> authorization(pad4(nameLength) + pad4(dataLength));
            if (readExactly(fd, authorization.data(), authorization.size())) {
                observed.authorizationName.assign(authorization.begin(), authorization.begin() + nameLength);
                observed.authorizationData.assign(authorization.begin() + pad4(nameLength),
                                                  authorization.begin() + pad4(nameLength) + dataLength);
                serveRequests(fd, behaviour, observed);
            }
        }
        close(fd);
        std::lock_guard<std::mutex> lock(mutex_);
        observed_ = observed;
        finished_ = true;
        done_.notify_all();
    }

    void serveRequests(int fd, Behaviour behaviour, Observed& observed) {
        if (behaviour == Behaviour::RefuseConnection) {
            const std::string reason = "No protocol specified";
            /* xConnSetupPrefix: success, reasonLength, major, minor, additionalLength (offset 6) */
            std::vector<unsigned char> reply = {0, static_cast<unsigned char>(reason.size()), 11, 0, 0, 0,
                                                static_cast<unsigned char>(pad4(reason.size()) / 4), 0};
            reply.insert(reply.end(), reason.begin(), reason.end());
            reply.resize(8 + pad4(reason.size()));
            writeAll(fd, reply);
            return;
        }
        /* xConnSetupPrefix with a non-empty setup body (160 bytes), like a real
         * server: the client has to consume it before it can read a reply. */
        std::vector<unsigned char> setup = {1, 0, 11, 0, 0, 0, 40, 0};
        setup.resize(8 + 160, 0x5a);
        writeAll(fd, setup);
        if (behaviour == Behaviour::CloseAfterSetup) {
            /* Half-close, like a server giving up on this client: the client
             * reads end-of-file while its own writes still succeed. */
            shutdown(fd, SHUT_WR);
            unsigned char sink[4096];
            while (read(fd, sink, sizeof(sink)) > 0) {
            }
            return;
        }

        for (;;) {
            unsigned char header[4];
            if (!readExactly(fd, header, sizeof(header))) {
                return;
            }
            const size_t length = (header[2] | (header[3] << 8)) * 4;
            std::vector<unsigned char> rest(length > 4 ? length - 4 : 0);
            if (!rest.empty() && !readExactly(fd, rest.data(), rest.size())) {
                return;
            }

            if (header[0] == kChangeHosts) {
                /* xChangeHostsReq: mode in header[1]; then family, pad, hostLength, address */
                observed.insertedFamily = rest[0];
                const size_t hostLength = rest[2] | (rest[3] << 8);
                observed.insertedAddress.assign(rest.begin() + 4, rest.begin() + 4 + hostLength);
                if (behaviour == Behaviour::RefuseChangeHosts) {
                    std::vector<unsigned char> error(32);
                    error[1] = 2;               /* BadValue */
                    error[10] = kChangeHosts;
                    writeAll(fd, error);
                }
            } else if (header[0] == kListHosts) {
                observed.listHostsRequested = true;
                if (behaviour == Behaviour::Stall) {
                    unsigned char sink[4096];
                    while (read(fd, sink, sizeof(sink)) > 0) {
                    }
                    return;
                }
                if (behaviour == Behaviour::Trickle) {
                    /* Each byte arrives well within the client's per-read
                     * timeout, so only an overall deadline can end this. */
                    static const unsigned char reply[32] = {1, 1};
                    for (size_t sent = 0; sent < sizeof(reply); ++sent) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(300));
                        if (send(fd, reply + sent, 1, MSG_NOSIGNAL) != 1) {
                            break;
                        }
                    }
                    return;
                }
                if (behaviour == Behaviour::InsertAndList && !observed.insertedAddress.empty()) {
                    hosts_.push_back(observed.insertedAddress);
                }
                writeListHostsReply(fd, behaviour == Behaviour::AccessControlDisabled ? 0 : 1, hosts_);
                return;
            }
        }
    }

    static void writeListHostsReply(int fd, unsigned char enabled, const std::vector<std::string>& hosts) {
        std::vector<unsigned char> body;
        for (const std::string& host : hosts) {
            body.push_back(kFamilyServerInterpreted);
            body.push_back(0);
            body.push_back(static_cast<unsigned char>(host.size()));
            body.push_back(0);
            body.insert(body.end(), host.begin(), host.end());
            body.resize(pad4(body.size()));
        }
        std::vector<unsigned char> reply(32);
        reply[0] = 1;
        reply[1] = enabled;
        putU16(reply, 4, static_cast<unsigned>(body.size() / 4));
        putU16(reply, 6, 0);
        putU16(reply, 8, static_cast<unsigned>(hosts.size()));
        putU16(reply, 10, 0);
        reply.insert(reply.end(), body.begin(), body.end());
        writeAll(fd, reply);
    }

    std::string directory_;
    std::string socketPath_;
    int listenFd_ = -1;
    std::vector<std::string> hosts_;       // server thread only, once started
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable done_;
    Observed observed_;                    // guarded by mutex_
    bool finished_ = false;                // guarded by mutex_
};

struct Outcome {
    int code;
    std::string report;
};

/* Runs runGrant() the way the imager does: in a forked child that reports
 * through a pipe. */
Outcome runInChild(GrantPlan plan) {
    int fds[2];
    REQUIRE(pipe(fds) == 0);
    const pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        // Child of a multi-threaded process: async-signal-safe code only
        close(fds[0]);
        X11HostAccess::runGrant(plan, fds[1]);
        _exit(99);
    }
    close(fds[1]);

    std::string report;
    char buffer[X11HostAccess::kMaxReason];
    ssize_t n;
    while ((n = read(fds[0], buffer, sizeof(buffer))) > 0) {
        report.append(buffer, static_cast<size_t>(n));
    }
    close(fds[0]);
    report.erase(std::remove(report.begin(), report.end(), '\0'), report.end());

    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status), report};
}

/* A plan for ":0" redirected to the fake server's socket. */
GrantPlan planFor(const FakeXServer& server) {
    GrantPlan plan;
    char reason[X11HostAccess::kMaxReason];
    REQUIRE(X11HostAccess::prepareGrant(":0", nullptr, nullptr, "root", plan, reason, sizeof(reason)));
    plan.socketCount = 1;
    std::memcpy(plan.socketPath[0], server.socketPath().c_str(), server.socketPath().size() + 1);
    plan.timeoutSeconds = 5;
    return plan;
}

}   // namespace

TEST_CASE("DISPLAY names resolve to local sockets or are declined", "[x11hostaccess]") {
    char reason[X11HostAccess::kMaxReason];
    GrantPlan plan;

    SECTION("a local display yields the filesystem socket and the abstract one") {
        REQUIRE(X11HostAccess::prepareGrant(":0", nullptr, nullptr, "root", plan, reason, sizeof(reason)));
        CHECK(plan.socketCount == 2);
        CHECK(std::string(plan.socketPath[0]) == "/tmp/.X11-unix/X0");
        CHECK(plan.socketPath[1][0] == '\0');
        CHECK(std::string(plan.socketPath[1] + 1) == "/tmp/.X11-unix/X0");
    }

    SECTION("display numbers, screens and the unix host are accepted") {
        REQUIRE(X11HostAccess::prepareGrant("unix:1", nullptr, nullptr, "root", plan, reason, sizeof(reason)));
        CHECK(std::string(plan.socketPath[0]) == "/tmp/.X11-unix/X1");
        REQUIRE(X11HostAccess::prepareGrant("unix/:12.0", nullptr, nullptr, "root", plan, reason, sizeof(reason)));
        CHECK(std::string(plan.socketPath[0]) == "/tmp/.X11-unix/X12");
    }

    SECTION("remote, malformed or missing displays are declined with a reason") {
        for (const char* display : {"localhost:0", "host:0", "tcp/host:0", ":", ":x", "unix:abc", "", ":0x"}) {
            INFO(display);
            CHECK_FALSE(X11HostAccess::prepareGrant(display, nullptr, nullptr, "root", plan, reason, sizeof(reason)));
            CHECK(std::string(reason).find("DISPLAY=") != std::string::npos);
        }
        CHECK_FALSE(X11HostAccess::prepareGrant(nullptr, nullptr, nullptr, "root", plan, reason, sizeof(reason)));
        CHECK(std::string(reason).find("not set") != std::string::npos);
    }
}

TEST_CASE("The connection setup carries the display's MIT-MAGIC-COOKIE-1", "[x11hostaccess]") {
    char hostname[256] = {};
    REQUIRE(gethostname(hostname, sizeof(hostname) - 1) == 0);
    const std::string host = hostname;

    const std::string otherCookie(16, '\x11');
    const std::string cookie(16, '\xAB');

    /* An Xauthority file to go with each section; created where the test wants it. */
    TempDirectory directory;
    const auto writeAuthority = [&](const char* name, const std::vector<unsigned char>& content) {
        const std::string path = directory.path(name);
        FILE* file = std::fopen(path.c_str(), "wb");
        REQUIRE(file != nullptr);
        REQUIRE(std::fwrite(content.data(), 1, content.size(), file) == content.size());
        REQUIRE(std::fclose(file) == 0);
        return path;
    };

    const auto checkSetup = [](GrantPlan plan, const std::string& expectedCookie) {
        FakeXServer server;
        server.start(FakeXServer::Behaviour::InsertAndList);
        plan.socketCount = 1;
        std::memcpy(plan.socketPath[0], server.socketPath().c_str(), server.socketPath().size() + 1);
        plan.timeoutSeconds = 2;
        const Outcome outcome = runInChild(plan);
        REQUIRE(server.wait());
        REQUIRE(outcome.code == ExitCode::Ok);
        const auto observed = server.observed();
        CHECK(observed.authorizationName == (expectedCookie.empty() ? "" : kCookieName));
        CHECK(observed.authorizationData == expectedCookie);
    };

    char reason[X11HostAccess::kMaxReason];
    GrantPlan plan;

    SECTION("the child reads the cookie for its display after the plan is prepared") {
        const std::string path = directory.base() + "/Xauthority";
        REQUIRE(X11HostAccess::prepareGrant(":0", path.c_str(), nullptr, "root", plan, reason, sizeof(reason)));
        std::vector<unsigned char> authority;
        appendAuthorityRecord(authority, 256, host, "1", kCookieName, otherCookie);
        appendAuthorityRecord(authority, 256, host, "0", kCookieName, cookie);
        authority.insert(authority.end(), {0, 1, 0});           /* corrupt tail: ignored */
        writeAuthority("Xauthority", authority);
        checkSetup(plan, cookie);
    }

    SECTION("a wildcard record matches every display") {
        std::vector<unsigned char> authority;
        appendAuthorityRecord(authority, 65535, "", "", kCookieName, cookie);
        const std::string path = writeAuthority("Xauthority", authority);

        REQUIRE(X11HostAccess::prepareGrant(":7", path.c_str(), nullptr, "root", plan, reason, sizeof(reason)));
        checkSetup(plan, cookie);
    }

    SECTION("display managers store the address as <host>/unix") {
        std::vector<unsigned char> authority;
        appendAuthorityRecord(authority, 256, host + "/unix", "0", kCookieName, cookie);
        const std::string path = writeAuthority("Xauthority", authority);

        REQUIRE(X11HostAccess::prepareGrant(":0", path.c_str(), nullptr, "root", plan, reason, sizeof(reason)));
        checkSetup(plan, cookie);
    }

    SECTION("without $XAUTHORITY the cookie is read from <home>/.Xauthority") {
        std::vector<unsigned char> authority;
        appendAuthorityRecord(authority, 256, host, "0", kCookieName, cookie);
        writeAuthority(".Xauthority", authority);

        REQUIRE(X11HostAccess::prepareGrant(":0", nullptr, directory.base().c_str(), "root", plan, reason,
                                            sizeof(reason)));
        checkSetup(plan, cookie);
    }

    SECTION("a display the file has no entry for connects without a cookie") {
        std::vector<unsigned char> authority;
        appendAuthorityRecord(authority, 256, host, "1", kCookieName, cookie);
        const std::string path = writeAuthority("Xauthority", authority);

        REQUIRE(X11HostAccess::prepareGrant(":3", path.c_str(), nullptr, "root", plan, reason, sizeof(reason)));
        checkSetup(plan, {});
    }

    SECTION("an unreadable authority file is not an error") {
        REQUIRE(X11HostAccess::prepareGrant(":0", "/nonexistent/Xauthority", nullptr, "root", plan, reason,
                                            sizeof(reason)));
        checkSetup(plan, {});
    }

    SECTION("an authority path that names a FIFO does not hang startup") {
        const std::string path = directory.path("Xauthority");
        REQUIRE(mkfifo(path.c_str(), 0600) == 0);
        REQUIRE(X11HostAccess::prepareGrant(":0", path.c_str(), nullptr, "root", plan, reason, sizeof(reason)));
        checkSetup(plan, {});
    }
}

TEST_CASE("root is added to the X server's host access list", "[x11hostaccess]") {
    FakeXServer server({localUser("master")});
    server.start(FakeXServer::Behaviour::InsertAndList);

    const Outcome outcome = runInChild(planFor(server));
    REQUIRE(server.wait());

    CHECK(outcome.code == ExitCode::Ok);
    CHECK(outcome.report == "si:localuser:root is on the host access list (si:localuser:master, si:localuser:root)");
    CHECK(server.observed().insertedFamily == kFamilyServerInterpreted);
    CHECK(server.observed().insertedAddress == localUser("root"));
    CHECK(server.observed().listHostsRequested);
}

TEST_CASE("Every way the grant can fail is reported", "[x11hostaccess]") {
    SECTION("the server lists hosts, but not root") {
        FakeXServer server({localUser("master")});
        server.start(FakeXServer::Behaviour::ListWithoutUser);
        const Outcome outcome = runInChild(planFor(server));
        REQUIRE(server.wait());
        CHECK(outcome.code == ExitCode::NotGranted);
        CHECK(outcome.report == "si:localuser:root is not on the host access list (si:localuser:master)");
    }

    SECTION("the server refuses the ChangeHosts request") {
        FakeXServer server;
        server.start(FakeXServer::Behaviour::RefuseChangeHosts);
        const Outcome outcome = runInChild(planFor(server));
        REQUIRE(server.wait());
        CHECK(outcome.code == ExitCode::ChangeHostsRefused);
        CHECK(outcome.report == "the X server refused the ChangeHosts request (error 2)");
    }

    SECTION("the server refuses the connection") {
        FakeXServer server;
        server.start(FakeXServer::Behaviour::RefuseConnection);
        const Outcome outcome = runInChild(planFor(server));
        REQUIRE(server.wait());
        CHECK(outcome.code == ExitCode::SetupRejected);
        CHECK(outcome.report == "the X server rejected the connection: No protocol specified");
    }

    SECTION("the server stops answering") {
        FakeXServer server;
        server.start(FakeXServer::Behaviour::CloseAfterSetup);
        const Outcome outcome = runInChild(planFor(server));
        REQUIRE(server.wait());
        CHECK(outcome.code == ExitCode::ProtocolError);
        CHECK(outcome.report == "no reply to ListHosts: the X server closed the connection");
    }

    SECTION("the server stalls") {
        FakeXServer server;
        server.start(FakeXServer::Behaviour::Stall);
        GrantPlan plan = planFor(server);
        plan.timeoutSeconds = 1;
        const Outcome outcome = runInChild(plan);
        REQUIRE(server.wait());
        CHECK(outcome.code == ExitCode::Timeout);
        CHECK(outcome.report == "no reply to ListHosts: timed out waiting for the X server");
    }

    SECTION("the server trickles bytes, keeping every single read alive") {
        FakeXServer server;
        server.start(FakeXServer::Behaviour::Trickle);
        GrantPlan plan = planFor(server);
        plan.timeoutSeconds = 1;                         // deadline: twice this
        const auto started = std::chrono::steady_clock::now();
        const Outcome outcome = runInChild(plan);
        REQUIRE(server.wait());
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started).count();
        CHECK(outcome.code == -SIGALRM);                 // killed by the deadline
        CHECK(seconds < 10);
    }

    SECTION("nothing is listening on the socket") {
        char reason[X11HostAccess::kMaxReason];
        GrantPlan plan;
        REQUIRE(X11HostAccess::prepareGrant(":0", nullptr, nullptr, "root", plan, reason, sizeof(reason)));
        plan.socketCount = 1;
        std::memcpy(plan.socketPath[0], "/tmp/x11hostaccess-nothing/X0", sizeof("/tmp/x11hostaccess-nothing/X0"));
        const Outcome outcome = runInChild(plan);
        CHECK(outcome.code == ExitCode::CouldNotConnect);
        CHECK(outcome.report == "could not connect to the X display socket /tmp/x11hostaccess-nothing/X0");
    }

    SECTION("access control is disabled, so nobody needs to be listed") {
        FakeXServer server({localUser("master")});
        server.start(FakeXServer::Behaviour::AccessControlDisabled);
        const Outcome outcome = runInChild(planFor(server));
        REQUIRE(server.wait());
        CHECK(outcome.code == ExitCode::Ok);
        CHECK(outcome.report ==
              "si:localuser:root may connect, access control is disabled (host access list: si:localuser:master)");
    }
}

TEST_CASE("listOnly reports the host access list without changing it", "[x11hostaccess]") {
    SECTION("root is not listed") {
        FakeXServer server({localUser("master")});
        server.start(FakeXServer::Behaviour::InsertAndList);
        GrantPlan plan = planFor(server);
        plan.listOnly = true;
        const Outcome outcome = runInChild(plan);
        REQUIRE(server.wait());
        CHECK(outcome.code == ExitCode::NotGranted);
        CHECK(outcome.report == "si:localuser:root is not on the host access list (si:localuser:master)");
        CHECK(server.observed().insertedAddress.empty());
        CHECK(server.observed().listHostsRequested);
    }

    SECTION("root is listed") {
        FakeXServer server({localUser("master"), localUser("root")});
        server.start(FakeXServer::Behaviour::InsertAndList);
        GrantPlan plan = planFor(server);
        plan.listOnly = true;
        const Outcome outcome = runInChild(plan);
        REQUIRE(server.wait());
        CHECK(outcome.code == ExitCode::Ok);
        CHECK(outcome.report == "si:localuser:root is on the host access list (si:localuser:master, si:localuser:root)");
    }

    SECTION("an empty list") {
        FakeXServer server;
        server.start(FakeXServer::Behaviour::InsertAndList);
        GrantPlan plan = planFor(server);
        plan.listOnly = true;
        const Outcome outcome = runInChild(plan);
        REQUIRE(server.wait());
        CHECK(outcome.code == ExitCode::NotGranted);
        CHECK(outcome.report == "si:localuser:root is not on the host access list (empty)");
    }
}

TEST_CASE("describeOutcome explains every way the helper can end", "[x11hostaccess]") {
    char reason[X11HostAccess::kMaxReason];

    CHECK(X11HostAccess::describeOutcome(ExitCode::Ok << 8, "all good", reason, sizeof(reason)));
    CHECK(std::string(reason) == "all good");

    CHECK_FALSE(X11HostAccess::describeOutcome(ExitCode::SetupRejected << 8, "the X server said no", reason,
                                               sizeof(reason)));
    CHECK(std::string(reason) == "the X server said no");

    const int codes[] = {X11HostAccess::PrivilegeDropFailed, X11HostAccess::CouldNotConnect, X11HostAccess::IoError,
                         X11HostAccess::Timeout,          X11HostAccess::ChangeHostsRefused,
                         X11HostAccess::ListHostsRefused, X11HostAccess::ProtocolError,
                         X11HostAccess::NotGranted,       99};
    for (const int code : codes) {
        INFO(code);
        CHECK_FALSE(X11HostAccess::describeOutcome(code << 8, "", reason, sizeof(reason)));
        CHECK(std::strlen(reason) > 0);
    }
    CHECK_FALSE(X11HostAccess::describeOutcome(ExitCode::NotGranted << 8, "", reason, sizeof(reason)));
    CHECK(std::string(reason) == "the user is not on the X server's host access list");

    CHECK_FALSE(X11HostAccess::describeOutcome(SIGKILL, "", reason, sizeof(reason)));
    CHECK(std::string(reason) == "the grant helper was killed by signal 9");
}

/* Read-only check against a real X server: lists the host access list, changes
 * nothing.  Run with X11HOSTACCESS_TEST_DISPLAY=:0 to include it. */
TEST_CASE("The exchange works against a real X server", "[x11hostaccess][.hidden]") {
    const char* display = std::getenv("X11HOSTACCESS_TEST_DISPLAY");
    if (!display) {
        SKIP("set X11HOSTACCESS_TEST_DISPLAY to test against a real X server");
    }

    GrantPlan plan;
    char reason[X11HostAccess::kMaxReason];
    REQUIRE(X11HostAccess::prepareGrant(display, nullptr, nullptr, "root", plan, reason, sizeof(reason)));
    plan.listOnly = true;

    const Outcome outcome = runInChild(plan);
    CHECK((outcome.code == ExitCode::Ok || outcome.code == ExitCode::NotGranted));
    WARN("X server " << display << ": " << outcome.report);
}
