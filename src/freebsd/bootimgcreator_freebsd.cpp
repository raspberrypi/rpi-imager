/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "bootimgcreator.h"
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QDebug>
#include <QSet>

bool BootImgCreator::createBootImg(const QMap<QString, QByteArray> &files,
                                   const QString &outputPath,
                                   qint64 totalSize)
{
    return false;
}
