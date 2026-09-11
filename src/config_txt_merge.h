#ifndef CONFIG_TXT_MERGE_H
#define CONFIG_TXT_MERGE_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include <QByteArray>
#include <QList>

/**
 * @brief Merge one setting into a config.txt, matching whole lines.
 *
 * Three outcomes, in order:
 *   - the setting is already there as its own line, and nothing changes;
 *   - it is there commented out, and that line is uncommented in place;
 *   - it is not there, and it is appended.
 */
inline QByteArray mergeConfigTxtItem(const QByteArray &config, const QByteArray &item)
{
    if (item.isEmpty())
        return config;

    const QList<QByteArray> lines = config.split('\n');
    const QByteArray commented = QByteArray("#") + item;

    auto stripped = [](const QByteArray &line) {
        return line.endsWith('\r') ? line.left(line.size() - 1) : line;
    };

    int commentedAt = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QByteArray line = stripped(lines[i]);
        if (line == item)
            return config;                 // already set; leave the file alone
        if (commentedAt < 0 && line == commented)
            commentedAt = i;
    }

    if (commentedAt >= 0) {
        QList<QByteArray> out = lines;
        // Keep whatever line ending the file was using.
        out[commentedAt] = lines[commentedAt].endsWith('\r') ? item + '\r' : item;
        return out.join('\n');
    }

    QByteArray result = config;
    if (!result.isEmpty() && !result.endsWith('\n'))
        result += '\n';
    result += item;
    result += '\n';
    return result;
}

#endif // CONFIG_TXT_MERGE_H
