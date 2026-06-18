// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "../drive_watch_service.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>
#include <poll.h>
#include <thread>

#include <libudev.h>

namespace rpi_imager::writer {

struct DriveWatchService::Impl {
    std::mutex mutex;
    NotifyFn notify;
    std::atomic<bool> stop{false};
    std::thread thread;

    udev* udev = nullptr;
    udev_monitor* monitor = nullptr;
    int monitor_fd = -1;

    ~Impl() {
        stop.store(true);
        if (thread.joinable()) {
            thread.join();
        }
        if (monitor) {
            udev_monitor_unref(monitor);
        }
        if (udev) {
            udev_unref(udev);
        }
    }

    void ensureMonitor() {
        if (monitor) {
            return;
        }
        udev = udev_new();
        if (!udev) {
            return;
        }
        monitor = udev_monitor_new_from_netlink(udev, "udev");
        if (!monitor) {
            return;
        }
        udev_monitor_filter_add_match_subsystem_devtype(monitor, "block", nullptr);
        udev_monitor_enable_receiving(monitor);
        monitor_fd = udev_monitor_get_fd(monitor);
    }

    void run() {
        while (!stop.load()) {
            if (monitor_fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            pollfd pfd {};
            pfd.fd = monitor_fd;
            pfd.events = POLLIN;
            const int r = ::poll(&pfd, 1, 500);
            if (r <= 0 || !(pfd.revents & POLLIN)) {
                continue;
            }
            udev_device* dev = udev_monitor_receive_device(monitor);
            if (!dev) {
                continue;
            }
            const char* action = udev_device_get_action(dev);
            if (action
                && (std::strcmp(action, "add") == 0
                    || std::strcmp(action, "remove") == 0
                    || std::strcmp(action, "change") == 0)) {
                NotifyFn fn;
                {
                    std::lock_guard<std::mutex> lk(mutex);
                    fn = notify;
                }
                if (fn) {
                    fn();
                }
            }
            udev_device_unref(dev);
        }
    }
};

DriveWatchService::DriveWatchService() : impl_(new Impl()) {}

DriveWatchService::~DriveWatchService() {
    delete impl_;
}

void DriveWatchService::subscribe(NotifyFn fn) {
    std::lock_guard<std::mutex> lk(impl_->mutex);
    impl_->notify = std::move(fn);
    impl_->ensureMonitor();
    if (!impl_->thread.joinable()) {
        impl_->stop.store(false);
        impl_->thread = std::thread([this] { impl_->run(); });
    }
}

void DriveWatchService::unsubscribe() {
    impl_->stop.store(true);
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    impl_->stop.store(false);
    std::lock_guard<std::mutex> lk(impl_->mutex);
    impl_->notify = nullptr;
}

} // namespace rpi_imager::writer
