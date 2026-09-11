/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Hazard tests for runWithTimeout() in timeout_utils.h.
 *
 * On the timeout and cancellation paths the worker is detached and the
 * function returns, so anything it reaches by reference dies underneath it.
 * These cases provoke that deliberately, for a sanitiser to catch rather
 * than the field. They were hidden behind [.timeout-hazard] because an
 * uninstrumented build segfaults on them, which is the point but is no use
 * as a CI signal. Run them explicitly against build-asan or build-tsan with
 * the "[timeout-hazard]" filter; ASan stops at its first report, so name one
 * case at a time.
 */

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include "timeout_utils.h"

using rpi_imager::TimeoutConfig;
using rpi_imager::TimeoutResult;
using rpi_imager::runWithTimeout;

namespace {

// Stands in for a syscall wedged on an unresponsive device: blocks until the
// test releases it, exactly as a pwrite() to a stalled SD card blocks until the
// onTimeout handler closes the fd.
//
// Held by shared_ptr and captured by value, so the operation lambda itself owns
// nothing that can dangle. Any lifetime error a sanitiser reports therefore
// belongs to runWithTimeout, not to this fixture.
class Gate {
 public:
  void wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return released_; });
  }

  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      released_ = true;
    }
    cv_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool released_ = false;
};

// Overwrites the stack region runWithTimeout was using, so the detached
// thread's write lands in memory that has since been handed to someone else.
// Not needed for ASan -- its fake-stack poisoning catches the access whether or
// not the frame is reused -- but it turns the uninstrumented build from
// "silently gets away with it" into a visible corruption.
__attribute__((noinline)) void clobberDeadFrame() {
  volatile std::uint64_t scratch[512];
  for (std::size_t i = 0; i < sizeof(scratch) / sizeof(scratch[0]); ++i) {
    scratch[i] = 0xDEADBEEFDEADBEEFULL;
  }
}

// Time for the detached worker to wake and perform its write into the dead
// frame. Generous: the test's value is in the sanitiser report, and exiting the
// process before the worker gets there would prove nothing.
constexpr auto kWorkerSettleTime = std::chrono::milliseconds(500);

}  // namespace

TEST_CASE("timed-out operation does not write into runWithTimeout's dead frame",
          "[timeout-hazard]") {
  auto gate = std::make_shared<Gate>();
  std::atomic<bool> onTimeoutFired{false};

  // 1s timeout against an operation that will not return until we say so.
  auto result = runWithTimeout(
      [gate]() { gate->wait(); },
      TimeoutConfig(1).withOnTimeout([&onTimeoutFired]() {
        // The real handler closes the fd to unblock the syscall. Here the gate
        // plays that role, released below.
        onTimeoutFired.store(true);
      }));

  REQUIRE(result == TimeoutResult::TimedOut);
  REQUIRE(onTimeoutFired.load());

  // runWithTimeout has returned. Its `completed` flag, its promise and the
  // promise's heap-allocated shared state are all gone; the worker is still
  // parked in gate->wait() holding references to every one of them.
  clobberDeadFrame();

  // Unblock the worker. It now runs `completed.store(true)` followed by
  // `promise.set_value()` against destroyed objects.
  gate->release();
  std::this_thread::sleep_for(kWorkerSettleTime);

  SUCCEED("reached the end without the worker taking the process down");
}

TEST_CASE("cancelled operation does not write into runWithTimeout's dead frame",
          "[timeout-hazard]") {
  auto gate = std::make_shared<Gate>();
  std::atomic<bool> cancel{false};

  // Trip the cancellation flag shortly after runWithTimeout starts polling it.
  // Joined before the test returns, so the canceller itself is not a hazard.
  std::thread canceller([&cancel]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    cancel.store(true);
  });

  auto result = runWithTimeout(
      [gate]() { gate->wait(); },
      // A long timeout, so cancellation is unambiguously what ends the wait.
      TimeoutConfig(600).withCancelFlag(&cancel));

  canceller.join();
  REQUIRE(result == TimeoutResult::Cancelled);

  // Same dangling worker as the timeout case, and worse in one respect: the
  // cancel path never invokes onTimeout, so in production nothing closes the fd
  // and the worker stays blocked in the syscall for the life of the process.
  clobberDeadFrame();

  gate->release();
  std::this_thread::sleep_for(kWorkerSettleTime);

  SUCCEED("reached the end without the worker taking the process down");
}

