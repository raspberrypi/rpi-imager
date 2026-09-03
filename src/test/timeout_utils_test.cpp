/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Hazard tests for runWithTimeout() in timeout_utils.h.
 *
 * runWithTimeout() spawns a worker thread whose lambda captures the promise and
 * the completion flag BY REFERENCE. Those objects live on runWithTimeout's own
 * stack frame. On the two exit paths that call detach() -- timeout and external
 * cancellation -- the function returns while the worker is still inside the
 * operation, so the frame dies underneath it. When the operation finally
 * unblocks, the worker writes `completed` and calls promise.set_value() through
 * references to a frame that no longer exists, and into a promise shared state
 * that both the promise and the future have already released.
 *
 * That is undefined behaviour on a path the header documents as expected
 * ("the operation thread is detached and may continue running"). These cases
 * make it happen deliberately and on demand, so a sanitiser can be pointed at
 * it rather than waiting for it to surface as a field crash.
 *
 * THESE CASES DELIBERATELY PROVOKE UNDEFINED BEHAVIOUR. They are tagged [.]
 * (hidden) so neither Catch2's default run nor CTest picks them up -- an
 * uninstrumented build is likely to corrupt memory or segfault, which is the
 * point but is not a useful CI signal. Run them explicitly:
 *
 *   cmake -B build-asan -DBUILD_TESTING=ON -DRPI_IMAGER_TEST_SANITIZER=address
 *   cmake --build build-asan --target timeout_utils_test
 *   ASAN_OPTIONS=detect_stack_use_after_return=1 \
 *       ./build-asan/src/test/timeout_utils_test "[timeout-hazard]"
 *
 *   cmake -B build-tsan -DBUILD_TESTING=ON -DRPI_IMAGER_TEST_SANITIZER=thread
 *   cmake --build build-tsan --target timeout_utils_test
 *   ./build-tsan/src/test/timeout_utils_test "[timeout-hazard]"
 *
 * ASan stops at its first report, so pass a single case name rather than the
 * tag to see both. TSan runs on and reports all four (two per case).
 *
 * As of writing, ASan reports stack-use-after-return on `completed` at
 * timeout_utils.h:100, and TSan reports heap-use-after-free between
 * set_value() at timeout_utils.h:101 and ~promise() at timeout_utils.h:129.
 * Uninstrumented, the timeout case segfaults outright.
 *
 * A clean run means the sanitiser did NOT see the bug, not that the bug is
 * absent. Once runWithTimeout keeps its state in a shared_ptr captured by
 * value, these cases become well defined and should fall silent.
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

TEST_CASE("timed-out operation writes into runWithTimeout's dead frame",
          "[.timeout-hazard]") {
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

TEST_CASE("cancelled operation writes into runWithTimeout's dead frame",
          "[.timeout-hazard]") {
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
