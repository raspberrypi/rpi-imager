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
        COVERAGE_GENERATOR GCOVR_EXECUTABLE CTEST_EXECUTABLE)
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
# "Cannot be reached" is a claim about the code as it stands, though, and it
# was worth less than it looked. Two pieces of pure logic were lifted out of
# QML-facing files because they never needed the UI at all: oslistparser.cpp
# (which entries the OS chooser shows, in which language and order) and
# imagesizeparser.cpp (whether an image is judged to fit the chosen card).
#
# The rest of the claim then failed too. ImageWriter builds its models as
# members and leaves the QML engine null, so it needs no engine -- only a
# QGuiApplication, which the offscreen platform provides. It, the picker
# models and the icon fetcher are all linked and driven by
# image_writer_test, so they are no longer excluded. What remains below is
# the part that genuinely wants a desktop session: the native file dialogs,
# the clipboard, and the platform helper.
#
# The headline percentage fell when they came in, which is the honest
# direction: several thousand branches of behaviour the user meets directly
# were being left out of the denominator.
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
    # Entry point. main.cpp is argument dispatch and Qt setup with nothing
    # to decide.
    #
    # cli.cpp is not excluded with it, though it was. It is the front end a
    # script drives, and it has decisions in it -- which source it was given,
    # whether the destination is removable, whether a customisation file can
    # be read, whether the cache options make sense. Those were split into
    # statics precisely so they could be tested, and they are; excluding the
    # file meant none of that showed, and whatever is left in run() showed
    # even less. A report that hides a surface is worth less than one whose
    # number is lower.
    ".*/src/main\\.cpp"
    # Presentation helpers.
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
# Keep going after a failure. Without this the claim above is not true:
# ninja and make both stop at the first failing edge, so one unbuildable
# target takes every target queued behind it with it. That is not a
# hypothetical -- a single unlinked test hid nineteen others and produced a
# report over a third of the suite short, with a plausible-looking
# percentage and nothing obviously wrong.
#
# The generator has to be passed in. This script runs under `cmake -P`, where
# CMAKE_GENERATOR is whatever the -P invocation was given and not what the
# project was configured with -- which is to say empty. Testing it directly
# chose the no-flag branch every time, so the keep-going above never actually
# happened: on macOS one Linux-only source took twenty-five objects and ten
# binaries down with it, and the report came back a confident 0.0%.
if(COVERAGE_GENERATOR MATCHES "Ninja")
    set(_keep_going -- -k 0)
elseif(COVERAGE_GENERATOR MATCHES "Make")
    set(_keep_going -- -k)
else()
    set(_keep_going)
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${COVERAGE_BINARY_DIR}" --parallel ${_keep_going}
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
# --timeout, because a deadlocked case must not cost the whole report. CTest's
# own default is 1500s per test; a suite whose slowest legitimate case is a few
# seconds does not need to wait 25 minutes to learn that one has wedged.
execute_process(
    COMMAND "${CTEST_EXECUTABLE}" --output-on-failure --timeout 300
    WORKING_DIRECTORY "${COVERAGE_BINARY_DIR}"
    RESULT_VARIABLE _ctest_result
)
if(NOT _ctest_result EQUAL 0)
    message(WARNING
        "Coverage: the suite exited ${_ctest_result}. Reporting anyway -- but read the "
        "numbers knowing some cases did not finish.")
endif()

# ---------------------------------------------------------------------------
# 2a. Drop duplicate copies of sources the suite never executed
# ---------------------------------------------------------------------------
# Most of this project's sources are compiled more than once: into the app
# target, into the rpi_imager_testable library the test binaries link, and
# historically into individual test targets that have since been changed to
# link the library instead. gcov emits a .gcno per copy, and gcovr counts
# every one of them -- so a file measured once was being reported several
# times over, with the copies that never ran contributing their full branch
# count and nothing taken.
#
# It is not a small correction. Left in, imagewriter.cpp read 1196 of 3872
# branches; the copy that actually ran has 2138. Across the tree the report
# said 54.1% of 25,330 branches when the true figure for the same run was
# 67.0% of 20,449 -- thirteen points of a report that exists to say where the
# gaps are, spent describing object files rather than code. Line coverage was
# out by the same margin, 61.9% against 75.5%. The counters were identical
# either way: 13,691 branches taken, 12,684 lines. Nothing was measured
# differently, it was only divided by the wrong number.
#
# The stale ones a clean build would not produce are the worst of it, but the
# app target's copy is never run by definition, so this cannot be fixed by
# rebuilding.
#
# The rule has to be narrow, because the obvious version of it is wrong:
# deleting every .gcno with no .gcda would also delete the genuinely untested
# files -- the ones with a single copy that nothing executes -- and hide real
# gaps behind a better-looking number. So an orphan is only dropped when the
# *same source* has counters somewhere else, which makes it a duplicate of
# something already measured and never the only record of a file.
#
# The key is the path below the target's .dir, with the ../ segments CMake
# spells `__` removed, so that CMakeFiles/rpi-imager.dir/imagewriter.cpp and
# test/CMakeFiles/rpi_imager_testable.dir/__/imagewriter.cpp compare equal.
file(GLOB_RECURSE _gcda_files "${COVERAGE_BINARY_DIR}/*.gcda")
set(_measured_sources)
foreach(_gcda IN LISTS _gcda_files)
    string(REGEX REPLACE "^.*\\.dir/" "" _key "${_gcda}")
    string(REGEX REPLACE "(__/)+" "" _key "${_key}")
    string(REGEX REPLACE "\\.gcda$" "" _key "${_key}")
    list(APPEND _measured_sources "${_key}")
