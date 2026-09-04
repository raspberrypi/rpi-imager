# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# Drives a coverage run for the `coverage` target: clears stale counters, runs
# the CTest suite, and renders a gcovr HTML report scoped to the headless core.
#
# Run via `cmake -P`, not included. Expects COVERAGE_BINARY_DIR,
# COVERAGE_SOURCE_DIR, COVERAGE_OUTPUT_DIR, GCOVR_EXECUTABLE and
# CTEST_EXECUTABLE to be passed with -D.
#
# This lives in a script rather than inline COMMANDs so the suite failing does
# not abort the run -- a report of what a failing suite covered is still worth
# having, and it is usually the thing you want to look at.

foreach(_required
        COVERAGE_BINARY_DIR COVERAGE_SOURCE_DIR COVERAGE_OUTPUT_DIR
        GCOVR_EXECUTABLE CTEST_EXECUTABLE)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "RunCoverage.cmake: -D${_required} is required")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Scope
# ---------------------------------------------------------------------------
# Exclusion-based rather than an allow-list of files, so a new core source is
# in scope the day it is written. Being absent from a gap report is the failure
# mode that matters; appearing in one and being uninteresting is cheap.
#
# What is excluded is the QML/Qt UI layer -- not because it is unimportant, but
# because no unit test can reach it, so its 0% rows are noise that buries the
# rows you can act on. The GUI is covered by the embedded scaling matrix and by
# screenshots.sh instead.
#
# "Cannot be reached" is a claim about the code as it stands, though, not a
# licence to leave decisions in there. Logic that happens to live behind the
# UI but does not need it belongs on this side of the line, and two pieces
# have been moved out for exactly that reason: oslistparser.cpp (which
# entries the OS chooser shows, in which language and order) and
# imagesizeparser.cpp (whether an image is judged to fit the chosen card).
# Both were unreachable only because they sat in an anonymous namespace or a
# private member of a QML-facing class. If something else in here turns out
# to decide what the user sees, prefer lifting it out to widening this list.
#
# Deliberately still IN scope, and expected to report low: downloadthread.cpp
# and the rest of the download/cache path. Nothing links them into a test
# binary today, so they report at 0% via their .gcno alone. That row is the
# point of the exercise, not a defect in it.
set(_exclude
    # The test tree itself, vendored code and generated Qt sources.
    ".*/test/.*"
    # Two test sources live outside test/ -- src/fat_partition_test.cpp and
    # src/drivelist/drivelist_test.cpp -- so the directory rule above misses
    # them and they were being reported as core. fat_partition_test.cpp is
    # tagged [.destructive] and never runs, which made 1,114 never-executed
    # branches of *test* code the second largest block of red in a report
    # about the headless core. Match on the name too.
    ".*_test\\.(cpp|h)"
    ".*/dependencies/.*"
    ".*_autogen/.*"
    ".*/moc_.*"
    ".*/qrc_.*"
    ".*/ui_.*"
    ".*/build[^/]*/.*"
    # Entry point and the QML-facing backend object.
    ".*/src/main\\.cpp"
    ".*/src/imagewriter\\.(cpp|h)"
    ".*/src/cli\\.(cpp|h)"
    # Qt item models and view glue: constructed by the QML engine, and
    # meaningless without one.
    ".*/src/drivelistmodel.*"
    ".*/src/drivelistitem.*"
    ".*/src/oslistmodel.*"
    ".*/src/hwlistmodel.*"
    ".*/src/imageadvancedoptions.*"
    # Presentation helpers.
    ".*/src/iconimageprovider.*"
    ".*/src/iconmultifetcher.*"
    ".*/src/clipboardhelper.*"
    ".*/src/nativefiledialog.*"
    # ...and its per-platform halves, which live a directory down and so are
    # missed by the rule above. linux/nativefiledialog_linux.cpp is 210
    # branches of portal and GTK dialog plumbing -- the same UI layer the rule
    # above exists to exclude, reachable only with a desktop session.
    ".*/src/(linux|mac|windows)/nativefiledialog.*"
    ".*/src/platformhelper.*"
)

set(_exclude_args)
foreach(_pattern IN LISTS _exclude)
    list(APPEND _exclude_args --exclude "${_pattern}")
endforeach()

# ---------------------------------------------------------------------------
# 0. Build everything we can
# ---------------------------------------------------------------------------
# Best-effort on purpose. The report wants a .gcno for every instrumented
# source, which means building the app and every test binary -- but a single
# unbuildable target must not deny you a report on the other ninety-odd
# sources, so this deliberately does not express the targets as CMake
# DEPENDS, which would make any single failure fatal to the whole run.
#
# (This used to name rpiboot_integration_test as a target that would not
# link. It does now -- the missing sources were added to it -- so there is no
# longer a known-unbuildable target. The best-effort handling stays anyway:
# the next one should cost a row in the report, not the whole report.)
#
# Anything that fails to build simply has no .gcno and is absent from the
# report, which the warning below calls out so it cannot be mistaken for 0%.
# Clear any log from a previous run before building. It is written below
# only when something actually fails, so leaving a stale one in place puts a
# build-failures.log next to a clean report -- which reads as though this run
# had failures, and sends the reader chasing a target that already builds.
file(MAKE_DIRECTORY "${COVERAGE_OUTPUT_DIR}")
file(REMOVE "${COVERAGE_OUTPUT_DIR}/build-failures.log")

