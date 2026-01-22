/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef ELEVATEDWRITEPROCESS_H
#define ELEVATEDWRITEPROCESS_H

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTemporaryFile>
#include <QJsonObject>
#include <memory>

/**
 * @brief Client-side handler for elevated write operations
 * 
 * This class spawns the CLI mode with elevated privileges (via pkexec on Linux)
 * to perform disk write operations. It uses --json-output to receive progress
 * updates from the CLI process.
 * 
 * This allows the main UI to remain unprivileged (enabling D-Bus registration
 * for Pi Connect callbacks) while disk writes run elevated.
 */
class ElevatedWriteProcess : public QObject
{
    Q_OBJECT
public:
    explicit ElevatedWriteProcess(QObject *parent = nullptr);
    virtual ~ElevatedWriteProcess();
    
    /**
     * @brief Start an elevated write operation using CLI mode
     * @param writeSpec JSON object containing all write parameters
     * @return true if process started successfully
     */
    bool start(const QJsonObject &writeSpec);
    
    /**
     * @brief Cancel the current write operation
     */
    void cancel();
    
    /**
     * @brief Check if a write operation is in progress
     */
    bool isRunning() const;

signals:
    // Signals matching ImageWriter's interface
    void downloadProgress(QVariant dlnow, QVariant dltotal);
    void verifyProgress(QVariant now, QVariant total);
    void preparationStatusUpdate(QVariant msg);
    void error(QVariant msg);
    void success();
    void cancelled();

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    QProcess *_process;
    QByteArray _stdoutBuffer;  // Buffer for incomplete JSON lines
    bool _errorEmitted;        // Track if error was already emitted
    std::unique_ptr<QTemporaryFile> _specFile;  // Temp file for write spec
    
    /**
     * @brief Parse a JSON message from the CLI process
     */
    void handleMessage(const QJsonObject &msg);
    
    /**
     * @brief Get the path to the rpi-imager executable
     */
    QString getExecutablePath() const;
};

#endif // ELEVATEDWRITEPROCESS_H
