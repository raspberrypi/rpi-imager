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

if(NOT DEFINED COVERAGE_FLAVOUR)
    set(COVERAGE_FLAVOUR "gcov")
endif()
if(COVERAGE_FLAVOUR STREQUAL "llvm")
    set(_tool_required LLVM_PROFDATA_EXECUTABLE LLVM_COV_EXECUTABLE)
else()
    set(_tool_required GCOVR_EXECUTABLE)
endif()

foreach(_required
        COVERAGE_BINARY_DIR COVERAGE_SOURCE_DIR COVERAGE_OUTPUT_DIR
        COVERAGE_GENERATOR ${_tool_required} CTEST_EXECUTABLE)
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
file(MAKE_DIRECTORY "${COVERAGE_OUTPUT_DIR}")
file(REMOVE "${COVERAGE_OUTPUT_DIR}/build-failures.log")

message(STATUS "Coverage: building instrumented targets")
# Keep going after a failure. Without this the claim above is not true:
# ninja and make both stop at the first failing edge, so one unbuildable
# target takes every target queued behind it with it. That is not a
# hypothetical -- a single unlinked test hid nineteen others and produced a
# report over a third of the suite short, with a plausible-looking
# percentage and nothing obviously wrong.
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
# Counters accumulate across runs, so without this the report describes every
# run since the build directory was created rather than this one. Under clang
# it is worse than stale: %m in LLVM_PROFILE_FILE means the runtime merges into
# whatever is already there.
set(_profraw_dir "${COVERAGE_BINARY_DIR}/profraw")
if(COVERAGE_FLAVOUR STREQUAL "llvm")
    file(GLOB _stale "${_profraw_dir}/*.profraw" "${COVERAGE_BINARY_DIR}/*.profdata")
else()
    file(GLOB_RECURSE _stale "${COVERAGE_BINARY_DIR}/*.gcda")
endif()
list(LENGTH _stale _stale_count)
if(_stale_count GREATER 0)
    message(STATUS "Coverage: clearing ${_stale_count} counter file(s) from a previous run")
    file(REMOVE ${_stale})
endif()
file(MAKE_DIRECTORY "${_profraw_dir}")

# ---------------------------------------------------------------------------
# 2. Run the suite
# ---------------------------------------------------------------------------
message(STATUS "Coverage: running the CTest suite")
# --timeout, because a deadlocked case must not cost the whole report. CTest's
# own default is 1500s per test; a suite whose slowest legitimate case is a few
# seconds does not need to wait 25 minutes to learn that one has wedged.
# %m rather than %p: it names the file after the binary rather than the
# process, and the runtime merges into it. One file per test binary instead of
# one per case, which for a suite of 1,697 cases is the difference between
# sixty files and several thousand.
if(COVERAGE_FLAVOUR STREQUAL "llvm")
    set(_ctest_launcher "${CMAKE_COMMAND}" -E env
        "LLVM_PROFILE_FILE=${_profraw_dir}/%m.profraw")
else()
    set(_ctest_launcher)
endif()

# In parallel, like every other run of this suite. Serially the report took an
# hour of which most was one case waiting on the next: the two secure-boot
# cases are four minutes each on their own, and nothing overlapped them.
set(COVERAGE_CTEST_PARALLEL "4" CACHE STRING
    "How many test processes the coverage run may use at once")

execute_process(
    COMMAND ${_ctest_launcher} "${CTEST_EXECUTABLE}" --output-on-failure --timeout 300
            -j "${COVERAGE_CTEST_PARALLEL}"
    WORKING_DIRECTORY "${COVERAGE_BINARY_DIR}"
    RESULT_VARIABLE _ctest_result
)
if(NOT _ctest_result EQUAL 0)
    message(WARNING
        "Coverage: the suite exited ${_ctest_result}. Reporting anyway -- but read the "
        "numbers knowing some cases did not finish.")
endif()

