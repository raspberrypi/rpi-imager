#!/usr/bin/env python3
"""Find assertions that sit inside an `if`, and so may never run.

An assertion that never runs is invisible: the suite is green either way.
Catch2's `-w NoAssertions` catches a case where *no* assertion ran, which
is the other half of the problem -- it cannot see a case that has some
unconditional assertions and one gated behind a condition that never
holds. That is what this finds.

Five of those were found and fixed on 13 September 2026:

    tst_chaos_customisation  guard was a running maximum in
                             cleanupTestCase(), so one case satisfied it
                             for forty-two
    tst_chaos_dialogs        nothing required a dialog to open, and the
                             modality check lives behind that
    tst_chaos_dialogs        the safe-default-button check ran only for
                             cases carrying understandingOnly, two in
                             seven, with nothing requiring any
    image_writer_test        `if (haveOpensslBinary())` where its two
                             siblings SKIP: without openssl it checked
                             less and still reported a pass
    download_thread_test     the digest check sat behind a test for the
                             digest being present, so it was skipped
                             exactly when it mattered

Not every hit is a defect. A deterministic gate is usually fine -- the
ordered QML tests came back clean because they use if/else with
assertions in *both* branches, so no path asserts nothing. A gate that
depends on a random walk, an environment, or a value the code under test
produced is the one to look at: it works until it quietly does not.

    tools/gated_assertions.py qml src/test/qml/tst_*.qml
    tools/gated_assertions.py cpp src/test/*.cpp

Brace depth is tracked rather than line proximity. Proximity confuses an
adjacent one-line `if` with an enclosing block, and reported three
assertions in tst_chaos_custom_image that are not gated at all.
"""

import re
import sys
import pathlib

QML = r'\b(verify|compare|fail|tryVerify)\s*\('
CPP = (r'\b(CHECK|REQUIRE|CHECK_FALSE|REQUIRE_FALSE|CHECK_THAT|REQUIRE_THAT'
       r'|CHECK_THROWS|CHECK_NOTHROW)\s*\(')


def analyse(path, assertion_re):
    lines = pathlib.Path(path).read_text(errors="replace").split("\n")
    stack = []            # one entry per open brace: True if an `if` opened it
    hits = []
    in_comment = False

    for number, raw in enumerate(lines, 1):
        line = raw
        if in_comment:
            if "*/" in line:
                line, in_comment = line.split("*/", 1)[1], False
            else:
                continue
        if "/*" in line:
            before, _, rest = line.partition("/*")
            if "*/" in rest:
                line = before + rest.split("*/", 1)[1]
            else:
                line, in_comment = before, True

        line = re.sub(r'//.*$', '', line)
        line = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)

        opened_by_if = bool(re.search(r'\b(if|else\s+if)\s*\(', line))
        asserts = bool(re.search(assertion_re, line))

        if asserts and any(stack):
            hits.append((number, raw.strip()[:96]))

        for ch in line:
            if ch == "{":
                stack.append(opened_by_if)
                opened_by_if = False
            elif ch == "}" and stack:
                stack.pop()

        # `if (...) assert;` with no braces at all
        if opened_by_if and asserts:
            hits.append((number, "[unbraced] " + raw.strip()[:86]))

    return hits


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("qml", "cpp"):
        print(__doc__)
        return 2

    pattern = QML if sys.argv[1] == "qml" else CPP
    total = 0
    for path in sys.argv[2:]:
        hits = analyse(path, pattern)
        total += len(hits)
        if not hits:
            continue
        print(f"{path}: {len(hits)}")
        for number, text in hits:
            print(f"    {number}: {text}")
    print(f"total: {total} assertion(s) inside a conditional")
    return 0


if __name__ == "__main__":
    sys.exit(main())
