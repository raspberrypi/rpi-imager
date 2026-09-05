/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "platformhelper.h"
#include "platformquirks.h"

#include <QAccessible>
#include <QPointer>

namespace {

// One observer per process, forwarding to every PlatformHelper that has
// asked. QAccessible::installActivationObserver takes a bare pointer and
// keeps it, so this outlives any particular helper.
class AccessibilityWatcher : public QAccessible::ActivationObserver
{
public:
    static AccessibilityWatcher &instance()
    {
        static AccessibilityWatcher watcher;
        return watcher;
    }

    void watch(PlatformHelper *helper)
    {
        if (!_installed) {
            QAccessible::installActivationObserver(this);
            _installed = true;
        }
        for (const auto &known : _helpers) {
            if (known == helper)
                return;
        }
        _helpers.append(helper);
    }

    void accessibilityActiveChanged(bool) override
    {
        // QPointer: the singleton outlives the helpers, and a QML engine
        // teardown destroys them without telling us.
        _helpers.removeIf([](const QPointer<PlatformHelper> &h) { return h.isNull(); });
        for (const auto &helper : _helpers) {
            if (helper)
                emit helper->assistiveTechnologyActiveChanged();
        }
    }

private:
    QList<QPointer<PlatformHelper>> _helpers;
    bool _installed = false;
};

} // namespace

bool PlatformHelper::hasNetworkConnectivity() const
{
    return PlatformQuirks::hasNetworkConnectivity();
}

void PlatformHelper::beep() const
{
    PlatformQuirks::beep();
}

bool PlatformHelper::isScrollInverted(bool qtInvertedFlag) const
{
    return PlatformQuirks::isScrollInverted(qtInvertedFlag);
}

qreal PlatformHelper::textScaleFactor() const
{
    // Check for user override in settings first
    QSettings settings("Raspberry Pi", "Raspberry Pi Imager");
    QVariant override = settings.value("textScaleFactor");
    if (override.isValid()) {
        bool ok = false;
        qreal factor = override.toReal(&ok);
        if (ok && factor >= 0.5 && factor <= 3.0) {
            return factor;
        }
    }
    return PlatformQuirks::detectTextScaleFactor();
}

qreal PlatformHelper::fontDpiCorrection() const
{
    return PlatformQuirks::fontDpiCorrection();
}

bool PlatformHelper::prefersReducedMotion() const
{
    return PlatformQuirks::prefersReducedMotion();
}

void PlatformHelper::ensureAccessibilityObserver() const
{
    AccessibilityWatcher::instance().watch(const_cast<PlatformHelper *>(this));
}

bool PlatformHelper::assistiveTechnologyActive() const
{
    ensureAccessibilityObserver();
    return QAccessible::isActive();
}
