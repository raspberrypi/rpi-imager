/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "elevatedwriteprocess.h"
#include "platformquirks.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDebug>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

ElevatedWriteProcess::ElevatedWriteProcess(QObject *parent)
    : QObject(parent), _process(nullptr)
{
}

ElevatedWriteProcess::~ElevatedWriteProcess()
{
    cancel();
}

QString ElevatedWriteProcess::getExecutablePath() const
{
#ifdef Q_OS_LINUX
    // Check if running from AppImage
    QByteArray appImagePath = qgetenv("APPIMAGE");
    if (!appImagePath.isEmpty()) {
        return QString::fromUtf8(appImagePath);
    }
#endif
    
    // Default to the application's executable path
    return QCoreApplication::applicationFilePath();
}

bool ElevatedWriteProcess::start(const QJsonObject &writeSpec)
{
    if (_process && _process->state() != QProcess::NotRunning) {
        qWarning() << "ElevatedWriteProcess: Already running";
        return false;
    }
    
    // Clean up any previous process
    if (_process) {
        _process->deleteLater();
    }
    
    // Write spec to temp file
    _specFile.reset(new QTemporaryFile());
    _specFile->setAutoRemove(true);
    if (!_specFile->open()) {
        emit error(QVariant(QString("Failed to create temp file for write spec")));
        return false;
    }
    
    QJsonDocument doc(writeSpec);
    _specFile->write(doc.toJson(QJsonDocument::Compact));
    _specFile->close();
    
    qDebug() << "Wrote write-spec to:" << _specFile->fileName();
    
    _process = new QProcess(this);
    _stdoutBuffer.clear();
    _errorEmitted = false;
    
    // Connect signals
    connect(_process, &QProcess::readyReadStandardOutput, 
            this, &ElevatedWriteProcess::onReadyReadStdout);
    connect(_process, &QProcess::readyReadStandardError, 
            this, &ElevatedWriteProcess::onReadyReadStderr);
    connect(_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ElevatedWriteProcess::onProcessFinished);
    connect(_process, &QProcess::errorOccurred,
            this, &ElevatedWriteProcess::onProcessError);
    
    // Build the command: pkexec rpi-imager --cli --json-output --write-spec <file>
    QString executable = getExecutablePath();
    
    QStringList arguments;
#ifdef Q_OS_LINUX
    arguments << "pkexec" << "--disable-internal-agent";
#endif
    arguments << executable << "--cli" << "--json-output";
    arguments << "--write-spec" << _specFile->fileName();
    
    QString program = arguments.takeFirst();
    
    qDebug() << "Starting elevated CLI process:" << program << arguments;
    
    // Start the process asynchronously (don't block UI)
    _process->start(program, arguments);
    
    // Check if start initiated (actual start happens asynchronously)
    if (_process->state() == QProcess::NotRunning && _process->error() != QProcess::UnknownError) {
        QString errorMsg = _process->errorString();
        qWarning() << "Failed to start elevated write process:" << errorMsg;
        emit error(QVariant(QString("Failed to start elevated process: %1").arg(errorMsg)));
        return false;
    }
    
    return true;
}

void ElevatedWriteProcess::cancel()
{
    if (_process && _process->state() != QProcess::NotRunning) {
        qDebug() << "Cancelling elevated write process";
        
        // Try graceful termination first
        _process->terminate();
        
        if (!_process->waitForFinished(3000)) {
            // Force kill if it doesn't respond
            _process->kill();
            _process->waitForFinished(1000);
        }
        
        emit cancelled();
    }
}

bool ElevatedWriteProcess::isRunning() const
{
    return _process && _process->state() != QProcess::NotRunning;
}

void ElevatedWriteProcess::onReadyReadStdout()
{
    _stdoutBuffer.append(_process->readAllStandardOutput());
    
    // Process complete JSON lines
    int newlinePos;
    while ((newlinePos = _stdoutBuffer.indexOf('\n')) != -1) {
        QByteArray line = _stdoutBuffer.left(newlinePos);
        _stdoutBuffer.remove(0, newlinePos + 1);
        
        if (line.isEmpty()) continue;
        
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "Failed to parse elevated process output:" << parseError.errorString();
            qWarning() << "Line was:" << line;
            continue;
        }
        
        if (doc.isObject()) {
            handleMessage(doc.object());
        }
    }
}

void ElevatedWriteProcess::onReadyReadStderr()
{
    // Log stderr for debugging
    QByteArray stderrData = _process->readAllStandardError();
    if (!stderrData.isEmpty()) {
        qDebug() << "Elevated process stderr:" << stderrData;
    }
}

void ElevatedWriteProcess::handleMessage(const QJsonObject &msg)
{
    QString type = msg["type"].toString();
    
    if (type == "status") {
        emit preparationStatusUpdate(QVariant(msg["message"].toString()));
    }
    else if (type == "download_progress") {
        qint64 now = msg["now"].toVariant().toLongLong();
        qint64 total = msg["total"].toVariant().toLongLong();
        emit downloadProgress(QVariant(now), QVariant(total));
    }
    else if (type == "verify_progress") {
        qint64 now = msg["now"].toVariant().toLongLong();
        qint64 total = msg["total"].toVariant().toLongLong();
        emit verifyProgress(QVariant(now), QVariant(total));
    }
    else if (type == "error") {
        _errorEmitted = true;
        emit error(QVariant(msg["message"].toString()));
    }
    else if (type == "success") {
        emit success();
    }
    else {
        qWarning() << "Unknown message type from elevated process:" << type;
    }
}

void ElevatedWriteProcess::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "Elevated write process finished with exit code:" << exitCode 
             << "status:" << exitStatus;
    
    // Process any remaining stdout data
    if (!_stdoutBuffer.isEmpty()) {
        onReadyReadStdout();
    }
    
    // If process crashed or exited with error without sending error message
    if (exitStatus == QProcess::CrashExit && !_errorEmitted) {
        _errorEmitted = true;
        emit error(QVariant(QString("Elevated write process crashed")));
    }
    else if (exitCode != 0 && !_errorEmitted) {
        // CLI should have sent an error message, but emit generic one as fallback
        _errorEmitted = true;
        emit error(QVariant(QString("Write failed with exit code %1").arg(exitCode)));
    }
}

void ElevatedWriteProcess::onProcessError(QProcess::ProcessError processError)
{
    QString errorMsg;
    
    switch (processError) {
    case QProcess::FailedToStart:
        errorMsg = "Failed to start elevated process. Make sure pkexec is installed.";
        break;
    case QProcess::Crashed:
        errorMsg = "Elevated process crashed.";
        break;
    case QProcess::Timedout:
        errorMsg = "Elevated process timed out.";
        break;
    case QProcess::WriteError:
        errorMsg = "Failed to write to elevated process.";
        break;
    case QProcess::ReadError:
        errorMsg = "Failed to read from elevated process.";
        break;
    default:
        errorMsg = QString("Elevated process error: %1").arg(_process->errorString());
        break;
    }
    
    qWarning() << "Elevated write process error:" << errorMsg;
    emit error(QVariant(errorMsg));
}