if(NOT COVERAGE_FLAVOUR STREQUAL "llvm")
set(_source_prefixes)
get_filename_component(_src_root "${COVERAGE_SOURCE_DIR}" ABSOLUTE)
get_filename_component(_src_root_real "${COVERAGE_SOURCE_DIR}" REALPATH)
foreach(_root IN ITEMS "${_src_root}" "${_src_root_real}")
    string(REGEX REPLACE "^/" "" _root "${_root}")
    list(APPEND _source_prefixes "${_root}/")
endforeach()
list(REMOVE_DUPLICATES _source_prefixes)

# Reduce one object path to the source it was built from, however that
# target's object directory happens to spell it. A function rather than a
# macro: the extension pattern has to survive being written literally, and a
# macro re-parses its arguments, which turns the backslash into a warning on
# every one of the thousand-odd files here.
function(_coverage_source_key _out _path)
    string(REGEX REPLACE "^.*\\.dir/" "" _key "${_path}")
    foreach(_prefix IN LISTS _source_prefixes)
        string(REGEX REPLACE "^${_prefix}" "" _key "${_key}")
    endforeach()
    string(REGEX REPLACE "(__/)+" "" _key "${_key}")
    string(REGEX REPLACE "\\.gc(da|no)$" "" _key "${_key}")
    set(${_out} "${_key}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE _gcda_files "${COVERAGE_BINARY_DIR}/*.gcda")
set(_measured_sources)
foreach(_gcda IN LISTS _gcda_files)
    _coverage_source_key(_key "${_gcda}")
    list(APPEND _measured_sources "${_key}")
endforeach()

file(GLOB_RECURSE _gcno_files "${COVERAGE_BINARY_DIR}/*.gcno")
set(_duplicate_gcno)
foreach(_gcno IN LISTS _gcno_files)
    string(REGEX REPLACE "\\.gcno$" "" _base "${_gcno}")
    if(EXISTS "${_base}.gcda")
        continue()
    endif()
    _coverage_source_key(_key "${_gcno}")
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
# source that binary touches. Seventy of them, which is most of the core.
#
# Both copies are kept, and the render below reads them separately and merges
# the two tracefiles. What that fixes: anything reachable *only* by running
# the shipping binary used to be reported as never run.
endif()

# ---------------------------------------------------------------------------
# 3. Render
# ---------------------------------------------------------------------------
file(MAKE_DIRECTORY "${COVERAGE_OUTPUT_DIR}")

if(COVERAGE_FLAVOUR STREQUAL "llvm")
    # ---------------------------------------------------------------------
    # llvm-profdata + llvm-cov
    # ---------------------------------------------------------------------
    # Every binary that ran has to be named: llvm-cov reads the counters out
    # of the profile and the mapping out of the executables, so a binary left
    # off the list takes its sources out of the report entirely. Whatever is
    # executable under test/ qualifies, plus the app -- which is never run by
    # the suite, and is named for exactly that reason. Its QML-facing sources
    # then appear at 0% instead of not appearing at all, which is the whole
    # point of a gap report.
    file(GLOB _profraw "${_profraw_dir}/*.profraw")
    list(LENGTH _profraw _profraw_count)
    if(_profraw_count EQUAL 0)
        message(FATAL_ERROR
            "Coverage: no .profraw files were written. The suite either did not run "
            "or was not built with -fprofile-instr-generate.")
    endif()

    set(_profdata "${COVERAGE_BINARY_DIR}/coverage.profdata")
    message(STATUS "Coverage: merging ${_profraw_count} profile(s)")
    execute_process(
        COMMAND "${LLVM_PROFDATA_EXECUTABLE}" merge -sparse ${_profraw} -o "${_profdata}"
        RESULT_VARIABLE _merge_result
    )
    if(NOT _merge_result EQUAL 0)
        message(FATAL_ERROR "Coverage: llvm-profdata merge failed (${_merge_result})")
    endif()

    file(GLOB _candidates "${COVERAGE_BINARY_DIR}/test/*")
    set(_objects)
    foreach(_candidate IN LISTS _candidates)
        if(IS_DIRECTORY "${_candidate}")
            continue()
        endif()
        if(_candidate MATCHES "\\.(cmake|json|txt|a|dylib|so|log)$")
            continue()
        endif()
        file(READ "${_candidate}" _magic LIMIT 4 HEX)
        # Mach-O (feedface / cffaedfe and friends) or ELF (7f454c46).
        if(_magic MATCHES "^(cffaedfe|cefaedfe|feedfacf|feedface|7f454c46)$")
            list(APPEND _objects "${_candidate}")
        endif()
    endforeach()

    foreach(_app_candidate
            "${COVERAGE_BINARY_DIR}/${COVERAGE_APP_NAME}.app/Contents/MacOS/${COVERAGE_APP_NAME}"
            "${COVERAGE_BINARY_DIR}/${COVERAGE_APP_NAME}")
        if(EXISTS "${_app_candidate}" AND NOT IS_DIRECTORY "${_app_candidate}")
            list(APPEND _objects "${_app_candidate}")
            break()
        endif()
    endforeach()

    list(LENGTH _objects _object_count)
    if(_object_count EQUAL 0)
        message(FATAL_ERROR "Coverage: no instrumented binaries found to report on")
    endif()
    message(STATUS "Coverage: reading ${_object_count} binaries")

    # llvm-cov takes the first binary as a positional argument and the rest
    # behind -object; a list of nothing but -object leaves it reading the
    # first source file as an executable and giving up.
    list(POP_FRONT _objects _first_object)
    set(_object_args)
    foreach(_object IN LISTS _objects)
        list(APPEND _object_args "-object" "${_object}")
    endforeach()

    # The same scope as the gcovr path, in the form llvm-cov takes: one
    # -ignore-filename-regex per pattern, matched against the whole path.
    # Anything outside the source tree -- system headers, Qt, the vendored
    # dependencies -- is dropped by the source root filter that follows.
    set(_ignore_args)
    foreach(_pattern IN LISTS _exclude)
        list(APPEND _ignore_args "-ignore-filename-regex=${_pattern}")
    endforeach()
    # Everything outside the source tree -- Qt, the SDK headers, Catch2, the
    # vendored dependencies -- is dropped by naming our own files as the only
    # sources to report on. llvm-cov's regex engine has no negative lookahead,
    # so a whitelist is the way to say "only ours"; an ignore list would have
    # to name every foreign root correctly and silently inflates the report
    # the day it misses one. The exclusions above still apply on top, and are
    # what takes the test tree and the UI layer back out again.
    get_filename_component(_source_root "${COVERAGE_SOURCE_DIR}" ABSOLUTE)
    file(GLOB_RECURSE _project_sources
        "${_source_root}/*.c" "${_source_root}/*.cpp" "${_source_root}/*.cc"
        "${_source_root}/*.mm" "${_source_root}/*.h" "${_source_root}/*.hpp")
    list(LENGTH _project_sources _project_source_count)
    if(_project_source_count EQUAL 0)
        message(FATAL_ERROR "Coverage: no sources found under ${_source_root}")
    endif()

    message(STATUS "Coverage: rendering report")
    execute_process(
        COMMAND "${LLVM_COV_EXECUTABLE}" report
                "${_first_object}"
                ${_object_args}
                "-instr-profile=${_profdata}"
                ${_ignore_args}
                "-show-branch-summary"
                ${_project_sources}
        OUTPUT_VARIABLE _llvm_summary
        RESULT_VARIABLE _report_result
        ERROR_VARIABLE _llvm_report_error
    )
    if(NOT _report_result EQUAL 0)
        message(FATAL_ERROR
            "Coverage: llvm-cov report failed (${_report_result}): ${_llvm_report_error}")
    endif()
    file(WRITE "${COVERAGE_OUTPUT_DIR}/summary.txt" "${_llvm_summary}")

    execute_process(
        COMMAND "${LLVM_COV_EXECUTABLE}" show
                "${_first_object}"
                ${_object_args}
                "-instr-profile=${_profdata}"
                ${_ignore_args}
                "-format=html"
                "-show-branches=count"
                "-show-line-counts-or-regions"
                "-output-dir=${COVERAGE_OUTPUT_DIR}"
                "-project-title=rpi-imager core coverage"
                ${_project_sources}
        RESULT_VARIABLE _show_result
        ERROR_VARIABLE _llvm_show_error
    )
    if(NOT _show_result EQUAL 0)
        message(WARNING
            "Coverage: llvm-cov show failed (${_show_result}), so there is a text "
            "summary but no HTML: ${_llvm_show_error}")
    endif()

    # The last row of `llvm-cov report` is the tree total. Echo it, so a run
    # says the same kind of thing whichever toolchain produced it.
    string(REGEX MATCH "\nTOTAL[^\n]*" _total_line "${_llvm_summary}")
    if(_total_line)
        string(STRIP "${_total_line}" _total_line)
        message(STATUS "Coverage: ${_total_line}")
    endif()
    message(STATUS "Coverage: HTML   ${COVERAGE_OUTPUT_DIR}/index.html")
    message(STATUS "Coverage: text   ${COVERAGE_OUTPUT_DIR}/summary.txt")
else()
    file(MAKE_DIRECTORY "${COVERAGE_OUTPUT_DIR}")

    # Arguments that decide what is measured and how it is parsed. Both
    # producing passes below get the same set, so the two tracefiles describe
    # the same universe and merge cleanly.
    set(_gcovr_parse_args
        --root "${COVERAGE_SOURCE_DIR}"
        ${_exclude_args}
        --decisions
        # Without these two a branch-metric report on C++ is unreadable. Every
        # call that might throw carries a branch pair for the unwind edge, and
        # the unwind arm is never taken in a passing run, so gcov reports it
        # half-covered forever. It is not a gap anyone can close -- you would
        # have to make the allocation fail.
        --exclude-throw-branches
        --exclude-unreachable-branches
        # Qt's registration macros. Q_ENUM and its relatives expand to
        # meta-object glue the runtime touches only when something looks the
        # type up by name, so mostly they sit at zero for the life of the
        # project. Ten lines across five files, four of which gcov did count
        # as covered -- so this half does lose a little real signal. Worth it
        # because two of those files hold no other code, and they led the
        # report at 0% and 40%, drawing the eye away from the real gaps.
        --exclude-lines-by-pattern "^\\s*(?:(?:Q_ENUM|Q_ENUM_NS|Q_FLAG|Q_FLAG_NS|Q_DECLARE_OPERATORS_FOR_FLAGS|Q_DECLARE_METATYPE)\\s*\\(|\\{\\s*(?:0x[0-9A-Fa-f]+|[0-9]+|\"[^\"]*\"|[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z0-9_]+)*)\\s*,\\s*(?:\"[^\"]*\"|[A-Za-z0-9_:.]+)\\s*\\}\\s*,?\\s*(?://.*)?$)"
        # The --exclude patterns above decide what reaches the report, but
        # gcovr processes every .gcda it finds before applying them, and
        # instrumentation is global -- so it walked into every FetchContent
        # dependency, 492 of the 508 .gcno files here. gcov cannot resolve
        # their sources from our build tree, and each failure printed: it was
        # 10,461 lines of stderr on a run whose real output is four numbers.
        # Pruning the walk removes the noise and the work both.
        --gcov-exclude-directories ".*_deps.*"
        # Kept as a backstop only. This suppresses the "could not infer a
        # working directory" failure, which before the pruning above was not
        # an edge case but the single loudest thing in the run -- and fatal
        # without it. Nothing in our own tree provokes it now.
        --gcov-ignore-errors no_working_dir_found)

    set(_app_object_dir "${COVERAGE_BINARY_DIR}/CMakeFiles/rpi-imager.dir")
    set(_library_tracefile "${COVERAGE_BINARY_DIR}/coverage-library.json")
    set(_app_tracefile "${COVERAGE_BINARY_DIR}/coverage-app.json")
    set(_tracefile_args -a "${_library_tracefile}")

    # Pass one: everything the test binaries link, which is the whole suite's
    # counters. The app target's objects are held back for pass two rather
    # than raced with these -- see the note above section 3.
    message(STATUS "Coverage: reading the test objects")
    execute_process(
        COMMAND "${GCOVR_EXECUTABLE}"
                ${_gcovr_parse_args}
                --gcov-exclude-directories "rpi-imager\\.dir"
                "${COVERAGE_BINARY_DIR}"
                --json "${_library_tracefile}"
        WORKING_DIRECTORY "${COVERAGE_BINARY_DIR}"
        RESULT_VARIABLE _gcovr_result
    )
    if(NOT _gcovr_result EQUAL 0)
        message(FATAL_ERROR "Coverage: gcovr failed reading the test objects (${_gcovr_result})")
    endif()

    # Pass two: the shipping binary, which cli_process_test drives as a
    # subprocess. Skipped when it was never run, so a build that did not
    # produce it does not fail the report.
    file(GLOB_RECURSE _app_gcda "${_app_object_dir}/*.gcda")
    if(_app_gcda)
        list(LENGTH _app_gcda _app_gcda_count)
        message(STATUS "Coverage: reading ${_app_gcda_count} object(s) from the shipping binary")
        execute_process(
            COMMAND "${GCOVR_EXECUTABLE}"
                    ${_gcovr_parse_args}
                    "${_app_object_dir}"
                    --json "${_app_tracefile}"
            WORKING_DIRECTORY "${COVERAGE_BINARY_DIR}"
            RESULT_VARIABLE _gcovr_result
        )
        if(NOT _gcovr_result EQUAL 0)
            message(FATAL_ERROR "Coverage: gcovr failed reading the shipping binary (${_gcovr_result})")
        endif()
        list(APPEND _tracefile_args -a "${_app_tracefile}")
    else()
        message(STATUS "Coverage: the shipping binary was not run; reporting the test objects alone")
    endif()

    message(STATUS "Coverage: rendering report")
    execute_process(
        COMMAND "${GCOVR_EXECUTABLE}"
                --root "${COVERAGE_SOURCE_DIR}"
                ${_tracefile_args}
                # Branch coverage is the reason for doing this at all: line
                # coverage would have called runWithTimeout's timeout path
                # "reached" as soon as anything entered the loop.
                --txt-metric branch
                # Sort by uncovered *branches*, not uncovered lines: without
                # --sort-branches the table is ordered by a metric it does not
                # display. --sort-reverse because gcovr sorts ascending, which
                # for a gap report is exactly backwards -- it opened on 27 rows
                # of header files with no branches at all ("--%"), and put
                # downloadthread.cpp, the single biggest gap in the tree at
                # 1,958 uncovered branches, last in an 89-row table.
                --sort uncovered-number
                --sort-branches
                --sort-reverse
                # Left on gcovr's default theme deliberately. The `github.*`
                # themes look considerably more modern, but all four of them
                # render a partially covered line in near enough the same
                # colour as a fully covered one -- and a partially covered line
                # is the single thing this report exists to show. The default
                # theme's green/yellow/red are ugly and unambiguous, in that
                # order of importance.
                --html-details "${COVERAGE_OUTPUT_DIR}/index.html"
                --html-title "rpi-imager core coverage"
                --txt "${COVERAGE_OUTPUT_DIR}/summary.txt"
                --print-summary
        WORKING_DIRECTORY "${COVERAGE_BINARY_DIR}"
        RESULT_VARIABLE _gcovr_result
    )
    if(NOT _gcovr_result EQUAL 0)
        message(FATAL_ERROR "Coverage: gcovr failed (${_gcovr_result})")
    endif()
endif()

# ---------------------------------------------------------------------------
# 4. Make summary.txt fit on a screen
# ---------------------------------------------------------------------------
# gcov only. llvm-cov's own table is already one line per file with no trailing
# list of line numbers, so there is nothing here to trim.
if(COVERAGE_FLAVOUR STREQUAL "llvm")
    return()
endif()

# gcovr's text report ends each row with every uncovered line number, and has
# no option to shorten it. For a file at 0% that is the whole file: the
# downloadthread.cpp row alone was 3,902 characters, 33 rows ran past 200, and
# the "summary" came to 29KB. Sorting worst-first made it worse, because the
# longest rows are now the first ones you see.
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
