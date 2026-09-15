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
inline QByteArray mergeConfigTxtItem(const QByteArray &config, const QByteArray &rawItem)
{
    // Every trailing carriage return, not just one. A file written with
    // CRLF yields one; text that has been round-tripped through more than
    // one tool can carry several, and a line that differs from the item
    // only by them names the same setting.
    auto stripped = [](const QByteArray &line) {
        int end = line.size();
        while (end > 0 && line[end - 1] == '\r')
            --end;
        return line.left(end);
    };

    // The item is stripped the same way the lines are. A caller that split a
    // CRLF config on '\n' hands over items still carrying their carriage
    // return, and comparing those against stripped lines never matches -- so
    // the setting was appended again on every pass and config.txt grew a
    // duplicate line each time.
    const QByteArray item = stripped(rawItem);
    if (item.isEmpty())
        return config;

    const QList<QByteArray> lines = config.split('\n');
    const QByteArray commented = QByteArray("#") + item;

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