message(STATUS "Coverage: building instrumented targets")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${COVERAGE_BINARY_DIR}" --parallel
    WORKING_DIRECTORY "${COVERAGE_BINARY_DIR}"
    RESULT_VARIABLE _build_result
    OUTPUT_VARIABLE _build_output
    ERROR_VARIABLE _build_output
)
if(NOT _build_result EQUAL 0)
    # Name the targets rather than telling the reader to go and find them.
    # The build output is captured either way, so "re-run the build on its own
    # to see which" was asking for a second full build to recover something we
    # were already holding. Ninja and Make both announce a failed target on a
    # "FAILED:" line.
    file(MAKE_DIRECTORY "${COVERAGE_OUTPUT_DIR}")
    set(_build_log "${COVERAGE_OUTPUT_DIR}/build-failures.log")
    file(WRITE "${_build_log}" "${_build_output}")

    set(_failed_targets)
    string(REGEX MATCHALL "FAILED:[^\n]*" _failed_lines "${_build_output}")
    foreach(_line IN LISTS _failed_lines)
        string(REGEX REPLACE "^FAILED: +" "" _line "${_line}")
        # A Ninja FAILED: line lists every output of the edge; the first is
        # the one worth naming.
        string(REGEX REPLACE " .*$" "" _line "${_line}")
        list(APPEND _failed_targets "${_line}")
    endforeach()
    list(REMOVE_DUPLICATES _failed_targets)
    list(JOIN _failed_targets "\n    " _failed_pretty)

    if(_failed_targets)
        message(WARNING
            "Coverage: not everything built (exit ${_build_result}). These targets failed:\n"
            "    ${_failed_pretty}\n"
            "Their own sources are absent from the report entirely, having no .gcno. Treat "
            "the rest of the numbers with suspicion too: the suite still runs, but against "
            "whatever binaries the partial build left behind, and a binary older than its "
            "own .gcno makes gcov reject the run with \"stamp mismatch with notes file\" -- "
            "which reports a covered file at a confident 0%. Full build output: ${_build_log}")
    else()
        message(WARNING
            "Coverage: not everything built (exit ${_build_result}), and no FAILED: line "
            "was found to say which target. Full build output: ${_build_log}")
    endif()
endif()

# ---------------------------------------------------------------------------
# 1. Clear counters from any previous run
# ---------------------------------------------------------------------------
# .gcda files accumulate across runs, so without this the report describes
# every run since the build directory was created rather than this one.
file(GLOB_RECURSE _stale "${COVERAGE_BINARY_DIR}/*.gcda")
list(LENGTH _stale _stale_count)
if(_stale_count GREATER 0)
    message(STATUS "Coverage: clearing ${_stale_count} counter file(s) from a previous run")
    file(REMOVE ${_stale})
endif()

# ---------------------------------------------------------------------------
# 2. Run the suite
# ---------------------------------------------------------------------------
message(STATUS "Coverage: running the CTest suite")
execute_process(
    COMMAND "${CTEST_EXECUTABLE}" --output-on-failure
    WORKING_DIRECTORY "${COVERAGE_BINARY_DIR}"
    RESULT_VARIABLE _ctest_result
)
if(NOT _ctest_result EQUAL 0)
    message(WARNING
        "Coverage: the suite exited ${_ctest_result}. Reporting anyway -- but read the "
        "numbers knowing some cases did not finish.")
endif()

# ---------------------------------------------------------------------------
# 3. Render
# ---------------------------------------------------------------------------
file(MAKE_DIRECTORY "${COVERAGE_OUTPUT_DIR}")

