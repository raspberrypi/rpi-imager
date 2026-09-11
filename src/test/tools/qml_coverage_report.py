#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
"""Turn an instrumentation inventory and one or more runs' hits into a report.

More than one hits file because the suite is run more than once. A QtQuickTest
run shares one ImageWriter across every file in it, so a couple of files cover
one half of their subject in the whole-suite run and the other half only when
run on their own -- which is why CTest has a second entry for
tst_device_selection_step. Reporting the whole-suite run alone counts the
cases in that second entry as never having run, and the sites they cover as
never reached.
"""

import argparse
import json
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inventory')
    ap.add_argument('hits', nargs='+')
    ap.add_argument('--out')
    ap.add_argument('--min-width', type=int, default=44)
    args = ap.parse_args()

    probes = json.load(open(args.inventory))['probes']
    hits = {}
    for path in args.hits:
        try:
            counts = json.load(open(path))
        except FileNotFoundError:
            continue
        for k, v in counts.items():
            key = int(k)
            hits[key] = hits.get(key, 0) + v

    per_file = {}
    for p in probes:
        total, run = per_file.setdefault(p['file'], [0, 0])
        per_file[p['file']] = [total + 1, run + (1 if hits.get(p['id']) else 0)]

    rows = sorted(per_file.items(), key=lambda kv: (kv[1][0] - kv[1][1]), reverse=True)
    width = max(args.min_width, max((len(f) for f in per_file), default=0))

    lines = []
    lines.append('-' * (width + 26))
    lines.append('QML coverage: functions and block-bodied signal handlers'.center(width + 26))
    lines.append('-' * (width + 26))
    lines.append(f'{"File".ljust(width)} {"Sites":>6} {"Ran":>5} {"Cover":>6}')
    lines.append('-' * (width + 26))
    for name, (total, run) in rows:
        pct = f'{100 * run // total}%' if total else '--'
        lines.append(f'{name.ljust(width)} {total:>6} {run:>5} {pct:>6}')
    lines.append('-' * (width + 26))
    total = sum(t for t, _ in per_file.values())
    run = sum(r for _, r in per_file.values())
    pct = f'{100 * run // total}%' if total else '--'
    lines.append(f'{"TOTAL".ljust(width)} {total:>6} {run:>5} {pct:>6}')
    lines.append('-' * (width + 26))

    text = '\n'.join(lines)
    print(text)
    if args.out:
        with open(args.out, 'w', encoding='utf-8') as f:
            f.write(text + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
