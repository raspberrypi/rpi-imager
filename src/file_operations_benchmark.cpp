// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "file_operations_benchmark.h"

#include "file_operations.h"
#include "write_buffer_provider.h"
#include "privileged_io/privileged_writer.h"
#include "privileged_io_glue.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

namespace rpi_imager {

namespace {

const char* describeSelectedBackend() {
#if defined(__APPLE__)
    const char* legacy = std::getenv("RPI_IMAGER_USE_LEGACY_AUTHOPEN");
    if (legacy && legacy[0] == '1') return "macos-authopen-legacy";
    const char* xpc = std::getenv("RPI_IMAGER_USE_XPC_HELPER");
    if (xpc && xpc[0] == '0') return "macos-authopen-legacy";
    return "macos-xpc-helper";
#elif defined(__linux__)
    if (::geteuid() == 0) {
        return "linux-embedded";
    }
#if defined(RPI_IMAGER_ENABLE_LINUX_HELPER)
    if (preferNativePrivilegedHelper("RPI_IMAGER_USE_LINUX_HELPER")) {
        return "linux-polkit-helper";
    }
#endif
    return "linux-in-process";
#elif defined(_WIN32)
#if defined(RPI_IMAGER_ENABLE_WINDOWS_HELPER)
    if (preferNativePrivilegedHelper("RPI_IMAGER_USE_WINDOWS_HELPER")) {
        return "windows-uac-helper";
    }
#endif
    return "windows-in-process";
#else
    return "unknown";
#endif
}

// One sample per submitted write. submit_us / complete_us are
// monotonic offsets in microseconds from benchmark start. Captured
// from both the submit thread (submit_us) and the completion callback
// (complete_us / size / status), so a slow completion shows up as a
// long span even if submits were fast.
struct WriteSample {
    std::int64_t  submit_us = -1;
    std::int64_t  complete_us = -1;
    std::size_t   size = 0;
    int           status = 0;       // FileError underlying int
    int           sequence = 0;     // 0-based submit order
};

// Helper-only: read the tail of /Library/Logs/Raspberry Pi/Imager/helper.log
// so the capture is self-contained. Returns the last `max_bytes` of the
// file (or all if smaller), empty string on any failure.
std::string readHelperAuditLogTail(std::size_t max_bytes) {
#if defined(__APPLE__)
    const char* path = "/Library/Logs/Raspberry Pi/Imager/helper.log";
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    if (size <= 0) return {};
    std::size_t bytes = static_cast<std::size_t>(size);
    std::size_t skip = bytes > max_bytes ? bytes - max_bytes : 0;
    f.seekg(static_cast<std::streamoff>(skip), std::ios::beg);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
#elif defined(__linux__)
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    std::string path = runtime && runtime[0]
                           ? std::string(runtime) + "/rpi-imager/helper.log"
                           : "/tmp/rpi-imager-helper.log";
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    if (size <= 0) return {};
    std::size_t bytes = static_cast<std::size_t>(size);
    std::size_t skip = bytes > max_bytes ? bytes - max_bytes : 0;
    f.seekg(static_cast<std::streamoff>(skip), std::ios::beg);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
#else
    (void)max_bytes;
    return {};
#endif
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string isoTimestampNow() {
    char ts[40];
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", &tm_buf);
    return ts;
}

}  // namespace

int runFileOperationsBenchmark(const BenchmarkOptions& opts) {
    auto file = FileOperations::Create();
    if (!file) {
        std::fprintf(stderr, "benchmark: FileOperations::Create() returned null\n");
        return 1;
    }

    const char* backend = describeSelectedBackend();
    std::printf("benchmark: backend=%s device=%s bytes=%zu chunk=%zu qd=%d direct=%d\n",
                backend, opts.device_path.c_str(),
                opts.total_bytes, opts.chunk_bytes,
                opts.queue_depth, (int)opts.direct_io);

    // Unmount any volumes on the device before opening. macOS will
    // auto-mount partitions on insertion; without unmount the helper's
    // ::open(O_RDWR) returns EBUSY. The real imager pipeline does this
    // via getProcessPrivilegedWriter().unmount(); we mirror that here so
    // the comparison includes everything the imager actually pays for.
    auto unmount_t0 = std::chrono::steady_clock::now();
    {
        std::string unmount_path = opts.device_path;
        constexpr const char kRPrefix[] = "/dev/rdisk";
        if (unmount_path.rfind(kRPrefix, 0) == 0) {
            unmount_path = "/dev/disk" + unmount_path.substr(sizeof(kRPrefix) - 1);
        }
        auto& w = ::rpi_imager::getProcessPrivilegedWriter();
        auto r = w.unmount(unmount_path);
        if (!r.ok) {
            std::fprintf(stderr,
                         "benchmark: unmount(%s) failed: %s (continuing anyway)\n",
                         unmount_path.c_str(), r.error.detail().c_str());
        } else {
            std::printf("benchmark: unmounted %s\n", unmount_path.c_str());
        }
    }
    auto unmount_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - unmount_t0).count();