message(STATUS "Coverage: rendering report")
execute_process(
    COMMAND "${GCOVR_EXECUTABLE}"
            --root "${COVERAGE_SOURCE_DIR}"
            "${COVERAGE_BINARY_DIR}"
            ${_exclude_args}
            # Branch coverage is the reason for doing this at all: line
            # coverage would have called runWithTimeout's timeout path
            # "reached" as soon as anything entered the loop.
            --txt-metric branch
            --decisions
            # Without these two a branch-metric report on C++ is unreadable.
            # Every call that might throw carries a branch pair for the unwind
            # edge, and the unwind arm is never taken in a passing run, so
            # gcov reports it half-covered forever. It is not a gap anyone can
            # close -- you would have to make the allocation fail.
            #
            # Not a small correction, either: they were 10,598 of the 28,284
            # branches here, and removing them takes the headline from 13.8%
            # to 21.5% without a line of test code changing. What they cost in
            # legibility is worse than what they cost in percentage points --
            # lines like `out += ":";` were being painted as partially
            # covered, and those lines contain no branch at all.
            --exclude-throw-branches
            --exclude-unreachable-branches
            # Sort by uncovered *branches*, not uncovered lines: without
            # --sort-branches the table is ordered by a metric it does not
            # display. --sort-reverse because gcovr sorts ascending, which for
            # a gap report is exactly backwards -- it opened on 27 rows of
            # header files with no branches at all ("--%"), and put
            # downloadthread.cpp, the single biggest gap in the tree at 1,958
            # uncovered branches, last in an 89-row table.
            --sort uncovered-number
            --sort-branches
            --sort-reverse
            # Left on gcovr's default theme deliberately. The `github.*` themes
            # look considerably more modern, but all four of them render a
            # partially covered line in near enough the same colour as a fully
            # covered one -- and a partially covered line is the single thing
            # this report exists to show. The default theme's green/yellow/red
            # are ugly and unambiguous, in that order of importance.
            --html-details "${COVERAGE_OUTPUT_DIR}/index.html"
            --html-title "rpi-imager core coverage"
            --txt "${COVERAGE_OUTPUT_DIR}/summary.txt"
            --print-summary
            # The --exclude patterns above decide what reaches the report, but
            # gcovr processes every .gcda it finds before applying them, and
            # instrumentation is global -- so it walked into every FetchContent
            # dependency, 492 of the 508 .gcno files here. gcov cannot resolve
            # their sources from our build tree, and each failure printed: it
            # was 10,461 lines of stderr on a run whose real output is four
            # numbers. Pruning the walk removes the noise and the work both.
            #
            # The pattern is matched against the directory name rather than
            # the full path, so it takes no leading slash -- ".*/_deps.*"
            # matches nothing at all, silently.
            # Only _deps. Pruning ".*dependencies.*" as well looks tempting --
            # the vendored crypto built in-tree still emits a couple of dozen
            # gcov warnings -- but the pattern is matched against the path
            # relative to the search root, and the test binaries compile that
            # crypto through object directories of their own. It took 1,192
            # covered branches of customization_generator.cpp out of the
            # report along with the warnings. Twenty-two lines of noise is the
            # cheaper of the two.
            --gcov-exclude-directories ".*_deps.*"
            # Kept as a backstop only. This suppresses the "could not infer a
            # working directory" failure, which before the pruning above was
            # not an edge case but the single loudest thing in the run -- and
            # fatal without it. Nothing in our own tree provokes it now.
            --gcov-ignore-errors no_working_dir_found
    WORKING_DIRECTORY "${COVERAGE_BINARY_DIR}"
    RESULT_VARIABLE _gcovr_result
)
if(NOT _gcovr_result EQUAL 0)
    message(FATAL_ERROR "Coverage: gcovr failed (${_gcovr_result})")
endif()

# ---------------------------------------------------------------------------
# 4. Make summary.txt fit on a screen
# ---------------------------------------------------------------------------
# gcovr's text report ends each row with every uncovered line number, and has
# no option to shorten it. For a file at 0% that is the whole file: the
# downloadthread.cpp row alone was 3,902 characters, 33 rows ran past 200, and
# the "summary" came to 29KB. Sorting worst-first made it worse, because the
# longest rows are now the first ones you see.
#
# So cap the list. Anything past the cap is a count, not a number, and the
# full detail is a click away in the HTML. A line that does not match the row
# shape -- headers, rules, the TOTAL row -- is passed through untouched, so a
# change to gcovr's format degrades to "no truncation" rather than mangling.
set(_missing_cap 12)

file(READ "${COVERAGE_OUTPUT_DIR}/summary.txt" _summary)
string(REPLACE ";" "\\;" _summary "${_summary}")
string(REPLACE "\n" ";" _summary_lines "${_summary}")

set(_summary_out "")
foreach(_line IN LISTS _summary_lines)
    if(_line MATCHES "^(.+%[ \t]+)([0-9]+(,[0-9]+)*)$")
        set(_row_head "${CMAKE_MATCH_1}")
        string(REPLACE "," ";" _missing "${CMAKE_MATCH_2}")
        list(LENGTH _missing _missing_count)
        if(_missing_count GREATER _missing_cap)
            list(SUBLIST _missing 0 ${_missing_cap} _missing_kept)
            list(JOIN _missing_kept "," _missing_kept)
            math(EXPR _missing_rest "${_missing_count} - ${_missing_cap}")
            set(_line "${_row_head}${_missing_kept} (+${_missing_rest} more)")
        endif()
    endif()
    string(APPEND _summary_out "${_line}\n")
endforeach()
file(WRITE "${COVERAGE_OUTPUT_DIR}/summary.txt" "${_summary_out}")

message(STATUS "Coverage: HTML   ${COVERAGE_OUTPUT_DIR}/index.html")
message(STATUS "Coverage: text   ${COVERAGE_OUTPUT_DIR}/summary.txt")
