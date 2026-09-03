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
# Deliberately still IN scope, and expected to report low: downloadthread.cpp
# and the rest of the download/cache path. Nothing links them into a test
# binary today, so they report at 0% via their .gcno alone. That row is the
# point of the exercise, not a defect in it.
set(_exclude
    # The test tree itself, vendored code and generated Qt sources.
    ".*/test/.*"
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
# sources. rpiboot_integration_test does not currently link, and expressing
# this as CMake DEPENDS made that one failure fatal to the whole run.
#
# Anything that fails to build simply has no .gcno and is absent from the
# report, which the warning below calls out so it cannot be mistaken for 0%.
message(STATUS "Coverage: building instrumented targets")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${COVERAGE_BINARY_DIR}" --parallel
    WORKING_DIRECTORY "${COVERAGE_BINARY_DIR}"
    RESULT_VARIABLE _build_result
    OUTPUT_QUIET
)
if(NOT _build_result EQUAL 0)
    message(WARNING
        "Coverage: not everything built (exit ${_build_result}). Continuing -- but any "
        "source whose target failed has no .gcno and will be MISSING from the report "
        "rather than reported at 0%. Re-run the build on its own to see which.")
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
            --sort uncovered-number
            --html-details "${COVERAGE_OUTPUT_DIR}/index.html"
            --html-title "rpi-imager core coverage"
            --txt "${COVERAGE_OUTPUT_DIR}/summary.txt"
            --print-summary
            # A source with a .gcno but no .gcda has been compiled and never
            # run. That is a real 0%, not an error.
            --gcov-ignore-errors no_working_dir_found
    WORKING_DIRECTORY "${COVERAGE_BINARY_DIR}"
    RESULT_VARIABLE _gcovr_result
)
if(NOT _gcovr_result EQUAL 0)
    message(FATAL_ERROR "Coverage: gcovr failed (${_gcovr_result})")
endif()

message(STATUS "Coverage: HTML   ${COVERAGE_OUTPUT_DIR}/index.html")
message(STATUS "Coverage: text   ${COVERAGE_OUTPUT_DIR}/summary.txt")
