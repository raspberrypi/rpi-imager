/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef PLATFORMHELPER_H
#define PLATFORMHELPER_H

#include <QObject>
#ifndef CLI_ONLY_BUILD
#include <QQmlEngine>
#endif

/**
 * @brief QML-accessible wrapper for platform-specific functionality
 * 
 * This class exposes PlatformQuirks functionality to QML in a way that
 * follows Qt/QML conventions (Q_INVOKABLE methods, signals, etc.)
 */
class PlatformHelper : public QObject
{
    Q_OBJECT
#ifndef CLI_ONLY_BUILD
    QML_ELEMENT
    QML_SINGLETON
#endif

public:
    explicit PlatformHelper(QObject *parent = nullptr) : QObject(parent) {}
    
    /**
     * @brief Check if the system has network connectivity
     * 
     * Uses platform-specific APIs to determine if network access is available.
     * This is a lightweight check that doesn't make actual network requests.
     * 
     * @return true if network connectivity is available, false otherwise
     */
    Q_INVOKABLE bool hasNetworkConnectivity() const;
    
    /**
     * @brief Play a system beep sound
     * 
     * Uses platform-specific audio APIs to play a system beep.
     */
    Q_INVOKABLE void beep() const;
    
    /**
     * @brief Determine if scroll direction should be inverted for natural scrolling
     * 
     * Encapsulates platform-specific scroll direction detection:
     * - On Windows: Reads from registry (Qt doesn't report this correctly)
     * - On macOS/Linux: Passes through Qt's inverted flag
     * 
     * @param qtInvertedFlag The inverted flag from Qt's WheelEvent
     * @return true if scroll direction should be inverted (natural scrolling)
     */
    Q_INVOKABLE bool isScrollInverted(bool qtInvertedFlag) const;
};

#endif // PLATFORMHELPER_H

