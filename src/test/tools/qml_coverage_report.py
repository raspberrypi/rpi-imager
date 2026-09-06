#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2025 Raspberry Pi Ltd
"""Turn an instrumentation inventory and a run's hits into a report."""

import argparse
import json
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inventory')
    ap.add_argument('hits')
    ap.add_argument('--out')
    ap.add_argument('--min-width', type=int, default=44)
    args = ap.parse_args()

    probes = json.load(open(args.inventory))['probes']
    try:
        hits = {int(k): v for k, v in json.load(open(args.hits)).items()}
    except FileNotFoundError:
        hits = {}

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
