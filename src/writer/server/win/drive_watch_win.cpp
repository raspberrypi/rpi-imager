// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "../drive_watch_service.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbt.h>
#include <initguid.h>
#include <devguid.h>

namespace rpi_imager::writer {

namespace {

constexpr wchar_t kWindowClass[] = L"RpiImagerWriterDriveWatch";

} // namespace

struct DriveWatchService::Impl {
    std::mutex mutex;
    NotifyFn notify;
    std::atomic<bool> stop{false};
    std::thread thread;
    HWND hwnd = nullptr;
    HDEVNOTIFY dev_notify = nullptr;

    static Impl* fromHwnd(HWND hwnd) {
        return reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    void fireNotify() {
        NotifyFn fn;
        {
            std::lock_guard<std::mutex> lk(mutex);
            fn = notify;
        }
        if (fn) {
            fn();
        }
    }

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        Impl* self = fromHwnd(hwnd);
        if (!self) {
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        switch (msg) {
            case WM_DEVICECHANGE:
                if (wparam == DBT_DEVICEARRIVAL || wparam == DBT_DEVICEREMOVECOMPLETE
                    || wparam == DBT_DEVNODES_CHANGED) {
                    self->fireNotify();
                }
                return TRUE;
            case WM_DESTROY:
                if (self->dev_notify) {
                    UnregisterDeviceNotification(self->dev_notify);
                    self->dev_notify = nullptr;
                }
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    }

    void run() {
        WNDCLASSEXW wc {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);

        hwnd = CreateWindowExW(
            0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
            wc.hInstance, nullptr);
        if (!hwnd) {
            return;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        DEV_BROADCAST_DEVICEINTERFACE_W filter {};
        filter.dbcc_size = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid = GUID_DEVINTERFACE_DISK;
        dev_notify = RegisterDeviceNotificationW(
            hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

        MSG msg {};
        while (!stop.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        DestroyWindow(hwnd);
        hwnd = nullptr;
        UnregisterClassW(kWindowClass, wc.hInstance);
    }
};

DriveWatchService::DriveWatchService() : impl_(new Impl()) {}

DriveWatchService::~DriveWatchService() {
    unsubscribe();
    delete impl_;
}

void DriveWatchService::subscribe(NotifyFn fn) {
    {
        std::lock_guard<std::mutex> lk(impl_->mutex);
        impl_->notify = std::move(fn);
    }
    if (!impl_->thread.joinable()) {
        impl_->stop.store(false);
        impl_->thread = std::thread([this] { impl_->run(); });
    }
}

void DriveWatchService::unsubscribe() {
    impl_->stop.store(true);
    if (impl_->hwnd) {
        PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0);
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    impl_->stop.store(false);
    std::lock_guard<std::mutex> lk(impl_->mutex);
    impl_->notify = nullptr;
}

} // namespace rpi_imager::writer
