#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2025 Raspberry Pi Ltd
"""
Instrument a copy of the QML tree so a test run can report which of it ran.

There is no coverage tool for QML. gcov cannot see it -- qmlcachegen emits
bytecode as a byte array with no line mapping, so the .cpp it generates is
instrumented and tells you nothing -- and the AOT compiler that would emit
real C++ (qmlsc) is not in the open-source Qt. So 19,000 lines of the part
users actually touch were not merely uncovered, they were unmeasured.

This injects a counter into every function body and block-bodied signal
handler in a *copy* of the tree. The shipping QML is never touched: the QML
test harness already copies the module somewhere writable and rewrites its
qmldir, and this instruments that copy.

Injection is inline, immediately after the opening brace, so line numbers do
not move and a stack trace from an instrumented run still points at the right
line of the original file.

What counts is a function body or a block-bodied signal handler, not a
statement. Statement-level instrumentation would need far more of the
grammar understood, and a coverage number that is wrong is worse than no
coverage number at all.

The known blind spot, so the number is read for what it is: an expression
handler -- `onClicked: doThing()` -- has nowhere to put a probe without
rewriting it into a block, and finding where a multi-line expression ends
needs the parser this deliberately avoids. At the time of writing the tree
has 298 block-bodied handlers and functions, which are counted, and 117
expression handlers, which are not. So the denominator is the logic that
lives in bodies, and a file showing 100% may still have untried one-liners.
"""

import argparse
import json
import os
import re
import shutil
import sys

# `on` followed by an upper-case letter, then an identifier, then a colon.
# `onclick:` or `only:` must not match, so the third character is anchored.
HANDLER = re.compile(r'\bon[A-Z]\w*\s*:')
FUNCTION = re.compile(r'\bfunction\s+(\w+)\s*\(')


def scan(text):
    """Yield (index, kind, name) for each injectable site.

    A hand-rolled scanner rather than a regex sweep over the whole file: QML
    is full of strings containing braces and comments containing code, and a
    probe injected inside either produces a file that no longer parses.
    """
    sites = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]

        # Comments and strings are skipped wholesale; nothing inside them is
        # a declaration however much it looks like one.
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j + 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if c in '"\'`':
            quote = c
            i += 1
            while i < n:
                if text[i] == '\\':
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue

        # A declaration is only a site if its body is a block. `onFoo: bar()`
        # is a single expression with nowhere to put a probe.
        m = FUNCTION.match(text, i)
        if m:
            brace = find_body_brace(text, m.end() - 1)
            if brace is not None:
                sites.append((brace, 'function', m.group(1)))
            i = m.end()
            continue

        m = HANDLER.match(text, i)
        if m:
            after = skip_space(text, m.end())
            if after < n and text[after] == '{':
                sites.append((after, 'handler', m.group(0).rstrip(': \t')))
            i = m.end()
            continue

        i += 1
    return sites


def skip_space(text, i):
    while i < len(text) and text[i] in ' \t\r\n':
        i += 1
    return i


def find_body_brace(text, open_paren):
    """From a parameter list's '(', find the '{' that opens the body."""
    depth = 0
    i, n = open_paren, len(text)
    while i < n:
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                j = skip_space(text, i + 1)
                # A return type annotation may sit between the two.
                if j < n and text[j] == ':':
                    j = skip_space(text, j + 1)
                    while j < n and (text[j].isalnum() or text[j] in '_.<>'):
                        j += 1
                    j = skip_space(text, j)
                return j if j < n and text[j] == '{' else None
        i += 1
    return None


def instrument_file(src_path, rel_path, next_id, probes):
    with open(src_path, 'r', encoding='utf-8') as f:
        text = f.read()

    sites = scan(text)
    if not sites:
        return text, next_id

    # Apply back to front so earlier offsets stay valid.
    out = text
    for offset, kind, name in sorted(sites, key=lambda s: -s[0]):
        probe_id = next_id + len([p for p in sites if p[0] < offset])
        line = text.count('\n', 0, offset) + 1
        out = out[:offset + 1] + f' __qmlcov.hit({probe_id});' + out[offset + 1:]
        probes.append({'id': probe_id, 'file': rel_path, 'line': line,
                       'kind': kind, 'name': name})
    return out, next_id + len(sites)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('source')
    ap.add_argument('dest')
    ap.add_argument('--inventory', required=True)
    args = ap.parse_args()

    if os.path.exists(args.dest):
        shutil.rmtree(args.dest)
    shutil.copytree(args.source, args.dest)

    probes = []
    next_id = 0
    for root, _dirs, files in os.walk(args.dest):
        for name in sorted(files):
            if not name.endswith('.qml'):
                continue
            path = os.path.join(root, name)
            rel = os.path.relpath(path, args.dest)
            out, next_id = instrument_file(path, rel, next_id, probes)
            with open(path, 'w', encoding='utf-8') as f:
                f.write(out)

    probes.sort(key=lambda p: p['id'])
    with open(args.inventory, 'w', encoding='utf-8') as f:
        json.dump({'probes': probes}, f, indent=1)

    files = len({p['file'] for p in probes})
    print(f'instrumented {len(probes)} sites across {files} files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