// The overload that captures a result has a second thing to keep alive: the
// operation itself. It used to hand the worker a reference to the caller's
// lambda, which at every call site here is a temporary that dies at the end
// of the full expression -- long before a cancelled worker gets round to
// calling it. DownloadThread::_openAndPrepareDevice() hit this when a write
// was cancelled while the end of the card was being zeroed, and took the
// process down about one run in three.
__attribute__((noinline))
static TimeoutResult prepareWithResult(const std::shared_ptr<Gate> &gate,
                                       std::atomic<bool> *cancel, int &out) {
  int local = 0;
  const TimeoutResult outcome = runWithTimeout(
      [gate]() { gate->wait(); return 7; },
      local,
      TimeoutConfig(600).withCancelFlag(cancel));
  out = local;
  return outcome;
}

TEST_CASE("cancelled operation with a result leaves it untouched",
          "[timeout-utils]") {
  auto gate = std::make_shared<Gate>();
  std::atomic<bool> cancel{false};
  std::thread canceller([&cancel]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    cancel.store(true);
  });

  int result = -1;
  const TimeoutResult outcome = prepareWithResult(gate, &cancel, result);
  canceller.join();

  REQUIRE(outcome == TimeoutResult::Cancelled);
  // The abandoned operation must not reach the caller's variable either.
  CHECK(result == 0);

  // Reuse the stack the returned frame occupied, then let the worker run.
  clobberDeadFrame();
  gate->release();
  std::this_thread::sleep_for(kWorkerSettleTime);
  SUCCEED("the abandoned worker ran its own copy of the operation");
}

// Control case. Exercises the same machinery on the path where the worker is
// joined, so its state is still alive when it writes. A sanitiser firing here
// would mean the harness above is at fault rather than runWithTimeout.
TEST_CASE("completed operation is clean", "[timeout-utils]") {
  auto gate = std::make_shared<Gate>();
  gate->release();

  int sideEffect = 0;
  auto result = runWithTimeout([gate, &sideEffect]() {
    gate->wait();
    sideEffect = 42;
  }, TimeoutConfig(30));

  REQUIRE(result == TimeoutResult::Completed);
  REQUIRE(sideEffect == 42);
}

// ── Abandoning is now the exception, not the rule ───────────────────────
//
// A cancellation usually arrives while a perfectly healthy operation is in
// flight; the operation is not stuck, it is simply not finished. Detaching
// unconditionally threw away a thread that was about to complete, and left
// it running against the caller's dying frame. Waiting briefly instead
// means the common case is joined and nothing outlives the call.

TEST_CASE("a cancelled operation that finishes is joined, not abandoned",
          "[timeout-utils]") {
    std::atomic<bool> cancelled{true};          // cancelled before it starts
    auto finished = std::make_shared<std::atomic<bool>>(false);

    const auto result = rpi_imager::runWithTimeout(
        [finished]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            finished->store(true);
        },
        rpi_imager::TimeoutConfig(5).withCancelFlag(&cancelled));

    CHECK(result == rpi_imager::TimeoutResult::Cancelled);
    // The point: by the time the call returns the worker is done and gone,
    // so anything it captured is safe to destroy.
    CHECK(finished->load());
}

TEST_CASE("an operation that will not finish is still abandoned",
          "[timeout-utils]") {
    // The grace period has to stay bounded. A worker wedged in a syscall
    // that never returns must not hold the caller here -- that would be the
    // hang this class exists to escape, moved somewhere worse.
    auto release = std::make_shared<std::atomic<bool>>(false);
    auto started = std::make_shared<std::atomic<bool>>(false);

    const auto begin = std::chrono::steady_clock::now();
    const auto result = rpi_imager::runWithTimeout(
        [release, started]() {
            started->store(true);
            while (!release->load())
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        },
        rpi_imager::TimeoutConfig(1).withJoinGrace(std::chrono::milliseconds(150)));
    const auto took = std::chrono::steady_clock::now() - begin;

    CHECK(result == rpi_imager::TimeoutResult::TimedOut);
    CHECK(started->load());
    // Returned rather than waiting for a worker that never finishes: the
    // one second timeout plus the grace, not for ever.
    CHECK(took < std::chrono::seconds(5));

    // Let the abandoned worker go. Its state is shared, so this is safe
    // whether it has been joined or detached -- which is the contract.
    release->store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
