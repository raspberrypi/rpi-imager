/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef CURLNETWORKCONFIG_H
#define CURLNETWORKCONFIG_H

#include <QByteArray>
#include <QMutex>
#include <QThreadPool>
#include <atomic>
#include <curl/curl.h>

/**
 * Singleton class for shared libcurl network configuration.
 * 
 * All libcurl-based network operations should use this class to ensure
 * consistent behavior (IPv4-only mode, proxy, timeouts, etc.)
 * 
 * Thread-safe: settings can be read from any thread, written from main thread.
 * 
 * IMPORTANT: Call ensureInitialized() from the main thread at startup before
 * any curl operations can occur (e.g., in main() or ImageWriter constructor).
 */
class CurlNetworkConfig
{
public:
    static CurlNetworkConfig& instance();
    
    /**
     * Initialize libcurl globally. MUST be called from main thread at startup.
     * Safe to call multiple times - only initializes once.
     */
    static void ensureInitialized();
    
    /**
     * Fetch profile determines timeout and retry behavior.
     */
    enum class FetchProfile {
        SmallFile,    // Icons, JSON files - shorter timeouts, no keepalive
        LargeFile,    // OS images - longer timeouts, TCP keepalive, HTTP/2
        FireAndForget // Telemetry - short timeouts, failures are silent
    };
    
    // IPv4-only mode - for users with broken IPv6 routing
    bool ipv4Only() const { return _ipv4Only.load(std::memory_order_relaxed); }
    void setIPv4Only(bool enabled) { _ipv4Only.store(enabled, std::memory_order_relaxed); }
    
    // Proxy settings
    QByteArray proxy() const;
    void setProxy(const QByteArray &proxy);
    
    // User agent string
    QByteArray userAgent() const;
    void setUserAgent(const QByteArray &ua);
    
    /**
     * Apply standard curl settings for a given fetch profile.
     * 
     * This sets up:
     * - Thread safety (NOSIGNAL)
     * - Redirect handling (FOLLOWLOCATION, MAXREDIRS)
     * - Error handling (FAILONERROR)
     * - Timeouts appropriate for the profile
     * - TCP keepalive (for large files)
     * - HTTP/2 (for large files over HTTPS)
     * - IPv4-only mode (if enabled)
     * - Proxy (if configured)
     * - CA bundle (on Linux for AppImage compatibility)
     * - User agent
     * 
     * @param curl The curl handle to configure
     * @param profile The fetch profile determining timeout behavior
     * @param errorBuffer Optional buffer for detailed error messages (must be CURL_ERROR_SIZE)
     */
    void applyCurlSettings(CURL *curl, FetchProfile profile = FetchProfile::SmallFile, 
                          char *errorBuffer = nullptr) const;
    
    /**
     * Get the dedicated thread pool for network I/O operations.
     * 
     * This is separate from Qt's global thread pool to prevent network operations
     * from starving other work. Limited to a reasonable number of concurrent connections.
     */
    QThreadPool* networkThreadPool();
    
    /**
     * Maximum concurrent network fetches (icons + JSON).
     * Higher values = faster parallel loading but more resource usage.
     */
    static constexpr int MaxConcurrentFetches = 6;
    
    // Deleted copy/move constructors
    CurlNetworkConfig(const CurlNetworkConfig&) = delete;
    CurlNetworkConfig& operator=(const CurlNetworkConfig&) = delete;

private:
    CurlNetworkConfig();
    ~CurlNetworkConfig();
    
    static std::atomic<bool> _curlInitialized;
    std::atomic<bool> _ipv4Only{false};
    QByteArray _proxy;
    QByteArray _userAgent;
    mutable QMutex _mutex;
    QThreadPool *_networkPool{nullptr};
};

#endif // CURLNETWORKCONFIG_H