    auto open_t0 = std::chrono::steady_clock::now();
    if (file->OpenDevice(opts.device_path) != FileError::kSuccess) {
        std::fprintf(stderr, "benchmark: OpenDevice failed for %s\n",
                     opts.device_path.c_str());
        return 1;
    }
    auto open_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - open_t0).count();

    if (!opts.direct_io) {
        (void)file->SetDirectIOEnabled(false);
    }

    const bool async_ok = (opts.queue_depth > 1) &&
                            file->IsAsyncIOSupported() &&
                            file->SetAsyncQueueDepth(opts.queue_depth);
    std::printf("benchmark: async=%s\n", async_ok ? "yes" : "no (sync fallback)");

    std::vector<std::uint8_t> chunk(opts.chunk_bytes, 0);

    // Zero-copy setup: back the write buffers with the backend's shared-memory
    // ring so the benchmark exercises the production zero-copy path (writes
    // submit directly from mapped slots, no per-write copy). The backend's
    // zero-copy mode provides no internal back-pressure (it relies on the
    // producer's ring), so we cap in-flight writes to the slot count and don't
    // reuse a slot until its write completes. Falls back to the copy path if a
    // provider isn't available or the ring can't be mapped.
    std::shared_ptr<WriteBufferProvider> provider;
    std::vector<void*> zc_slots;
    std::size_t zc_slot_bytes = 0;
    bool zero_copy_engaged = false;
    if (async_ok && opts.zero_copy) {
        provider = file->CreateWriteBufferProvider();
        if (provider) {
            std::size_t pageSize = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
            if (pageSize == 0) pageSize = 4096;
            zc_slot_bytes = ((opts.chunk_bytes + pageSize - 1) / pageSize) * pageSize;
            if (provider->allocateSlots(static_cast<std::size_t>(opts.queue_depth),
                                        zc_slot_bytes, pageSize, zc_slots) &&
                provider->isZeroCopy()) {
                zero_copy_engaged = true;
                for (void* s : zc_slots) std::memset(s, 0, zc_slot_bytes);
            } else {
                provider.reset();
                zc_slots.clear();
            }
        }
    }
    std::printf("benchmark: zero-copy=%s\n",
                zero_copy_engaged ? "yes"
                                  : (opts.zero_copy ? "no (provider unavailable; copy path)"
                                                    : "no (disabled; copy path)"));

    // Free-slot tracking for the zero-copy path's back-pressure.
    std::vector<char> slot_busy(zero_copy_engaged ? zc_slots.size() : 0, 0);
    std::mutex slot_mtx;
    std::condition_variable slot_cv;

    std::atomic<std::size_t> errors{0};
    std::atomic<std::size_t> bytes_done{0};

    // Pre-allocate one sample per anticipated write.
    const std::size_t expected_writes =
        (opts.total_bytes + opts.chunk_bytes - 1) / opts.chunk_bytes;
    std::vector<WriteSample> samples(expected_writes);
    std::mutex samples_mutex;  // only used to guard the rare resize case