endforeach()

file(GLOB_RECURSE _gcno_files "${COVERAGE_BINARY_DIR}/*.gcno")
set(_duplicate_gcno)
foreach(_gcno IN LISTS _gcno_files)
    string(REGEX REPLACE "\\.gcno$" "" _base "${_gcno}")
    if(EXISTS "${_base}.gcda")
        continue()
    endif()
    string(REGEX REPLACE "^.*\\.dir/" "" _key "${_gcno}")
    string(REGEX REPLACE "(__/)+" "" _key "${_key}")
    string(REGEX REPLACE "\\.gcno$" "" _key "${_key}")
    # list(FIND) rather than IN_LIST: this file runs under `cmake -P`, where
    # IN_LIST needs policy CMP0057 set and silently degrades to a string
    # comparison without it -- which matches nothing and prunes nothing.
    list(FIND _measured_sources "${_key}" _measured_index)
    if(NOT _measured_index EQUAL -1)
        list(APPEND _duplicate_gcno "${_gcno}")
    endif()
endforeach()

list(LENGTH _duplicate_gcno _duplicate_count)
if(_duplicate_count GREATER 0)
    message(STATUS
        "Coverage: dropping ${_duplicate_count} unexecuted duplicate object file(s) "
        "so each source is counted once")
    file(REMOVE ${_duplicate_gcno})
endif()

# The rule above only reaches copies that never ran, and there is now a copy
# that does. cli_process_test drives the shipping binary as a subprocess --
# the only way to reach Cli::run(), which builds its own QCoreApplication --
# so the app target's objects come back with counters on them for every
# source that binary touches while starting up. Seventy of them, which is
# most of the core.
#
# Two copies with counters is worse than two copies where one is empty. gcov
# writes its .gcov next to the object it was given but names it after the
# *source*, so for a source built twice the second run overwrites the first,
# and which one the report ends up describing depends on the order gcovr got
# through them. It is not a small difference either: the app target is built
# with the shipping optimisation flags and the test library is not, so the
# two copies do not even have the same number of branches. One run put
# imagewriter.cpp at 2033 of 3386 and the next at 425 of 3386, from the same
# suite and the same counters -- eleven points off the total, in whichever
# direction the walk happened to go.
#
# So one copy per source, chosen rather than raced for. The library copy is
# the one to keep: every test binary links it, so it carries the whole
# suite's counters, where the app target's carries ten CLI invocations that
# exit before doing anything. The exception is the file those invocations
# exist to cover -- run() is compiled into both, but only ever executed in
# the app target's copy, so for that one the app copy is the measurement and
# the library's is the near-empty duplicate.
set(_subprocess_measured_sources "cli.cpp")

set(_app_object_dir "${COVERAGE_BINARY_DIR}/CMakeFiles/rpi-imager.dir")
file(GLOB_RECURSE _app_gcda "${_app_object_dir}/*.gcda")
set(_raced_copies)
foreach(_gcda IN LISTS _app_gcda)
    string(REGEX REPLACE "\\.gcda$" "" _stem "${_gcda}")
    string(REGEX REPLACE "^.*\\.dir/" "" _key "${_gcda}")
    string(REGEX REPLACE "(__/)+" "" _key "${_key}")
    string(REGEX REPLACE "\\.gcda$" "" _key "${_key}")

    list(FIND _subprocess_measured_sources "${_key}" _keep_app_copy)
    if(NOT _keep_app_copy EQUAL -1)
        # Drop the library's copy instead, for the same reason in reverse.
        get_filename_component(_leaf "${_key}" NAME)
        file(GLOB_RECURSE _other_copies "${COVERAGE_BINARY_DIR}/*/${_leaf}.gcda")
        foreach(_other IN LISTS _other_copies)
            if(NOT _other MATCHES "^${_app_object_dir}/")
                string(REGEX REPLACE "\\.gcda$" "" _other_stem "${_other}")
                list(APPEND _raced_copies "${_other_stem}.gcda" "${_other_stem}.gcno")
            endif()
        endforeach()
        continue()
    endif()

    # Only a duplicate if the same source is measured somewhere else.
    get_filename_component(_leaf "${_key}" NAME)
    file(GLOB_RECURSE _other_copies "${COVERAGE_BINARY_DIR}/*/${_leaf}.gcda")
    set(_has_other FALSE)
    foreach(_other IN LISTS _other_copies)
        if(NOT _other MATCHES "^${_app_object_dir}/")
            set(_has_other TRUE)
        endif()
    endforeach()
    if(_has_other)
        list(APPEND _raced_copies "${_stem}.gcda" "${_stem}.gcno")
    endif()
endforeach()

list(REMOVE_DUPLICATES _raced_copies)
list(LENGTH _raced_copies _raced_count)
if(_raced_count GREATER 0)
    message(STATUS
        "Coverage: dropping ${_raced_count} object file(s) for sources measured "
        "in two places, so the report does not depend on which gcov ran last")
    file(REMOVE ${_raced_copies})
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