    auto t0 = std::chrono::steady_clock::now();
    auto now_us = [t0]() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();
    };

    (void)file->Seek(0);

    std::size_t remaining = opts.total_bytes;
    int seq = 0;
    while (remaining > 0) {
        const std::size_t this_chunk = std::min(remaining, opts.chunk_bytes);

        // Reserve a sample slot up-front so the completion callback can
        // fill it without contention.
        std::size_t idx;
        {
            std::lock_guard<std::mutex> lk(samples_mutex);
            if ((std::size_t)seq >= samples.size()) samples.resize(seq + 1);
            idx = (std::size_t)seq;
            samples[idx].sequence = seq;
            samples[idx].size = this_chunk;
            samples[idx].submit_us = now_us();
        }

        FileError fe;
        if (zero_copy_engaged) {
            // Acquire a free ring slot (blocks once queue_depth are in flight).
            std::size_t slotIdx = 0;
            {
                std::unique_lock<std::mutex> lk(slot_mtx);
                slot_cv.wait(lk, [&]{
                    for (char b : slot_busy) if (!b) return true;
                    return false;
                });
                for (std::size_t i = 0; i < slot_busy.size(); ++i) {
                    if (!slot_busy[i]) { slotIdx = i; slot_busy[i] = 1; break; }
                }
            }
            fe = file->AsyncWriteSequential(
                static_cast<const std::uint8_t*>(zc_slots[slotIdx]), this_chunk,
                [&samples, idx, &errors, &bytes_done, &now_us,
                 &slot_mtx, &slot_cv, &slot_busy, slotIdx]
                (FileError r, std::size_t n) {
                    samples[idx].complete_us = now_us();
                    samples[idx].status = (int)r;
                    samples[idx].size = n;
                    if (r != FileError::kSuccess) errors.fetch_add(1);
                    else bytes_done.fetch_add(n);
                    { std::lock_guard<std::mutex> lk(slot_mtx); slot_busy[slotIdx] = 0; }
                    slot_cv.notify_one();
                });
        } else if (async_ok) {
            fe = file->AsyncWriteSequential(chunk.data(), this_chunk,
                [&samples, idx, &errors, &bytes_done, &now_us]
                (FileError r, std::size_t n) {
                    samples[idx].complete_us = now_us();
                    samples[idx].status = (int)r;
                    samples[idx].size = n;
                    if (r != FileError::kSuccess) errors.fetch_add(1);
                    else bytes_done.fetch_add(n);
                });
        } else {
            fe = file->WriteSequential(chunk.data(), this_chunk);
            samples[idx].complete_us = now_us();
            samples[idx].status = (int)fe;
            if (fe == FileError::kSuccess) bytes_done.fetch_add(this_chunk);
        }
        if (fe != FileError::kSuccess) {
            std::fprintf(stderr,
                         "benchmark: write failed at remaining=%zu (FileError=%d)\n",
                         remaining, (int)fe);
            errors.fetch_add(1);
            break;
        }
        remaining -= this_chunk;
        ++seq;
    }

    auto wait_t0 = std::chrono::steady_clock::now();
    if (async_ok) {
        FileError fe = file->WaitForPendingWrites();
        if (fe != FileError::kSuccess) {
            std::fprintf(stderr,
                         "benchmark: WaitForPendingWrites returned FileError=%d\n",
                         (int)fe);
            errors.fetch_add(1);
        }
    }
    auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - wait_t0).count();

    auto sync_t0 = std::chrono::steady_clock::now();
    (void)file->Flush();
    (void)file->ForceSync();
    auto sync_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - sync_t0).count();

    auto t1 = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    // Optional verify pass via FileOperations::FastVerifySha256. We hash
    // exactly the bytes we wrote (zeros), with no prefix, so a correct
    // verify path returns SHA-256(0x00 × total_bytes) regardless of
    // backend - the test is whether it ran fast.
    std::int64_t verify_elapsed_ms = -1;
    double verify_mb_s = 0.0;
    bool   verify_supported = false;
    bool   verify_ok = false;
    std::string verify_digest_hex;
    if (opts.verify_after_write && bytes_done.load() > 0) {
        auto v0 = std::chrono::steady_clock::now();
        auto v = file->FastVerifySha256(nullptr, 0, 0,
                                          (std::uint64_t)bytes_done.load());
        auto v1 = std::chrono::steady_clock::now();
        verify_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(v1 - v0).count();
        verify_supported = v.supported;
        if (v.supported && v.error == FileError::kSuccess) {
            verify_ok = true;
            char hex[65] = {0};
            for (std::size_t i = 0; i < v.digest.size() && i < 32; ++i) {
                std::snprintf(hex + i * 2, 3, "%02x",
                              (unsigned)(std::uint8_t)v.digest[i]);
            }
            verify_digest_hex = hex;
            const double vmb = (double)bytes_done.load() / (1024.0 * 1024.0);
            verify_mb_s = verify_elapsed_ms > 0
                            ? (vmb * 1000.0) / (double)verify_elapsed_ms
                            : 0.0;
            std::printf("benchmark: verify ok in %lld ms (%.2f MB/s) sha256=%s\n",
                        (long long)verify_elapsed_ms, verify_mb_s,
                        verify_digest_hex.c_str());
        } else if (v.supported) {
            std::printf("benchmark: verify FAILED (FileError=%d)\n",
                        (int)v.error);
        } else {
            std::printf("benchmark: verify not supported by backend (skipped)\n");
        }
    }

    (void)file->Close();

    const double mb_written = (double)bytes_done.load() / (1024.0 * 1024.0);
    const double mb_per_s = elapsed_ms > 0
                              ? (mb_written * 1000.0) / (double)elapsed_ms
                              : 0.0;

    std::uint32_t wallMs = 0, count = 0, minUs = 0, maxUs = 0, avgUs = 0;
    if (async_ok) {
        file->GetAsyncIOStats(wallMs, count, minUs, maxUs, avgUs);
    }

    bool zc_stat_engaged = false;
    std::uint64_t zc_submits = 0, copy_submits = 0;
    file->GetZeroCopyWriteStats(zc_stat_engaged, zc_submits, copy_submits);

    std::printf("benchmark: wrote %.1f MiB in %lld ms (%.2f MB/s)\n",
                mb_written, (long long)elapsed_ms, mb_per_s);
    if (async_ok && count > 0) {
        std::printf("benchmark: async writes=%u min=%uus avg=%uus max=%uus\n",
                    count, minUs, avgUs, maxUs);
        std::printf("benchmark: zero-copy engaged=%s submits=%llu copy-submits=%llu\n",
                    zc_stat_engaged ? "yes" : "no",
                    (unsigned long long)zc_submits,
                    (unsigned long long)copy_submits);
    }
    if (errors.load() > 0) {
        std::printf("benchmark: %zu errors\n", errors.load());
    }

    // ---- Capture file (JSON) ------------------------------------------------
    if (!opts.output_path.empty()) {
        std::ofstream out(opts.output_path, std::ios::out | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr,
                         "benchmark: could not open output %s for writing\n",
                         opts.output_path.c_str());
        } else {
            struct utsname uts{};
            uname(&uts);

            out << "{\n";
            out << "  \"schema\": \"rpi-imager-benchmark/v1\",\n";
            out << "  \"timestamp\": \"" << jsonEscape(isoTimestampNow()) << "\",\n";
            out << "  \"host\": {\n";
            out << "    \"sysname\": \"" << jsonEscape(uts.sysname) << "\",\n";
            out << "    \"release\": \"" << jsonEscape(uts.release) << "\",\n";
            out << "    \"machine\": \"" << jsonEscape(uts.machine) << "\"\n";
            out << "  },\n";
            out << "  \"config\": {\n";
            out << "    \"backend\": \"" << jsonEscape(backend) << "\",\n";
            out << "    \"device\": \"" << jsonEscape(opts.device_path) << "\",\n";
            out << "    \"total_bytes\": " << opts.total_bytes << ",\n";
            out << "    \"chunk_bytes\": " << opts.chunk_bytes << ",\n";
            out << "    \"queue_depth\": " << opts.queue_depth << ",\n";
            out << "    \"direct_io\": " << (opts.direct_io ? "true" : "false") << ",\n";
            out << "    \"async_engaged\": " << (async_ok ? "true" : "false") << ",\n";
            out << "    \"zero_copy_requested\": " << (opts.zero_copy ? "true" : "false") << ",\n";
            out << "    \"zero_copy_engaged\": " << (zero_copy_engaged ? "true" : "false") << "\n";
            out << "  },\n";
            out << "  \"summary\": {\n";
            out << "    \"unmount_ms\": " << unmount_ms << ",\n";
            out << "    \"open_ms\": " << open_ms << ",\n";
            out << "    \"wait_ms\": " << wait_ms << ",\n";
            out << "    \"final_sync_ms\": " << sync_ms << ",\n";
            out << "    \"total_elapsed_ms\": " << elapsed_ms << ",\n";
            out << "    \"bytes_written\": " << bytes_done.load() << ",\n";
            out << "    \"throughput_mb_s\": " << mb_per_s << ",\n";
            out << "    \"errors\": " << errors.load() << "\n";
            out << "  },\n";
            out << "  \"verify\": {\n";
            out << "    \"requested\": " << (opts.verify_after_write ? "true" : "false") << ",\n";
            out << "    \"supported\": " << (verify_supported ? "true" : "false") << ",\n";
            out << "    \"ok\": " << (verify_ok ? "true" : "false") << ",\n";
            out << "    \"elapsed_ms\": " << verify_elapsed_ms << ",\n";
            out << "    \"throughput_mb_s\": " << verify_mb_s << ",\n";
            out << "    \"sha256\": \"" << jsonEscape(verify_digest_hex) << "\"\n";
            out << "  },\n";
            out << "  \"async_stats\": {\n";
            out << "    \"writes_completed\": " << count << ",\n";
            out << "    \"min_latency_us\": " << minUs << ",\n";
            out << "    \"avg_latency_us\": " << avgUs << ",\n";
            out << "    \"max_latency_us\": " << maxUs << ",\n";
            out << "    \"wall_clock_ms\": " << wallMs << "\n";
            out << "  },\n";
            out << "  \"zero_copy\": {\n";
            out << "    \"engaged\": " << (zc_stat_engaged ? "true" : "false") << ",\n";
            out << "    \"zero_copy_submits\": " << zc_submits << ",\n";
            out << "    \"copy_submits\": " << copy_submits << "\n";
            out << "  },\n";
            out << "  \"writes\": [\n";
            std::size_t emitted_samples = 0;
            {
                std::lock_guard<std::mutex> lk(samples_mutex);
                for (std::size_t i = 0; i < samples.size(); ++i) {
                    const auto& s = samples[i];
                    if (s.submit_us < 0) continue;
                    if (emitted_samples > 0) out << ",\n";
                    const std::int64_t lat =
                        s.complete_us >= 0 ? (s.complete_us - s.submit_us) : -1;
                    out << "    {\"seq\":" << s.sequence
                        << ",\"submit_us\":" << s.submit_us
                        << ",\"complete_us\":" << s.complete_us
                        << ",\"latency_us\":" << lat
                        << ",\"size\":" << s.size
                        << ",\"status\":" << s.status << "}";
                    ++emitted_samples;
                }
            }
            out << "\n  ],\n";
            out << "  \"helper_audit_log_tail\": \""
                << jsonEscape(readHelperAuditLogTail(64 * 1024)) << "\"\n";
            out << "}\n";

            std::printf("benchmark: capture written to %s (%zu samples)\n",
                        opts.output_path.c_str(), emitted_samples);
        }
    }

    std::printf("BENCHMARK result=%s backend=%s bytes=%zu elapsed_ms=%lld throughput_mb_s=%.2f zero_copy=%s\n",
                errors.load() == 0 ? "ok" : "fail",
                backend,
                (std::size_t)bytes_done.load(),
                (long long)elapsed_ms,
                mb_per_s,
                zc_stat_engaged ? "yes" : "no");
    return errors.load() == 0 ? 0 : 1;
}

} // namespace rpi_imager
