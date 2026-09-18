# Test configuration for rpi-imager
# Uses Catch2 and CTest for testing

# Fetch Catch2 for testing
include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.0
    # Version floor, or find_package matches a system Catch2 v2 -- still the
    # packaged one on Debian/Ubuntu -- and FetchContent skips the fetch. The
    # suite is v3: catch_test_macros.hpp and Catch2WithMain are v3-only, so
    # that build fails at the first #include rather than at configure time.
    FIND_PACKAGE_ARGS 3 NAMES Catch2
)

FetchContent_MakeAvailable(Catch2)

# Include Catch2 CMake utilities for test discovery
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# Where the DLLs a test binary needs at run time live.
#
# Windows resolves a DLL from PATH, having no rpath, so every test needs this --
# and so does catch_discover_tests, which runs each binary at build time to
# enumerate its cases. Without it a binary dies of STATUS_DLL_NOT_FOUND before
# main(), reported only as "Error listing tests from executable".
#
# Qt's install paths rather than $<TARGET_FILE_DIR:Qt6::Core>, because the
# QtQuickTest entries below are registered with add_test() and take this through
# set_tests_properties(), where a generator expression is not dependable. Qt's
# bin directory carries libgcc and libwinpthread beside the Qt DLLs.
set(RPI_TEST_DLL_PATHS)
if(WIN32)
    list(APPEND RPI_TEST_DLL_PATHS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    if(MINGW64_ROOT)
        list(APPEND RPI_TEST_DLL_PATHS "${MINGW64_ROOT}/bin")
    endif()
endif()

# Where the tests find fonts.
#
# Qt ships none, and the offscreen plugin on Windows looks under
# ${QT6_INSTALL_PREFIX}/lib/fonts, which Qt does not create. Linux has
# fontconfig to fall back on; Windows has not, so QFontDatabase::families()
# comes back empty and every glyph is a .notdef box.
#
# The product never meets this -- platformquirks_windows.cpp forces FreeType and
# the real plugin reads the system fonts -- but -platform offscreen bypasses
# that. Nothing failed to start, so the QtQuickTest run went green against a UI
# with no text in it, every width assertion measuring identical tofu.
set(RPI_TEST_FONT_ENV)
if(WIN32)
    if(DEFINED ENV{SystemRoot})
        file(TO_CMAKE_PATH "$ENV{SystemRoot}/Fonts" _rpi_test_fontdir)
    else()
        set(_rpi_test_fontdir "C:/Windows/Fonts")
    endif()
    if(EXISTS "${_rpi_test_fontdir}")
        set(RPI_TEST_FONT_ENV "QT_QPA_FONTDIR=${_rpi_test_fontdir}")
    else()
        message(WARNING
                "No font directory at ${_rpi_test_fontdir}. The offscreen "
                "platform ships no fonts, so QML tests that measure text will "
                "be measuring .notdef boxes.")
    endif()
endif()

# The same search path, plus the logging setting below, for tests registered
# with add_test() rather than through Catch2. Takes one or more test names.
function(rpi_set_windows_test_env)
    if(NOT WIN32)
        return()
    endif()
    set(_mod)
    foreach(_dir IN LISTS RPI_TEST_DLL_PATHS)
        list(APPEND _mod "PATH=path_list_prepend:${_dir}")
    endforeach()
    set_tests_properties(${ARGN} PROPERTIES ENVIRONMENT_MODIFICATION "${_mod}")
    set_property(TEST ${ARGN} APPEND PROPERTY
                 ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
    if(RPI_TEST_FONT_ENV)
        set_property(TEST ${ARGN} APPEND PROPERTY
                     ENVIRONMENT "${RPI_TEST_FONT_ENV}")
    endif()
endfunction()

# Register a Catch2 binary's cases with CTest.
#
# Wraps catch_discover_tests purely to pin SKIP_RETURN_CODE. Catch2 exits 4 when
# every selected case was skipped, and because discovery registers one CTest test
# per case, a case that calls SKIP() is always the only one selected — so without
# this CTest reports every skip as a failure. Tests that skip when optional
# fixtures or hardware are absent (the pieeprom and eeprom_signer ones) were
# accounting for six phantom failures. Use this in place of
# catch_discover_tests() so a new test binary cannot reintroduce them.
# Pass FAULT_INJECTION for a target using faulty_block_device.h, to insert the
# macOS interposer at launch -- dyld only honours __interpose in an image
# present then, so linking it would not do.
function(rpi_discover_tests target)
    # Set but empty: no download counter is reported. The counter is gated on
    # the user being on the default OS list, which a test satisfies without
    # asking for it, so every fabricated write in this suite was posted to
    # the production endpoint -- on every run, on every machine, including
    # the subprocess tests that drive the shipping binary.
    # One ENVIRONMENT list, not two: CMake keeps only the last of a repeated
    # property key, so appending a second would have dropped the first --
    # which on macOS is the interpose library the fault-injection tests need.
    #
    # XDG_CONFIG_HOME as well: the imager keeps its settings in QSettings, and
    # several paths write them -- setPersistedCustomisationSetting() is called
    # whenever a customisation step is completed. Without this a test run
    # rewrites the developer's own "Raspberry Pi Imager.conf", which holds
    # their hostname, username and password hash. Pointed at the build tree,
    # where it belongs.
    #
    # BROWSER too. Several screens offer a link, and openUrlExternally() goes
    # through xdg-open, which launches whatever the developer has configured
    # -- a chaos run clicking about opened a terminal browser at
    # raspberrypi.com and held the test until it timed out. Pointed at true(1),
    # which accepts the argument and does nothing.
    # "off" rather than empty on Windows, and this is not a nicety: setting an
    # environment variable to an empty string there deletes it, so the empty
    # value below never reached the process and every fabricated write in this
    # suite posted a download counter to the production endpoint -- the exact
    # thing the comment above says was fixed.
    if(WIN32)
        set(_telemetry_off "RPI_IMAGER_TELEMETRY_URL=off")
    else()
        set(_telemetry_off "RPI_IMAGER_TELEMETRY_URL=")
    endif()
    set(_test_env "${_telemetry_off}" "RPI_IMAGER_CONNECT_URL=http://127.0.0.1:1"
                  "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/scratch-config;BROWSER=true"
                  "BROWSER=true")
    # Without this Qt's default handler on Windows sends qDebug/qWarning to the
    # debugger rather than to stderr, so a test that fails under CTest comes
    # back with an exit code and not one line of why. The QtQuickTest binary was
    # the extreme case: thirteen seconds of work, a real assertion failure, and
    # completely empty output.
    if(WIN32)
        list(APPEND _test_env "QT_FORCE_STDERR_LOGGING=1")
    endif()
    if(RPI_TEST_FONT_ENV)
        list(APPEND _test_env "${RPI_TEST_FONT_ENV}")
    endif()
    if(APPLE AND "FAULT_INJECTION" IN_LIST ARGN)
        add_dependencies(${target} faulty_io_interpose)
        list(APPEND _test_env "DYLD_INSERT_LIBRARIES=${RPI_FAULTY_INTERPOSE_LIB}")
    endif()
    # Without this a freshly built binary cannot locate Qt6Core.dll, and the
    # discovery run dies of STATUS_DLL_NOT_FOUND before main() -- silently, so
    # all CMake reports is "Error listing tests from executable". Every test
    # target failed to configure on Windows for want of it.
    #
    # DL_PATHS rather than another entry in _test_env: Catch2 puts these on PATH
    # for its own discovery run and emits an ENVIRONMENT_MODIFICATION for each
    # test it registers, so one setting covers both, where ENVIRONMENT would
    # reach only the second.
    set(_dl_paths)
    if(WIN32)
        set(_dl_paths DL_PATHS ${RPI_TEST_DLL_PATHS})
    endif()
    # RESOURCE_LOCK <name> serialises this binary's cases against anything
    # holding the same lock, while the rest of the suite carries on. For a
    # binary whose cases all want one machine-wide thing -- a fixed port, a
    # device -- ctest -j otherwise lets them race, and the losers skip.
    set(_lock)
    list(FIND ARGN "RESOURCE_LOCK" _lock_idx)
    if(NOT _lock_idx EQUAL -1)
        math(EXPR _lock_value_idx "${_lock_idx} + 1")
        list(GET ARGN ${_lock_value_idx} _lock_name)
        set(_lock RESOURCE_LOCK ${_lock_name})
    endif()

    catch_discover_tests(${target}
        ${_dl_paths}
        TEST_LIST ${target}_TESTS
        PROPERTIES SKIP_RETURN_CODE 4 ${_lock})

    # Not through PROPERTIES: Catch2 re-splits a list-valued property there,
    # so ENVIRONMENT kept only its first entry and ctest read the rest as
    # property names. Applied after discovery instead, as one argument.
    set(_env_script "${CMAKE_CURRENT_BINARY_DIR}/${target}_env.cmake")
    file(WRITE "${_env_script}"
        "foreach(_t IN LISTS ${target}_TESTS)\n"
        "  set_tests_properties(\"\${_t}\" PROPERTIES ENVIRONMENT [==[${_test_env}]==])\n"
        "endforeach()\n")
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${_env_script}")
endfunction()

# Inert until a test arms it, so it costs every other test one atomic load
# per pwrite.
if(APPLE)
    add_library(faulty_io_interpose SHARED faulty_io_interpose.c)
    set_target_properties(faulty_io_interpose PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set(RPI_FAULTY_INTERPOSE_LIB
        "${CMAKE_CURRENT_BINARY_DIR}/libfaulty_io_interpose.dylib")
endif()

# MacFile: the authorised-open path, minus the authorisation.
if(APPLE)
    add_executable(authopen_stub ${CMAKE_CURRENT_SOURCE_DIR}/authopen_stub.cpp)
    target_compile_features(authopen_stub PRIVATE cxx_std_20)
    target_compile_options(authopen_stub PRIVATE -Wall -Wextra -Wpedantic)

    find_library(SECURITY_FRAMEWORK Security)
    add_executable(macfile_test
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/macfile.cpp
        macfile_test.cpp
    )
    set_target_properties(macfile_test PROPERTIES AUTOMOC ON)
    target_link_libraries(macfile_test PRIVATE
        Catch2::Catch2WithMain Qt6::Core ${SECURITY_FRAMEWORK})
    target_include_directories(macfile_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_features(macfile_test PRIVATE cxx_std_20)
    target_compile_definitions(macfile_test PRIVATE
        AUTHOPEN_STUB_BINARY="$<TARGET_FILE:authopen_stub>")
    add_dependencies(macfile_test authopen_stub)
    rpi_discover_tests(macfile_test)
endif()

# Link liburing into a target that compiles the platform file_operations.
#
# linux/Platform.cmake adds -DHAVE_LIBURING with add_definitions(), which every
# target in this directory inherits, but it only puts the library itself into
# EXTRALIBS -- and EXTRALIBS reaches the app target alone. Any test binary
# compiling file_operations_linux.cpp therefore got the io_uring code paths
# compiled in with nothing to resolve them against, and failed to link on any
# machine with liburing-dev >= 2.2 installed. Call this for every target that
# pulls in PLATFORM_FILE_OPS.
function(rpi_link_platform_file_ops target)
    if(LIBURING_USABLE)
        target_include_directories(${target} PRIVATE ${LIBURING_INCLUDE_DIRS})
        target_link_libraries(${target} PRIVATE ${LIBURING_LIBRARIES})
    endif()
endfunction()

# Add the customization generator test executable
# Includes the bundled crypto sources the generator now depends on for
# credential derivation (yescrypt/sha256crypt password hashing).
add_executable(customization_generator_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../customization_generator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../customization_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/sha256crypt/sha256crypt.c
    customization_generator_test.cpp
)

# Link against Catch2, Qt and the bundled yescrypt static library
# (yescrypt is defined in the parent scope via dependencies/yescrypt.cmake and
# carries its include directory as a PUBLIC usage requirement).
target_link_libraries(customization_generator_test PRIVATE 
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Network
    yescrypt
)

# Include parent directory for headers
target_include_directories(customization_generator_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(customization_generator_test PRIVATE cxx_std_20)
target_compile_options(customization_generator_test PRIVATE 
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

# Register with CTest for automatic test discovery
rpi_discover_tests(customization_generator_test)

# Also add a custom target for manual running
add_custom_target(test_customization_generator
    COMMAND customization_generator_test
    DEPENDS customization_generator_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running customization generator tests"
)

# Determine platform-specific file operations implementation for FAT partition test
if(WIN32)
    set(PLATFORM_FILE_OPS
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/file_operations_windows.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/file_operations_windows.cpp
    )
elseif(APPLE)
    set(PLATFORM_FILE_OPS
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/file_operations_macos.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/file_operations_macos.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/macfile.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/macfile.cpp
    )
else()
    set(PLATFORM_FILE_OPS
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/file_operations_linux.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/file_operations_linux.cpp
    )
endif()

# boot.img is built in this process on every platform now -- the filesystem
# by DiskFormatter and the files into it by DeviceWrapperFatPartition -- so
# the creator brings those with it rather than shelling out to diskpart,
# mtools or hdiutil. It is no longer chosen per platform, because there is
# only one of it.
set(PLATFORM_BOOTIMGCREATOR_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/../bootimgcreator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../disk_formatter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS})

# The other four platform backends, chosen the same way.
#
# Eleven targets below named the linux/ sources directly, so the suite could
# only ever be built on Linux: platformquirks_linux.cpp needs <sys/eventfd.h>
# and acceleratedcryptographichash_gnutls.cpp needs GnuTLS, which cost 15
# failed objects and 10 binaries that never linked on macOS. The application
# has always picked these per platform in ../CMakeLists.txt; the tests now do
# too, in one place rather than eleven.
#
# PLATFORM_BACKEND_LIBS goes with them: the macOS halves are Objective-C++
# against AppKit, SystemConfiguration, CoreWLAN and CoreLocation, and the
# secure-boot crypto against Security, none of which arrive with Qt.
if(WIN32)
    set(PLATFORM_QUIRKS_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/platformquirks_windows.cpp)
    set(PLATFORM_SECUREBOOT_CRYPTO_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/secureboot_crypto_windows.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot_crypto_common.cpp)
    set(PLATFORM_ACCEL_HASH_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/acceleratedcryptographichash_cng.cpp)
    set(PLATFORM_DRIVELIST_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_windows.cpp)

    # The Windows backend sources no test target ever compiled. DiskpartUtil is
    # called from platformquirks_windows.cpp, downloadthread.cpp and
    # driveformatthread.cpp, and it in turn reaches WinFile and
    # Drivelist::ListStorageDevices() -- so ten targets compiled cleanly and then
    # failed at the link. Gathered into one library rather than three sources
    # named across a dozen targets, so the next caller cannot reintroduce it.
    #
    # drivelist_windows.cpp appears here as well as in PLATFORM_DRIVELIST_SRC,
    # and deliberately: a static archive gives up only the members needed to
    # resolve something still undefined, so a target compiling it directly keeps
    # its own object and never sees this copy.
    add_library(rpi_windows_support STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/diskpart_util.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/winfile.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_windows.cpp)
    # WinFile is a QObject.
    set_target_properties(rpi_windows_support PROPERTIES AUTOMOC ON)
    target_include_directories(rpi_windows_support PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(rpi_windows_support PUBLIC Qt6::Core setupapi)
    target_compile_features(rpi_windows_support PRIVATE cxx_std_20)
    # Reaches extractDiskNumber(), which is static otherwise -- the same hook
    # platformquirks_windows.cpp offers for parseDeviceNumber(). Set here
    # because this is where diskpart_util.cpp is compiled; a define on the test
    # target that merely links the archive would come too late to affect it.
    target_compile_definitions(rpi_windows_support PRIVATE DISKPART_ENABLE_TEST_API)

    # Qt6::Gui sits among the system libraries because platformquirks_windows.cpp
    # reads the "Make text bigger" accessibility setting through
    # QFontDatabase::systemFont(). The application links Gui for its own reasons,
    # so the dependency only ever surfaced here: ten test targets compile that
    # backend against Qt6::Core alone and stopped at "QFont: No such file or
    # directory". Neither the Linux nor the macOS backend wants it, which is why
    # it belongs in this branch rather than in the common link list.
    # setupapi for drivelist_windows.cpp, which calls
    # SetupDiGetDeviceRegistryProperty and SetupDiGetDeviceInterfaceDetail. The
    # application has carried it in EXTRALIBS all along; the tests linking the
    # same backend had not.
    # virtdisk for platformquirks_windows.cpp, which detaches a virtual disk
    # rather than asking a non-removable device to eject its media. Every
    # target compiling that backend needs it, not only the ones whose cases
    # attach a VHD of their own.
    set(PLATFORM_BACKEND_LIBS
        ws2_32 iphlpapi wbemuuid ole32 oleaut32 crypt32 bcrypt ncrypt setupapi
        virtdisk Qt6::Gui rpi_windows_support)
elseif(APPLE)
    set(PLATFORM_QUIRKS_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/platformquirks_macos.mm)
    set(PLATFORM_SECUREBOOT_CRYPTO_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/secureboot_crypto_macos.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot_crypto_common.cpp)
    set(PLATFORM_ACCEL_HASH_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/acceleratedcryptographichash_commoncrypto.cpp)
    set(PLATFORM_DRIVELIST_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_darwin.mm)
    set(PLATFORM_BACKEND_LIBS
        "-framework AppKit"
        "-framework SystemConfiguration"
        "-framework DiskArbitration"
        "-framework CoreFoundation"
        "-framework CoreServices"  # LSSetDefaultHandlerForURLScheme (registerUriScheme)
        "-framework CoreWLAN"
        "-framework CoreLocation"
        "-framework Security"
        "-framework IOKit"
    )
else()
    set(PLATFORM_QUIRKS_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/platformquirks_linux.cpp)
    set(PLATFORM_SECUREBOOT_CRYPTO_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/secureboot_crypto_linux.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot_crypto_common.cpp)
    set(PLATFORM_ACCEL_HASH_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/acceleratedcryptographichash_gnutls.cpp)
    set(PLATFORM_DRIVELIST_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_linux.cpp)
    set(PLATFORM_BACKEND_LIBS "")
endif()

# Link the frameworks and libraries the platform backends above need.
# Companion to rpi_link_platform_file_ops, and for the same reason: the
# application's own list reaches the app target alone.
function(rpi_link_platform_backends target)
    target_link_libraries(${target} PRIVATE ${PLATFORM_BACKEND_LIBS})
endfunction()

# Add the FAT partition test executable (manual test against real filesystem)
add_executable(fat_partition_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    # Builds the partitioned scratch image these cases open when no real card
    # is named in the environment, which is every automated run.
    ${CMAKE_CURRENT_SOURCE_DIR}/../disk_formatter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../disk_formatter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fat_partition_test.cpp
)

# Enable Qt MOC for Q_OBJECT classes
set_target_properties(fat_partition_test PROPERTIES
    AUTOMOC ON
)

# Link Catch2 and Qt libraries
target_link_libraries(fat_partition_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

# Link platform-specific libraries
if(APPLE)
    target_link_libraries(fat_partition_test PRIVATE
        "-framework Security"
        "-framework DiskArbitration"
        "-framework CoreFoundation"
    )
endif()

# Include parent directory for headers
target_include_directories(fat_partition_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(fat_partition_test PRIVATE cxx_std_20)
target_compile_options(fat_partition_test PRIVATE 
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

rpi_link_platform_file_ops(fat_partition_test)

# Register with CTest for automatic test discovery
# Note: These tests are marked with [!mayfail] and [.destructive] tags
# Run with: ctest -C Debug --output-on-failure
# Or with environment variable: FAT_TEST_MOUNT_PATH="/Volumes/bootfs 1" ctest
rpi_discover_tests(fat_partition_test)

# Custom target for manual running with mount path argument
# Usage: FAT_TEST_MOUNT_PATH="/Volumes/bootfs 1" cmake --build . --target test_fat_partition
add_custom_target(test_fat_partition
    COMMAND echo "Set FAT_TEST_MOUNT_PATH environment variable and run: ./test/fat_partition_test"
    COMMAND echo "Example: FAT_TEST_MOUNT_PATH=\"/Volumes/bootfs 1\" ./test/fat_partition_test"
    DEPENDS fat_partition_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "FAT partition test built - set FAT_TEST_MOUNT_PATH to run"
)

# ============================================================================
# WinFile -- Windows only
# ============================================================================
# The Win32 handle wrapper the write path opens a device through, and what
# DiskpartUtil locks and dismounts volumes with. It had no test at all: 0% of
# its branches, in 221 lines that sit directly under the write.
if(WIN32)
    add_executable(winfile_test
        winfile_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/winfile.cpp)
    set_target_properties(winfile_test PROPERTIES AUTOMOC ON)
    target_include_directories(winfile_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(winfile_test PRIVATE Catch2::Catch2WithMain Qt6::Core)
    target_compile_features(winfile_test PRIVATE cxx_std_20)
    rpi_discover_tests(winfile_test)
endif()

# ============================================================================
# SecureBootCrypto
# ============================================================================
# The RSA signing behind secure boot. Every case that reached it went through
# rpi-eeprom-digest, which is a Linux tool, so the whole of this skipped on
# Windows -- leaving the CryptoAPI implementation at half its branches with
# nothing asserting it produces a signature a bootloader would take.
#
# Verified against openssl rather than a recorded blob: a signature is only
# right if the public half can check it.
add_executable(secureboot_crypto_test
    secureboot_crypto_test.cpp
    ${PLATFORM_SECUREBOOT_CRYPTO_SRC})
target_include_directories(secureboot_crypto_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS})
target_link_libraries(secureboot_crypto_test PRIVATE
    Catch2::Catch2WithMain Qt6::Core ${GNUTLS_LIBRARIES})
rpi_link_platform_backends(secureboot_crypto_test)
target_compile_features(secureboot_crypto_test PRIVATE cxx_std_20)
# Reaches the CryptoAPI and CNG failure paths, which no arrangement of
# inputs can provoke. Only the Windows backend has the hooks.
if(WIN32)
    target_compile_definitions(secureboot_crypto_test PRIVATE
        SECUREBOOT_CRYPTO_ENABLE_TEST_API)
endif()

rpi_discover_tests(secureboot_crypto_test)

# ============================================================================
# The Windows file-operations backend -- Windows only
# ============================================================================
# file_operations_test covers the POSIX backend and is excluded here, which
# left the 1,876 lines that actually write a card on Windows at 25% branch
# coverage -- 752 branches, the largest single gap in the tree. This is the
# Windows half: the open and close lifecycle, sequential and positioned I/O,
# the asynchronous queue and its synchronous fallback.
#
# virtdisk for vhd_device.h, which the physical-drive cases attach.
if(WIN32)
    add_executable(file_operations_windows_test
        file_operations_windows_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
        ${PLATFORM_FILE_OPS})
    set_target_properties(file_operations_windows_test PROPERTIES AUTOMOC ON)
    target_include_directories(file_operations_windows_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(file_operations_windows_test PRIVATE
        Catch2::Catch2WithMain Qt6::Core virtdisk)
    rpi_link_platform_backends(file_operations_windows_test)
    rpi_link_platform_file_ops(file_operations_windows_test)
    target_compile_features(file_operations_windows_test PRIVATE cxx_std_20)
    # Reaches FailNextWriteCompletions(), which rehearses a device refusing
    # writes while Windows re-enumerates it. Safe to set on the target: this one
    # compiles file_operations_windows.cpp itself rather than linking an archive.
    target_compile_definitions(file_operations_windows_test PRIVATE FILEOPS_ENABLE_TEST_API)
    rpi_discover_tests(file_operations_windows_test)
endif()

# ============================================================================
# DiskpartUtil -- Windows only
# ============================================================================
# The code that wipes a card's partition table before a write, and locks and
# dismounts its volumes on the way. It had no test of any kind: until the
# support library above, no test target even compiled it.
#
# The device cases want a VHD attached, which needs an elevated process, so an
# ordinary run exercises the parsing and skips the rest -- the same bargain
# file_operations_test strikes with loop devices on Linux. virtdisk carries
# CreateVirtualDisk and friends for vhd_device.h.
if(WIN32)
    # PLATFORM_QUIRKS_SRC as well: DiskpartUtil reaches
    # PlatformQuirks::getEjectDevicePath() on both the unmount and clean paths.
    # And the relay as the executable Windows starts, which is where the
    # socket path lives. Depends on the relay target so it is there to run.
    add_executable(callback_relay_process_test callback_relay_process_test.cpp)
    target_include_directories(callback_relay_process_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(callback_relay_process_test PRIVATE
        Catch2::Catch2WithMain Qt6::Core Qt6::Network)
    target_compile_definitions(callback_relay_process_test PRIVATE
        RPI_IMAGER_RELAY_EXE="$<TARGET_FILE:rpi-imager-callback-relay>")
    add_dependencies(callback_relay_process_test rpi-imager-callback-relay)
    target_compile_features(callback_relay_process_test PRIVATE cxx_std_20)
    # Every case binds the relay's fixed port, so two at once means one of
    # them skips rather than runs.
    rpi_discover_tests(callback_relay_process_test RESOURCE_LOCK relay_callback_port)

    # Reading the key out of a WLAN profile. The rest of that file wants the
    # WLAN service and a wireless card; the decoding does not, and it is what
    # decides the password written to the card.
    add_executable(wlan_profile_xml_test wlan_profile_xml_test.cpp)
    target_include_directories(wlan_profile_xml_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(wlan_profile_xml_test PRIVATE Catch2::Catch2WithMain Qt6::Core)
    target_compile_features(wlan_profile_xml_test PRIVATE cxx_std_20)
    rpi_discover_tests(wlan_profile_xml_test)

    # The callback relay is a WIN32 executable of its own, so nothing linked it
    # and none of it was measured. What it accepts from the shell is the one
    # place an outside application's string enters the product here.
    add_executable(callback_relay_url_test callback_relay_url_test.cpp)
    target_include_directories(callback_relay_url_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(callback_relay_url_test PRIVATE Catch2::Catch2WithMain)
    target_compile_features(callback_relay_url_test PRIVATE cxx_std_20)
    rpi_discover_tests(callback_relay_url_test)

    add_executable(diskpart_util_test
        diskpart_util_test.cpp
        ${PLATFORM_QUIRKS_SRC})
    target_include_directories(diskpart_util_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(diskpart_util_test PRIVATE
        Catch2::Catch2WithMain Qt6::Core virtdisk)
    rpi_link_platform_backends(diskpart_util_test)
    target_compile_features(diskpart_util_test PRIVATE cxx_std_20)
    rpi_discover_tests(diskpart_util_test)
endif()

# ============================================================================
# Drivelist Tests
# ============================================================================

# Determine platform-specific drivelist implementation
if(WIN32)
    set(DRIVELIST_PLATFORM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_windows.cpp
    )
    set(DRIVELIST_PLATFORM_LIBS setupapi)
elseif(APPLE)
    set(DRIVELIST_PLATFORM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_darwin.mm
    )
    set(DRIVELIST_PLATFORM_LIBS
        "-framework DiskArbitration"
        "-framework IOKit"
        "-framework Cocoa"
        "-framework CoreFoundation"
    )
else()
    set(DRIVELIST_PLATFORM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_linux.cpp
    )
    set(DRIVELIST_PLATFORM_LIBS "")
endif()

# Add the drivelist test executable
add_executable(drivelist_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist.h
    ${DRIVELIST_PLATFORM_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_test.cpp
)

# Enable test API for access to parsing functions
target_compile_definitions(drivelist_test PRIVATE DRIVELIST_ENABLE_TEST_API)

# Link against Catch2, Qt, and platform libraries
target_link_libraries(drivelist_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${DRIVELIST_PLATFORM_LIBS}
)

# Include parent directory for headers
target_include_directories(drivelist_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(drivelist_test PRIVATE cxx_std_20)
target_compile_options(drivelist_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

# Register with CTest for automatic test discovery
rpi_discover_tests(drivelist_test)

# The drive list when Windows refuses to enumerate at all.
#
# Its own binary because the refusal is forced with --wrap, which applies to
# everything linked into the target: every SetupDiGetClassDevsW in here
# fails, so nothing else can share it. The drive list itself is compiled
# unchanged and carries no seam -- a call into a DLL goes through
# __imp_<name>, a data symbol holding the address, so the wrap redirects a
# pointer rather than a function.
if(WIN32)
    add_executable(drivelist_enum_failure_test
        ${DRIVELIST_PLATFORM_SOURCES}
        drivelist_enum_failure_wrap.cpp
        drivelist_enum_failure_test.cpp
    )
    target_compile_definitions(drivelist_enum_failure_test PRIVATE DRIVELIST_ENABLE_TEST_API)
    target_link_libraries(drivelist_enum_failure_test PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        ${DRIVELIST_PLATFORM_LIBS}
    )
    target_link_options(drivelist_enum_failure_test PRIVATE
        -Wl,--wrap=__imp_SetupDiGetClassDevsW)
    target_include_directories(drivelist_enum_failure_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_features(drivelist_enum_failure_test PRIVATE cxx_std_20)
    rpi_discover_tests(drivelist_enum_failure_test)
endif()

# The formatter when it cannot get a buffer.
#
# Same reason as above for a binary of its own: --wrap applies to everything
# linked with it, so every aligned allocation in here goes through the
# injector. Qt6::Core because the case uses QTemporaryDir.
if(WIN32)
    add_executable(disk_formatter_alloc_test
        ${CMAKE_CURRENT_SOURCE_DIR}/../disk_formatter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
        ${PLATFORM_FILE_OPS}
        aligned_alloc_fault.cpp
        disk_formatter_alloc_test.cpp
    )
    target_link_libraries(disk_formatter_alloc_test PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
    )
    target_link_options(disk_formatter_alloc_test PRIVATE
        -Wl,--wrap=__imp__aligned_malloc)
    target_include_directories(disk_formatter_alloc_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_features(disk_formatter_alloc_test PRIVATE cxx_std_20)
    rpi_discover_tests(disk_formatter_alloc_test)
endif()

# WinFile when the volume lock is granted and when the sync will not finish.
#
# A binary of its own for the same reason as the two above: both wraps apply
# to everything linked with them.
if(WIN32)
    add_executable(winfile_fault_test
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/winfile.cpp
        winfile_fault.cpp
        winfile_fault_test.cpp
    )
    # WinFile is a QObject, so its vtable comes from moc.
    set_target_properties(winfile_fault_test PROPERTIES AUTOMOC ON)
    target_link_libraries(winfile_fault_test PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
    )
    target_link_options(winfile_fault_test PRIVATE
        -Wl,--wrap=__imp_DeviceIoControl
        -Wl,--wrap=__imp_FlushFileBuffers)
    target_include_directories(winfile_fault_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_features(winfile_fault_test PRIVATE cxx_std_20)
    rpi_discover_tests(winfile_fault_test)
endif()

# The rpiboot scan when libusb will not start.
#
# libusb is linked statically, so this wraps the plain symbol rather than an
# import pointer. Its own binary, like the others: the wrap is target-wide.
add_executable(libusb_init_fault_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_scanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/libusb_transport.cpp
    libusb_init_fault_test.cpp
)
target_link_libraries(libusb_init_fault_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${LIBUSB_LIBRARIES}
)
target_link_options(libusb_init_fault_test PRIVATE -Wl,--wrap=libusb_init)
target_include_directories(libusb_init_fault_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(libusb_init_fault_test PRIVATE cxx_std_20)
rpi_discover_tests(libusb_init_fault_test)

# Custom target for manual running
add_custom_target(test_drivelist
    COMMAND drivelist_test
    DEPENDS drivelist_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running drivelist tests"
)

# ============================================================================
# PlatformQuirks Tests
# ============================================================================

# Kept as names of their own because two targets read them, but the choice
# itself is the one made once above.
set(PLATFORMQUIRKS_SOURCES ${PLATFORM_QUIRKS_SRC})
set(PLATFORMQUIRKS_LIBS ${PLATFORM_BACKEND_LIBS})

# Add the platformquirks test executable
add_executable(platformquirks_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../platformquirks.h
    ${PLATFORMQUIRKS_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/platformquirks_test.cpp
)

# Enable test API for Windows parseDeviceNumber
target_compile_definitions(platformquirks_test PRIVATE PLATFORMQUIRKS_ENABLE_TEST_API)

# Link against Catch2, Qt, and platform libraries
target_link_libraries(platformquirks_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${PLATFORMQUIRKS_LIBS}
)

# Include parent directory for headers
target_include_directories(platformquirks_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(platformquirks_test PRIVATE cxx_std_20)
target_compile_options(platformquirks_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

# Register with CTest for automatic test discovery
if(WIN32)
    # virtdisk for vhd_device.h, reached through TestBlockDevice: a
    # virtual disk is what stands in for a loop device here.
    target_link_libraries(platformquirks_test PRIVATE virtdisk)
endif()

rpi_discover_tests(platformquirks_test)

# A real executable for the launch cases to launch, on every platform. They used
# to write a shell script, which cannot be run on Windows at all, and a .cmd
# rewrite of it measured cmd.exe's command-line parsing rather than
# launchDetached's -- a URL containing "&id=abc" came back as two arguments
# whatever the code under test had done. An .exe is handed its arguments by the
# OS and reports what it actually received.
add_executable(argument_recorder_probe
    ${CMAKE_CURRENT_SOURCE_DIR}/argument_recorder_probe.cpp)
# Qt on Windows only: see the probe for why the POSIX build must not need it.
if(WIN32)
    target_link_libraries(argument_recorder_probe PRIVATE Qt6::Core)
endif()
target_compile_features(argument_recorder_probe PRIVATE cxx_std_20)
add_dependencies(platformquirks_test argument_recorder_probe)
target_compile_definitions(platformquirks_test PRIVATE
    ARGUMENT_RECORDER_BINARY="$<TARGET_FILE:argument_recorder_probe>")

# Custom target for manual running
add_custom_target(test_platformquirks
    COMMAND platformquirks_test
    DEPENDS platformquirks_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running platformquirks tests"
)

# ============================================================================
# rpiboot Tests
# ============================================================================

# rpiboot protocol tests (bootcode loader, file server, types)
add_executable(rpiboot_protocol_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootcode_loader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootcode_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/file_server.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/file_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/test/mock_usb_transport.h
    rpiboot_protocol_test.cpp
)

target_link_libraries(rpiboot_protocol_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${LibArchive_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

target_include_directories(rpiboot_protocol_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LibArchive_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${LIBLZMA_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIR}
)

target_compile_features(rpiboot_protocol_test PRIVATE cxx_std_20)
target_compile_options(rpiboot_protocol_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(rpiboot_protocol_test)

add_custom_target(test_rpiboot_protocol
    COMMAND rpiboot_protocol_test
    DEPENDS rpiboot_protocol_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running rpiboot protocol tests"
)

# Bootfiles (tar extraction) tests
add_executable(rpiboot_bootfiles_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.cpp
    rpiboot_bootfiles_test.cpp
)

target_link_libraries(rpiboot_bootfiles_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${LibArchive_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

target_include_directories(rpiboot_bootfiles_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LibArchive_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${LIBLZMA_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIR}
)

target_compile_features(rpiboot_bootfiles_test PRIVATE cxx_std_20)
target_compile_options(rpiboot_bootfiles_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(rpiboot_bootfiles_test)

add_custom_target(test_rpiboot_bootfiles
    COMMAND rpiboot_bootfiles_test
    DEPENDS rpiboot_bootfiles_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running rpiboot bootfiles tests"
)

# Fastboot protocol tests
add_executable(fastboot_protocol_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/test/mock_usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.cpp
    fastboot_protocol_test.cpp
)

target_link_libraries(fastboot_protocol_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_include_directories(fastboot_protocol_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(fastboot_protocol_test PRIVATE cxx_std_20)
target_compile_options(fastboot_protocol_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(fastboot_protocol_test)

add_custom_target(test_fastboot_protocol
    COMMAND fastboot_protocol_test
    DEPENDS fastboot_protocol_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running fastboot protocol tests"
)

# Sparse encoder tests
add_executable(sparse_encoder_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/sparse_encoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/sparse_encoder.cpp
    # setBlockMap() is half of what the encoder does on a real write, and the
    # only map it is ever handed came out of this parser. Qt comes with it:
    # the bmap is XML.
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/bmap.cpp
    sparse_encoder_test.cpp
)

target_link_libraries(sparse_encoder_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_include_directories(sparse_encoder_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(sparse_encoder_test PRIVATE cxx_std_20)
target_compile_options(sparse_encoder_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(sparse_encoder_test)

add_custom_target(test_sparse_encoder
    COMMAND sparse_encoder_test
    DEPENDS sparse_encoder_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running sparse encoder tests"
)

# Pieeprom format tests (parse/edit pieeprom.bin, BOOT_ORDER helpers)
add_executable(pieeprom_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/pieeprom.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/pieeprom.cpp
    pieeprom_test.cpp
)
target_link_libraries(pieeprom_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)
target_include_directories(pieeprom_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(pieeprom_test PRIVATE cxx_std_20)
target_compile_options(pieeprom_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(pieeprom_test)

add_custom_target(test_pieeprom
    COMMAND pieeprom_test
    DEPENDS pieeprom_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running pieeprom format tests"
)

# BootloaderImage tests (AB-capable pieeprom: bootsys signing/extraction)
add_executable(bootloader_image_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootloader_image.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootloader_image.cpp
    bootloader_image_test.cpp
)
target_link_libraries(bootloader_image_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)
target_include_directories(bootloader_image_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(bootloader_image_test PRIVATE cxx_std_20)
target_compile_options(bootloader_image_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(bootloader_image_test)

add_custom_target(test_bootloader_image
    COMMAND bootloader_image_test
    DEPENDS bootloader_image_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running BootloaderImage (AB pieeprom) tests"
)

# Fastboot EEPROM wire-protocol tests
add_executable(fastboot_eeprom_wire_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/test/mock_usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.cpp
    fastboot_eeprom_wire_test.cpp
)
target_link_libraries(fastboot_eeprom_wire_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)
target_include_directories(fastboot_eeprom_wire_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(fastboot_eeprom_wire_test PRIVATE cxx_std_20)
target_compile_options(fastboot_eeprom_wire_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(fastboot_eeprom_wire_test)

add_custom_target(test_fastboot_eeprom_wire
    COMMAND fastboot_eeprom_wire_test
    DEPENDS fastboot_eeprom_wire_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running fastboot EEPROM wire tests"
)

# EEPROM signer tests (in-process .sig builder + cross-check vs rpi-eeprom-digest)
set(EEPROM_SIGNER_PLATFORM_SRC ${PLATFORM_SECUREBOOT_CRYPTO_SRC})

add_executable(eeprom_signer_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/eeprom_signer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/eeprom_signer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot_crypto.h
    ${EEPROM_SIGNER_PLATFORM_SRC}
    eeprom_signer_test.cpp
)
target_link_libraries(eeprom_signer_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)
if(APPLE)
    target_link_libraries(eeprom_signer_test PRIVATE
        "-framework Security" "-framework CoreFoundation"
    )
elseif(WIN32)
    target_link_libraries(eeprom_signer_test PRIVATE
        crypt32 bcrypt ncrypt
    )
endif()
target_include_directories(eeprom_signer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(eeprom_signer_test PRIVATE cxx_std_20)
target_compile_options(eeprom_signer_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(eeprom_signer_test)

add_custom_target(test_eeprom_signer
    COMMAND eeprom_signer_test
    DEPENDS eeprom_signer_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running EEPROM signer tests"
)

if(NOT WIN32)
# Tests for the real libusb transport, run against an emulated USB device
# (see rpiboot_usb_device_test.cpp and data/usb_gadget_emulator.sh). Kept
# separate from rpiboot_integration_test because that one wants a Compute
# Module plugged in and these bring their own; they skip with a reason when
# the machine cannot emulate one.
add_executable(rpiboot_usb_device_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/libusb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/libusb_transport.cpp
    rpiboot_usb_device_test.cpp
)

target_link_libraries(rpiboot_usb_device_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${LIBUSB_LIBRARIES}
)

target_include_directories(rpiboot_usb_device_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LIBUSB_INCLUDE_DIR}
)

target_compile_definitions(rpiboot_usb_device_test PRIVATE
    IMAGER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

target_compile_features(rpiboot_usb_device_test PRIVATE cxx_std_20)
target_compile_options(rpiboot_usb_device_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

# Not rpi_discover_tests: every case here brings the same emulated device up
# and takes it down again, so two of them running at once under `ctest -j`
# would pull the device out from under each other. The resource lock makes
# ctest run them one at a time while still allowing the rest of the suite to
# run alongside.
catch_discover_tests(rpiboot_usb_device_test
    PROPERTIES
        SKIP_RETURN_CODE 4
        RESOURCE_LOCK usb_gadget_emulator
        TIMEOUT 300
)
endif()

# Hardware integration tests (gated by RPIBOOT_TEST_DEVICE env var)
add_executable(rpiboot_integration_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/libusb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/libusb_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootcode_loader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootcode_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/file_server.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/file_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/firmware_manager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/firmware_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    # firmware_manager reaches the secure-boot provisioner, which reaches
    # SecureBoot and its crypto backends; curlnetworkconfig reaches
    # PlatformQuirks for the CA bundle. Without these the target did not link
    # at all, so CTest registered it as rpiboot_integration_test_NOT_BUILT and
    # reported a permanent failure, and RunCoverage.cmake warned on every run
    # that not everything had built.
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/secure_boot_provisioner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootloader_image.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.cpp
    ${PLATFORM_SECUREBOOT_CRYPTO_SRC}
    ${PLATFORM_BOOTIMGCREATOR_SRC}
    ${PLATFORM_ACCEL_HASH_SRC}
    ${PLATFORM_QUIRKS_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    rpiboot_integration_test.cpp
)

target_link_libraries(rpiboot_integration_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${LIBUSB_LIBRARIES}
    ${CURL_LIBRARIES}
    ${LibArchive_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${ZSTD_LIBRARIES}
)
rpi_link_platform_backends(rpiboot_integration_test)

target_include_directories(rpiboot_integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LIBUSB_INCLUDE_DIR}
    ${CURL_INCLUDE_DIR}
    ${LibArchive_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${LIBLZMA_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIR}
)

target_compile_features(rpiboot_integration_test PRIVATE cxx_std_20)
target_compile_options(rpiboot_integration_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
# PLATFORM_FILE_OPS brings in the io_uring write path, so the target needs
# liburing on the link line as well as in its sources.
# The device wrapper hierarchy and the platform file classes are QObjects,
# and moc emits their vtables: without this the target does not link.
set_target_properties(rpiboot_integration_test PROPERTIES AUTOMOC ON)

rpi_link_platform_file_ops(rpiboot_integration_test)

rpi_discover_tests(rpiboot_integration_test)

add_custom_target(test_rpiboot_integration
    COMMAND rpiboot_integration_test
    DEPENDS rpiboot_integration_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running rpiboot integration tests (requires hardware)"
)

# Combined target for all rpiboot tests
add_custom_target(test_rpiboot
    DEPENDS rpiboot_protocol_test rpiboot_bootfiles_test fastboot_protocol_test sparse_encoder_test
    COMMENT "Running all rpiboot unit tests"
)

# ============================================================================
# Disk Formatter Tests
# ============================================================================

# Standalone test (not Catch2). The loop-device section is POSIX and is
# compiled out elsewhere; everything else is arithmetic over the bytes a
# format lays down, and had no counterpart on Windows while the whole binary
# was skipped there.
add_executable(disk_formatter_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../disk_formatter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../disk_formatter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../aligned_buffer.h
    ${PLATFORM_FILE_OPS}
    disk_formatter_test.cpp
)

# Enable Qt MOC for Q_OBJECT classes (e.g. MacFile on macOS)
set_target_properties(disk_formatter_test PROPERTIES AUTOMOC ON)

target_include_directories(disk_formatter_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Qt6::Core needed by platform file_operations (QFile, QDebug)
target_link_libraries(disk_formatter_test PRIVATE
    Qt6::Core
)

# Reaches ComputeGeometry on its own. Driving it through a whole format only
# ever asks it the sizes a format is given, which leaves the degenerate ones
# -- a partition no larger than its own reserved sectors, or one whose FATs
# fill it -- with nothing holding them.
target_compile_definitions(disk_formatter_test PRIVATE DISKFORMATTER_ENABLE_TEST_API)

target_compile_features(disk_formatter_test PRIVATE cxx_std_20)
target_compile_options(disk_formatter_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

if(APPLE)
    target_link_libraries(disk_formatter_test PRIVATE
        "-framework Security"
        "-framework DiskArbitration"
        "-framework CoreFoundation"
    )
endif()

rpi_link_platform_file_ops(disk_formatter_test)

# Not Catch2, so rpi_discover_tests does not apply -- register it directly.
# Until now this binary was built but never registered, which left
# file_operations.cpp and the platform file_operations implementation with
# no test that actually runs. The TIMEOUT is a backstop: the loop-device
# section shells out, and while every sudo in it now passes -n, a wedged
# external tool should fail the suite rather than hang it.
add_test(NAME disk_formatter COMMAND disk_formatter_test)
set_tests_properties(disk_formatter PROPERTIES TIMEOUT 120)

add_custom_target(test_disk_formatter
    COMMAND disk_formatter_test
    DEPENDS disk_formatter_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running disk formatter tests"
)

# ============================================================================
# Device I/O Limits Diagnostic Test
# ============================================================================
# Reports raw DeviceIOLimits values for all connected drives.
# Validates the queue-depth and max-transfer heuristics against real hardware.

add_executable(device_io_limits_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    device_io_limits_test.cpp
)

set_target_properties(device_io_limits_test PROPERTIES AUTOMOC ON)

target_include_directories(device_io_limits_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(device_io_limits_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

if(APPLE)
    target_link_libraries(device_io_limits_test PRIVATE
        "-framework Security"
        "-framework DiskArbitration"
        "-framework CoreFoundation"
    )
endif()

rpi_link_platform_file_ops(device_io_limits_test)

target_compile_features(device_io_limits_test PRIVATE cxx_std_20)

rpi_discover_tests(device_io_limits_test)

add_custom_target(test_device_io_limits
    COMMAND device_io_limits_test
    DEPENDS device_io_limits_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running device I/O limits diagnostic"
)
# BlockBatcher coalesces libarchive data blocks into larger writes during
# multi-file extraction. Header-only and free of Qt/libarchive, so it tests without
# a download, a ring buffer or a device attached.
add_executable(block_batcher_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../block_batcher.h
    block_batcher_test.cpp
)

target_link_libraries(block_batcher_test PRIVATE
    Catch2::Catch2WithMain
)

target_include_directories(block_batcher_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(block_batcher_test PRIVATE cxx_std_20)
target_compile_options(block_batcher_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

rpi_discover_tests(block_batcher_test)

add_custom_target(test_block_batcher
    COMMAND block_batcher_test
    DEPENDS block_batcher_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running block batcher tests"
)

# ============================================================================
# RingBuffer Slot Recycling Tests
# ============================================================================
# The write path uses the ring buffer as a slot pool for asynchronous I/O, whose
# completions release slots out of order. Covers that a released slot -- and only
# that slot -- is recycled, so a buffer still being read from is never handed
# back to the producer. Needs Qt6::Core for qMallocAligned/qDebug.

add_executable(ringbuffer_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../ringbuffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ringbuffer.cpp
    ringbuffer_test.cpp
)

target_include_directories(ringbuffer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(ringbuffer_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(ringbuffer_test PRIVATE cxx_std_20)
target_compile_options(ringbuffer_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

rpi_discover_tests(ringbuffer_test)

add_custom_target(test_ringbuffer
    COMMAND ringbuffer_test
    DEPENDS ringbuffer_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running ring buffer tests"
)

# ============================================================================
# runWithTimeout Lifetime Hazards
# ============================================================================
# runWithTimeout() detaches its worker thread on both the timeout and the
# cancellation path, abandoning a thread that still holds references to the
# promise and completion flag on the frame being returned from. These cases
# provoke that deliberately so a sanitiser can be pointed at it.
#
# The cases are tagged [.timeout-hazard] -- hidden, so catch_discover_tests
# does not register them and an ordinary `ctest` run never executes them. That
# is deliberate: without instrumentation the detached worker writes through a
# dangling reference and segfaults the binary, which is the behaviour under
# test but is not a useful CI signal. Only the [timeout-utils] control case,
# which takes the joined path, runs by default.
#
# Build with RPI_IMAGER_TEST_SANITIZER and select the hidden tag explicitly:
#
#   cmake -B build-asan -DBUILD_TESTING=ON -DRPI_IMAGER_TEST_SANITIZER=address
#   cmake --build build-asan --target timeout_utils_test
#   ASAN_OPTIONS=detect_stack_use_after_return=1 \
#       ./build-asan/src/test/timeout_utils_test "[timeout-hazard]"
#
# ASan reports stack-use-after-return at timeout_utils.h:100 and TSan reports
# heap-use-after-free between timeout_utils.h:101 and the promise destructor.
# Note that ASan stops at the first report, so run the two hazard cases
# separately to see both.

set(RPI_IMAGER_TEST_SANITIZER "none" CACHE STRING
    "Sanitizer to build timeout_utils_test with: none, address or thread")
set_property(CACHE RPI_IMAGER_TEST_SANITIZER PROPERTY STRINGS none address thread)

# Header-only and free of Qt, so this builds without a device or a download --
# which also means nothing else has pulled in the thread library for us.
find_package(Threads REQUIRED)

add_executable(timeout_utils_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../timeout_utils.h
    timeout_utils_test.cpp
)

target_link_libraries(timeout_utils_test PRIVATE
    Catch2::Catch2WithMain
    Threads::Threads
)

target_include_directories(timeout_utils_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(timeout_utils_test PRIVATE cxx_std_20)
target_compile_options(timeout_utils_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

if(NOT RPI_IMAGER_TEST_SANITIZER STREQUAL "none")
    if(MSVC)
        message(FATAL_ERROR "RPI_IMAGER_TEST_SANITIZER needs a GCC/Clang-style driver")
    endif()
    # Optimisation level is chosen per sanitiser, because it changes which
    # diagnosis you get:
    #
    #   ASan at -O0  -- runWithTimeout stays out of line, so the report is
    #                   stack-use-after-return against runWithTimeout's own
    #                   frame, naming `completed` at timeout_utils.h:100.
    #                   Inlined at -O1+ it still fires, but as
    #                   stack-use-after-scope against the caller's frame.
    #   TSan at -O1  -- reports heap-use-after-free between set_value() at
    #                   timeout_utils.h:101 and ~promise() at
    #                   timeout_utils.h:129, which states the bug outright.
    #                   At -O0 it reports instead as a plain data race on the
    #                   dead frame; still a true positive, but it fingers the
    #                   test's own clobber helper rather than the destructor.
    if(RPI_IMAGER_TEST_SANITIZER STREQUAL "thread")
        set(_hazard_opt -O1)
    else()
        set(_hazard_opt -O0)
    endif()
    target_compile_options(timeout_utils_test PRIVATE
        -fsanitize=${RPI_IMAGER_TEST_SANITIZER} -fno-omit-frame-pointer -g ${_hazard_opt})
    target_link_options(timeout_utils_test PRIVATE
        -fsanitize=${RPI_IMAGER_TEST_SANITIZER})
    # Catch2 itself is not instrumented by this option. Harmless here -- it
    # runs single-threaded and owns none of the memory under test.
    message(STATUS "timeout_utils_test: building with -fsanitize=${RPI_IMAGER_TEST_SANITIZER}")
endif()

rpi_discover_tests(timeout_utils_test)

add_custom_target(test_timeout_utils
    COMMAND timeout_utils_test
    DEPENDS timeout_utils_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running timeout utils tests (hazard cases excluded)"
)

# ============================================================================
# Embedded Display Scaling Matrix
# ============================================================================
# applyEmbeddedDisplayScaling() chooses the embedded UI scale by reading the
# connected display out of /sys/class/drm, so the cases that matter -- a
# 43-inch-class 4K TV on a desk, the same set with no usable EDID, a DSI panel
# that reports no physical size -- are unreachable from an ordinary unit test,
# which only ever sees the build host's own display.
#
# embedded_scaling/run.sh drives the real code instead: per case it builds a
# synthetic /sys/class/drm, bind-mounts it over the real one inside an
# unprivileged mount namespace, and checks the factor chosen. This probe is
# what it runs inside that namespace. Linux-only, and it skips itself (exit 4)
# where the host cannot provide the namespace.

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(embedded_scaling_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/../platformquirks.h
        ${PLATFORMQUIRKS_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/embedded_scaling_probe.cpp
    )

    target_link_libraries(embedded_scaling_probe PRIVATE
        Qt6::Core
        ${PLATFORMQUIRKS_LIBS}
    )

    target_include_directories(embedded_scaling_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_compile_features(embedded_scaling_probe PRIVATE cxx_std_20)
    target_compile_options(embedded_scaling_probe PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
    )

    # The other half of platformquirks that cannot be reached in-process: the
    # sudo/pkexec identity handover in applyQuirks(), which only runs when euid
    # is 0. platformquirks_test drives this probe under sudo and reads the
    # environment back off its stdout; without sudo those cases skip.
    add_executable(platform_quirks_elevation_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/../platformquirks.h
        ${PLATFORMQUIRKS_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/platform_quirks_elevation_probe.cpp
    )

    target_link_libraries(platform_quirks_elevation_probe PRIVATE
        Qt6::Core
        ${PLATFORMQUIRKS_LIBS}
    )

    target_include_directories(platform_quirks_elevation_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_compile_features(platform_quirks_elevation_probe PRIVATE cxx_std_20)
    target_compile_options(platform_quirks_elevation_probe PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
    )

    target_compile_definitions(platformquirks_test PRIVATE
        ELEVATION_PROBE_BINARY="$<TARGET_FILE:platform_quirks_elevation_probe>")
    add_dependencies(platformquirks_test platform_quirks_elevation_probe)

    # hasNetworkConnectivity() walks /sys/class/net, which is hardcoded, so
    # platformquirks_test bind-mounts a synthetic one over it in an
    # unprivileged mount namespace and runs this probe inside. Same technique
    # as embedded_scaling/run.sh; the cases skip where unshare -rm is not
    # available.
    add_executable(platform_quirks_network_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/../platformquirks.h
        ${PLATFORMQUIRKS_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/platform_quirks_network_probe.cpp
    )

    # Qt6::DBus so the probe matches what the application links. Without it
    # QT_DBUS_LIB is undefined and openUriViaPortal() -- the path
    # openUrlExternally() prefers -- is compiled out of the probe entirely,
    # so no test could reach it however the bus was arranged.
    target_link_libraries(platform_quirks_network_probe PRIVATE
        Qt6::Core
        $<TARGET_NAME_IF_EXISTS:Qt6::DBus>
        ${PLATFORMQUIRKS_LIBS}
    )

    target_include_directories(platform_quirks_network_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_compile_features(platform_quirks_network_probe PRIVATE cxx_std_20)
    target_compile_options(platform_quirks_network_probe PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
    )

    target_compile_definitions(platformquirks_test PRIVATE
        NETWORK_PROBE_BINARY="$<TARGET_FILE:platform_quirks_network_probe>")
    add_dependencies(platformquirks_test platform_quirks_network_probe)

    # isBeepAvailable() and beep() find their audio tools through PATH, but
    # cache each answer for the life of the process with std::call_once, so
    # they cannot be driven in-process. platformquirks_test runs this once per
    # case with PATH pointed at a directory of fakes.
    # The bundled chime, so the probe can reach the fallback that extracts
    # it when a machine has no system sound file. Carried in a qrc of its
    # own rather than pulling in src/qml.qrc, which is the whole UI.
    qt_add_resources(BEEP_PROBE_RESOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/beep_probe_resources.qrc)

    add_executable(platform_quirks_beep_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/../platformquirks.h
        ${PLATFORMQUIRKS_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/platform_quirks_beep_probe.cpp
        ${BEEP_PROBE_RESOURCES}
    )
    target_link_libraries(platform_quirks_beep_probe PRIVATE
        Qt6::Core
        ${PLATFORMQUIRKS_LIBS}
    )
    target_include_directories(platform_quirks_beep_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_features(platform_quirks_beep_probe PRIVATE cxx_std_20)
    target_compile_options(platform_quirks_beep_probe PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
    )
    target_compile_definitions(platformquirks_test PRIVATE
        BEEP_PROBE_BINARY="$<TARGET_FILE:platform_quirks_beep_probe>")
    add_dependencies(platformquirks_test platform_quirks_beep_probe)

    # secureSettingsFile() hands a root-owned settings file back to the user
    # before narrowing it, and that needs a real root to exercise.
    # image_writer_test runs this inside `unshare -r --map-auto`, where the
    # invoking account is uid 0 and a range of further uids is mapped too, so
    # a file can belong to one account while another secures it.
    add_executable(settings_permissions_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/../settings_permissions.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/settings_permissions_probe.cpp
    )
    target_link_libraries(settings_permissions_probe PRIVATE Qt6::Core)
    target_include_directories(settings_permissions_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_features(settings_permissions_probe PRIVATE cxx_std_20)
    target_compile_options(settings_permissions_probe PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
    )

    # prefersReducedMotion() runs /usr/bin/gsettings and /usr/bin/kreadconfig6
    # by absolute path -- deliberately, since it can run as root after pkexec
    # -- so neither can be redirected with PATH. platformquirks_test binds
    # substitutes over them in an unprivileged mount namespace and runs this
    # probe inside.
    add_executable(platform_quirks_motion_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/../platformquirks.h
        ${PLATFORMQUIRKS_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/platform_quirks_motion_probe.cpp
    )

    target_link_libraries(platform_quirks_motion_probe PRIVATE
        Qt6::Core
        ${PLATFORMQUIRKS_LIBS}
    )

    target_include_directories(platform_quirks_motion_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_compile_features(platform_quirks_motion_probe PRIVATE cxx_std_20)
    target_compile_options(platform_quirks_motion_probe PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
    )

    target_compile_definitions(platformquirks_test PRIVATE
        MOTION_PROBE_BINARY="$<TARGET_FILE:platform_quirks_motion_probe>")
    add_dependencies(platformquirks_test platform_quirks_motion_probe)

    # unmountDisk() calls umount(2) in this process, so its existing cases
    # skip unless the whole suite runs as root -- which it does not. The
    # probe runs under `unshare -rm`, where it is uid 0 over a mount
    # namespace of its own: the mounts it makes are invisible outside and no
    # real device is involved.
    add_executable(platform_quirks_unmount_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/../platformquirks.h
        ${PLATFORMQUIRKS_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/platform_quirks_unmount_probe.cpp
    )
    target_link_libraries(platform_quirks_unmount_probe PRIVATE
        Qt6::Core
        ${PLATFORMQUIRKS_LIBS}
    )
    target_include_directories(platform_quirks_unmount_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_features(platform_quirks_unmount_probe PRIVATE cxx_std_20)
    target_compile_options(platform_quirks_unmount_probe PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
    )
    target_compile_definitions(platformquirks_test PRIVATE
        UNMOUNT_PROBE_BINARY="$<TARGET_FILE:platform_quirks_unmount_probe>")
    add_dependencies(platformquirks_test platform_quirks_unmount_probe)

    # detectPiKeyboard() reads /dev/input/by-id, which does not exist on a
    # machine with no Raspberry Pi keyboard attached and cannot be redirected
    # in-process. image_writer_test bind-mounts a synthetic /dev/input over
    # the real one in an unprivileged mount namespace and runs this probe
    # inside; the cases skip where unshare -rm is unavailable.
    add_executable(keyboard_detect_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/keyboard_detect_probe.cpp
    )
    set_target_properties(keyboard_detect_probe PROPERTIES AUTOMOC ON)
    target_link_libraries(keyboard_detect_probe PRIVATE
        rpi_imager_testable Qt6::Core)
    target_include_directories(keyboard_detect_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_features(keyboard_detect_probe PRIVATE cxx_std_20)

    add_test(NAME embedded_scaling_matrix
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/embedded_scaling/run.sh
                $<TARGET_FILE:embedded_scaling_probe>
    )
    set_tests_properties(embedded_scaling_matrix PROPERTIES SKIP_RETURN_CODE 4)

    add_custom_target(test_embedded_scaling
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/embedded_scaling/run.sh
                $<TARGET_FILE:embedded_scaling_probe>
        DEPENDS embedded_scaling_probe
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running embedded display scaling matrix"
    )

    # The matrix above proves which factor is chosen; screenshots.sh renders the
    # UI at it. That needs the app itself, so the target only appears in a GUI
    # build; only an embedded one lays out the way the netboot imager does; and
    # it needs the screenshot hook, which is compiled out unless a test build
    # asks for it (-DENABLE_TEST_HOOKS=ON). Not a CTest test: it wants fonts and
    # several seconds per profile, and its output is meant to be looked at.
    if(TARGET rpi-imager AND BUILD_EMBEDDED AND ENABLE_TEST_HOOKS)
        add_custom_target(screenshots_embedded_scaling
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/embedded_scaling/screenshots.sh
                    $<TARGET_FILE:rpi-imager>
                    ${CMAKE_CURRENT_BINARY_DIR}/embedded_scaling_screenshots
            DEPENDS rpi-imager
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Rendering the embedded UI at each display profile"
        )
    endif()

    # The desktop counterpart, sharing the same screenshot hook but none of the
    # display fixtures: it photographs the published UI rather than judging a
    # scale factor, so it wants the desktop layout and needs nothing synthesised
    # about the panel. Its output is what the AppStream metainfo and the store
    # listings point at, which is why it is a target you run and look at rather
    # than a CTest test.
    if(TARGET rpi-imager AND NOT BUILD_EMBEDDED AND ENABLE_TEST_HOOKS)
        add_custom_target(screenshots_desktop
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/screenshots/shots.sh
                    $<TARGET_FILE:rpi-imager>
                    ${CMAKE_CURRENT_BINARY_DIR}/desktop_screenshots
            DEPENDS rpi-imager
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Rendering the desktop UI, one PNG per wizard page"
        )
    endif()
endif()

# ============================================================================
# FileOperations Write Path
# ============================================================================
# The write path is the most safety-critical code in the imager and, until this
# target, had no test that ran: file_operations.cpp and the platform
# implementation behind it were reachable only from fat_partition_test (tagged
# [.destructive]) and disk_formatter_test (unregistered until recently).
#
# Every case works against a scratch file under a per-process temp directory,
# so it is safe to run unattended and touches no real device. The loopback
# cases attach that scratch file to a loop device to reach the block-device
# paths -- O_DIRECT, real size queries, aligned buffers -- and skip themselves
# where the host will not allow one, which is the normal case in containerised
# CI. Verified against a KVM guest, where the loop cases do run.

# Not on Windows. The cases are written against the POSIX backend throughout:
# O_DIRECT, open/pread/ftruncate on descriptors, RLIMIT_NOFILE to force the
# io_uring fallback, and a loop device for the block-device paths. None of
# that has a Windows counterpart, and file_operations_windows.cpp is a
# different implementation -- 1,876 lines of volume locking, dismount and
# sector-aligned overlapped I/O -- that wants cases of its own rather than a
# port of these. Guarded rather than half-ported, so the gap stays visible.
if(NOT WIN32)
add_executable(file_operations_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../aligned_buffer.h
    ${PLATFORM_FILE_OPS}
    file_operations_test.cpp
)

# Only the macOS set has a Q_OBJECT type in it (MacFile), so only that build
# needs the moc pass. The other test targets enable AUTOMOC unconditionally and
# pay for a scan that can never find anything.
if(APPLE)
    set_target_properties(file_operations_test PROPERTIES AUTOMOC ON)
endif()

target_include_directories(file_operations_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Qt6::Core for the QFile/qDebug use inside the platform implementations.
target_link_libraries(file_operations_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

if(APPLE)
    target_link_libraries(file_operations_test PRIVATE
        "-framework Security"
        "-framework DiskArbitration"
        "-framework CoreFoundation"
    )
endif()

target_compile_features(file_operations_test PRIVATE cxx_std_20)
target_compile_options(file_operations_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

rpi_link_platform_file_ops(file_operations_test)
rpi_discover_tests(file_operations_test FAULT_INJECTION)
endif()

add_custom_target(test_file_operations
    COMMAND file_operations_test
    DEPENDS file_operations_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running file operations write-path tests"
)

# ============================================================================
# Coverage Report
# ============================================================================
# `cmake --build <dir> --target coverage` with -DRPI_IMAGER_COVERAGE=ON runs
# the CTest suite and renders a gcovr HTML report of the headless core.
#
#   cmake -B build-cov -DBUILD_TESTING=ON -DRPI_IMAGER_COVERAGE=ON
#   cmake --build build-cov --target coverage
#   xdg-open build-cov/coverage/index.html
#
# The instrumentation itself is switched on in ../CMakeLists.txt; what lands in
# the report is decided by the exclusion list in RunCoverage.cmake. The report
# is branch-oriented on purpose -- line coverage alone would have called
# runWithTimeout's timeout path "reached" as soon as any test entered its poll
# loop, when in fact neither detach() had ever executed.

if(RPI_IMAGER_COVERAGE)
    # Two toolchains, because the instrumentation differs (see ../CMakeLists.txt).
    # GCC writes gcov data, read by gcovr. Clang writes its own profiles, read by
    # llvm-profdata and llvm-cov -- which gcovr can also drive, but only through
    # an export format two major versions behind the one Apple's llvm-cov emits,
    # so it is asked directly.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(RPI_COVERAGE_FLAVOUR "llvm")
    else()
        set(RPI_COVERAGE_FLAVOUR "gcov")
    endif()

    set(_coverage_tools_found TRUE)
    if(RPI_COVERAGE_FLAVOUR STREQUAL "llvm")
        # These ship inside the toolchain rather than on PATH, which is where
        # find_program looks; on macOS xcrun is how you ask for them.
        find_program(LLVM_PROFDATA_EXECUTABLE llvm-profdata)
        find_program(LLVM_COV_EXECUTABLE llvm-cov)
        if(APPLE AND (NOT LLVM_PROFDATA_EXECUTABLE OR NOT LLVM_COV_EXECUTABLE))
            execute_process(COMMAND xcrun --find llvm-profdata
                            OUTPUT_VARIABLE _xcrun_profdata
                            OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET)
            execute_process(COMMAND xcrun --find llvm-cov
                            OUTPUT_VARIABLE _xcrun_cov
                            OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET)
            if(_xcrun_profdata)
                set(LLVM_PROFDATA_EXECUTABLE "${_xcrun_profdata}" CACHE FILEPATH "" FORCE)
            endif()
            if(_xcrun_cov)
                set(LLVM_COV_EXECUTABLE "${_xcrun_cov}" CACHE FILEPATH "" FORCE)
            endif()
        endif()
        if(NOT LLVM_PROFDATA_EXECUTABLE OR NOT LLVM_COV_EXECUTABLE)
            set(_coverage_tools_found FALSE)
            message(WARNING
                "RPI_IMAGER_COVERAGE is on and this is a clang build, but llvm-profdata "
                "or llvm-cov was not found, so no `coverage` target was created. They "
                "come with the toolchain (xcode-select --install, or your llvm package).")
        endif()
        # Not used by the llvm path, but the report target below passes one
        # value for both, and an empty one reads as "not required here".
        set(GCOVR_EXECUTABLE "")
    else()
        find_program(GCOVR_EXECUTABLE gcovr)
        if(NOT GCOVR_EXECUTABLE)
            set(_coverage_tools_found FALSE)
            message(WARNING
                "RPI_IMAGER_COVERAGE is on but gcovr was not found, so no `coverage` target "
                "was created. Install it (apt install gcovr / pip install gcovr) and "
                "re-run CMake.")
        endif()
    endif()

    if(_coverage_tools_found)
        # No DEPENDS on the test binaries: RunCoverage.cmake builds them
        # itself, best-effort, so one unbuildable target (rpiboot_integration_test
        # does not currently link) degrades the report rather than blocking it.
        # Expressing it as DEPENDS made that single failure fatal to the run.
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND}
                    -DCOVERAGE_BINARY_DIR=${CMAKE_BINARY_DIR}
                    -DCOVERAGE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
                    -DCOVERAGE_OUTPUT_DIR=${CMAKE_BINARY_DIR}/coverage
                    -DCOVERAGE_GENERATOR=${CMAKE_GENERATOR}
                    -DCOVERAGE_FLAVOUR=${RPI_COVERAGE_FLAVOUR}
                    -DGCOVR_EXECUTABLE=${GCOVR_EXECUTABLE}
                    -DLLVM_PROFDATA_EXECUTABLE=${LLVM_PROFDATA_EXECUTABLE}
                    -DLLVM_COV_EXECUTABLE=${LLVM_COV_EXECUTABLE}
                    -DCOVERAGE_APP_NAME=${PROJECT_NAME}
                    -DCTEST_EXECUTABLE=${CMAKE_CTEST_COMMAND}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCoverage.cmake
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running the suite under coverage and rendering a report"
            USES_TERMINAL
            VERBATIM
        )
    endif()
endif()

# ============================================================================
# PerformanceStats
# ============================================================================
# The telemetry recorder: session lifecycle, event timing, per-phase
# throughput sampling and JSON export. It touches neither a device nor the
# network, so all of it is reachable from a plain unit test -- it had simply
# never had one, and its 735 branches were the largest block of genuinely
# testable uncovered code in the tree.

add_executable(performance_stats_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../performancestats.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../performancestats.cpp
    performance_stats_test.cpp
)

set_target_properties(performance_stats_test PROPERTIES AUTOMOC ON)

target_include_directories(performance_stats_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(performance_stats_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(performance_stats_test PRIVATE cxx_std_20)
target_compile_options(performance_stats_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

rpi_discover_tests(performance_stats_test)

add_custom_target(test_performance_stats
    COMMAND performance_stats_test
    DEPENDS performance_stats_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running PerformanceStats tests"
)

# ============================================================================
# FAT driver, against a filesystem in a scratch file
# ============================================================================
# DeviceWrapperFatPartition is the imager's own FAT implementation -- what
# writes config.txt, cmdline.txt and firstrun.sh onto the boot partition after
# imaging. fat_partition_test above can only reach it through a real mounted
# partition named by FAT_TEST_MOUNT_PATH, so on an ordinary run it exercises
# almost none of it.
#
# This target builds a filesystem with mkfs.vfat in a per-process scratch file
# and drives the same code against that: no device, no mount, no privileges,
# and both FAT16 and FAT32. Cases skip themselves where mkfs.vfat is absent.

if(WIN32)
    # The filter parsing behind the native open dialog. Only the Windows
    # backend has it; the dialog itself cannot be opened from a test, so the
    # parsing is reached through a seam instead.
    add_executable(native_file_dialog_test
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/nativefiledialog_windows.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../nativefiledialog.h
        native_file_dialog_test.cpp
    )

    target_compile_definitions(native_file_dialog_test PRIVATE
        NATIVEFILEDIALOG_ENABLE_TEST_API)

    target_include_directories(native_file_dialog_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(native_file_dialog_test PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        Qt6::Gui
        ole32
        shell32
    )

    target_compile_features(native_file_dialog_test PRIVATE cxx_std_20)

    rpi_discover_tests(native_file_dialog_test)
endif()

add_executable(proxy_url_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../proxy_url.h
    proxy_url_test.cpp
)

target_include_directories(proxy_url_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(proxy_url_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Network
)

target_compile_features(proxy_url_test PRIVATE cxx_std_20)

rpi_discover_tests(proxy_url_test)

add_executable(image_options_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../imageadvancedoptions.h
    image_options_test.cpp
)

# Q_NAMESPACE, so moc has to see the header.
set_target_properties(image_options_test PROPERTIES AUTOMOC ON)

target_include_directories(image_options_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(image_options_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(image_options_test PRIVATE cxx_std_20)

rpi_discover_tests(image_options_test)

add_executable(secure_bytes_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../secure_bytes.h
    secure_bytes_test.cpp
)

target_include_directories(secure_bytes_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(secure_bytes_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(secure_bytes_test PRIVATE cxx_std_20)

rpi_discover_tests(secure_bytes_test)

add_executable(revision_code_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../embedded/revision_code.h
    revision_code_test.cpp
)

target_include_directories(revision_code_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(revision_code_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(revision_code_test PRIVATE cxx_std_20)

rpi_discover_tests(revision_code_test)

add_executable(settings_permissions_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../settings_permissions.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../settings_permissions.cpp
    settings_permissions_test.cpp
)

target_include_directories(settings_permissions_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(settings_permissions_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(settings_permissions_test PRIVATE cxx_std_20)

rpi_discover_tests(settings_permissions_test)

# Writing into a subdirectory, measured against mtools rather than against
# this driver's own reading -- which has agreed with it while it put files in
# the wrong place.
add_executable(fat_subdirectory_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../disk_formatter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    fat_subdirectory_test.cpp
)

set_target_properties(fat_subdirectory_test PROPERTIES AUTOMOC ON)

target_include_directories(fat_subdirectory_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(fat_subdirectory_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

rpi_link_platform_file_ops(fat_subdirectory_test)
# macfile.cpp calls the Authorization API, which is in Security.
rpi_link_platform_backends(fat_subdirectory_test)
target_compile_features(fat_subdirectory_test PRIVATE cxx_std_20)
rpi_discover_tests(fat_subdirectory_test)

add_executable(fat_partition_image_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    fat_partition_image_test.cpp
)

set_target_properties(fat_partition_image_test PROPERTIES AUTOMOC ON)

target_include_directories(fat_partition_image_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(fat_partition_image_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

if(APPLE)
    target_link_libraries(fat_partition_image_test PRIVATE
        "-framework Security"
        "-framework DiskArbitration"
        "-framework CoreFoundation"
    )
endif()

rpi_link_platform_file_ops(fat_partition_image_test)

target_compile_features(fat_partition_image_test PRIVATE cxx_std_20)

rpi_discover_tests(fat_partition_image_test)

add_custom_target(test_fat_partition_image
    COMMAND fat_partition_image_test
    DEPENDS fat_partition_image_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running FAT driver tests against a scratch filesystem"
)

# ============================================================================
# SystemMemoryManager
# ============================================================================
# Sizes every buffer in the write path. The memory-tier branches read the
# host's real RAM and so are not selectable from a test, but the
# parameterised sizing -- verify buffer by file size, ring buffer slots by
# slot size, async queue depth by block size -- is fully drivable.

add_executable(system_memory_manager_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../systemmemorymanager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../systemmemorymanager.cpp
    system_memory_manager_test.cpp
)

target_include_directories(system_memory_manager_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(system_memory_manager_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(system_memory_manager_test PRIVATE cxx_std_20)

rpi_discover_tests(system_memory_manager_test)

# ============================================================================
# One row of the drive picker
# ============================================================================
# sizeInGb() is the figure printed beside each device, and the one the user
# checks a card against before agreeing to erase it.

add_executable(drive_list_item_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivelistitem.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivelistitem.cpp
    drive_list_item_test.cpp
)

set_target_properties(drive_list_item_test PROPERTIES AUTOMOC ON)

target_include_directories(drive_list_item_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(drive_list_item_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(drive_list_item_test PRIVATE cxx_std_20)

rpi_discover_tests(drive_list_item_test)

# ============================================================================
# The accelerated SHA-256
# ============================================================================
# The hash both sides of the write comparison are computed with. It had no
# direct test on any platform: it was only ever exercised through the writer,
# where a wrong digest on both sides still compares equal.

add_executable(accelerated_hash_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../acceleratedcryptographichash.h
    ${PLATFORM_ACCEL_HASH_SRC}
    accelerated_hash_test.cpp
)

# The CNG backend releases its resources on every error path and again in the
# destructor. The failures cannot be provoked -- they are the provider
# refusing -- but the sequence can.
target_compile_definitions(accelerated_hash_test PRIVATE ACCELERATED_HASH_ENABLE_TEST_API)

target_include_directories(accelerated_hash_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS}
)

target_link_libraries(accelerated_hash_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${GNUTLS_LIBRARIES}
)

rpi_link_platform_backends(accelerated_hash_test)

target_compile_features(accelerated_hash_test PRIVATE cxx_std_20)

rpi_discover_tests(accelerated_hash_test)

# ============================================================================
# DeviceInfo on a desktop
# ============================================================================
# The stubs the non-embedded build links in place of the /proc/cpuinfo
# reader. A separate target from device_info_test, which links the embedded
# implementation of the same class.

add_executable(device_info_desktop_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../device_info.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../device_info.cpp
    device_info_desktop_test.cpp
)

target_include_directories(device_info_desktop_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(device_info_desktop_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(device_info_desktop_test PRIVATE cxx_std_20)

rpi_discover_tests(device_info_desktop_test)

# ============================================================================
# SecureBoot
# ============================================================================
# Assembles and signs the boot image a locked-down Pi will accept. Testable
# without hardware because the Linux signing path shells out to openssl, so a
# throwaway key suffices, and extractFatPartitionFiles() takes a
# DeviceWrapperFatPartition that the FAT scratch-image fixture can build.
# Cases needing openssl or mkfs.vfat skip themselves.

add_executable(secureboot_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.cpp
    ${PLATFORM_SECUREBOOT_CRYPTO_SRC}
    ${PLATFORM_BOOTIMGCREATOR_SRC}
    ${PLATFORM_ACCEL_HASH_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    secureboot_test.cpp
)

set_target_properties(secureboot_test PROPERTIES AUTOMOC ON)

target_include_directories(secureboot_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(secureboot_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${GNUTLS_LIBRARIES}
)
rpi_link_platform_backends(secureboot_test)

target_include_directories(secureboot_test PRIVATE ${GNUTLS_INCLUDE_DIRS})

rpi_link_platform_file_ops(secureboot_test)

target_compile_features(secureboot_test PRIVATE cxx_std_20)

rpi_discover_tests(secureboot_test)

# ============================================================================
# DownloadThread
# ============================================================================
# The write path: curl in, hashed, onto the target. 1,958 branches, the
# largest uncovered block in the tree and the most safety-critical.
#
# Drivable without a device or a network because curl in this build carries
# the file:// protocol -- so a scratch file serves the image -- and
# _openAndPrepareDevice() only unmounts when the destination starts with
# "/dev/", so a scratch image takes the same open-and-write path a card
# would. Nothing is mocked; the real transfer loop runs end to end.

# QtConcurrent and QtTest are not among the components the app itself asks
# for, so this target finds them on its own.
find_package(Qt6 REQUIRED COMPONENTS Concurrent)

# QuickTest drives the QML controls. The Qt we ship is built without testlib
# (qt/features_exclude.list), so this is optional: found, the driven UI tests
# are built; absent, they are skipped and the rest of the suite is unaffected.
find_package(Qt6 QUIET COMPONENTS QuickTest)

# The write pipeline links as one unit -- DownloadThread reaches the device
# wrapper, the memory manager, platform quirks, the cache writer and the
# secure-boot helpers -- so both targets below share the list.
set(RPI_DOWNLOAD_PIPELINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../downloadthread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../systemmemorymanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../imageadvancedoptions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/sparse_encoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../asynccachewriter.cpp
    ${PLATFORM_QUIRKS_SRC}
    ${PLATFORM_SECUREBOOT_CRYPTO_SRC}
    ${PLATFORM_BOOTIMGCREATOR_SRC}
    ${PLATFORM_ACCEL_HASH_SRC}
    ${PLATFORM_DRIVELIST_SRC}
    ${PLATFORM_FILE_OPS}
)

# ── bmap ────────────────────────────────────────────────────────────────────
# Decides which blocks get written. A block wrongly reported unmapped is
# never written and leaves a hole in the filesystem on the card.
add_executable(bmap_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/bmap.cpp
    bmap_test.cpp
)

target_include_directories(bmap_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(bmap_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(bmap_test PRIVATE cxx_std_20)

rpi_discover_tests(bmap_test)

# ── image size estimation ───────────────────────────────────────────────────
# Decides whether an image is accepted for a given card, so a wrong answer
# either rejects a good image or lets a doomed write start.
add_executable(image_size_parser_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../imagesizeparser.cpp
    image_size_parser_test.cpp
)

target_include_directories(image_size_parser_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LibArchive_INCLUDE_DIR}
    ${LIBLZMA_INCLUDE_DIRS}
)

target_link_libraries(image_size_parser_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${LibArchive_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

target_compile_features(image_size_parser_test PRIVATE cxx_std_20)

rpi_discover_tests(image_size_parser_test)

# ── drive format thread ─────────────────────────────────────────────────────
# The "Erase" entry: refusing safely, and the message shown when it fails.
add_executable(drive_format_thread_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../driveformatthread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../disk_formatter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    ${DRIVELIST_PLATFORM_SOURCES}
    ${PLATFORMQUIRKS_SOURCES}
    drive_format_thread_test.cpp
)

set_target_properties(drive_format_thread_test PROPERTIES AUTOMOC ON)

target_include_directories(drive_format_thread_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(drive_format_thread_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    ${PLATFORMQUIRKS_LIBS}
)

rpi_link_platform_file_ops(drive_format_thread_test)

target_compile_features(drive_format_thread_test PRIVATE cxx_std_20)

if(WIN32)
    # virtdisk for vhd_device.h, reached through TestBlockDevice: a
    # virtual disk is what stands in for a loop device here.
    target_link_libraries(drive_format_thread_test PRIVATE virtdisk)
endif()

rpi_discover_tests(drive_format_thread_test)

# ── ImageWriter and the models behind the pickers ───────────────────────────
#
# The QML-facing backend. It was excluded from coverage as unreachable, which
# was only true of the Qt that happened to be installed: it needs no QML
# engine -- it builds its models as members and leaves the engine null -- so a
# QGuiApplication on the offscreen platform reaches all of it.
#
# imagewriter.cpp, hwlistmodel.cpp and oslistmodel.cpp are compiled into the
# QML module rather than into SOURCES for a GUI build, so they are named here
# explicitly alongside the rest of the application's sources. main.cpp is the
# only one left out; this target supplies its own.
if(NOT BUILD_CLI_ONLY)
    # SOURCES is the application's own final list, so this target cannot
    # drift from what actually ships. main.cpp is dropped because this binary
    # supplies its own.
    set(IMAGE_WRITER_TEST_SOURCES)
    foreach(_src IN LISTS SOURCES)
        # .qrc is deliberately included: the timezone, country and keymap
        # lists the customisation dialog is built from are Qt resources, and
        # a library without them hands back empty lists that look like a
        # product bug rather than a missing resource.
        if(_src MATCHES "\\.(cpp|cc|c|mm|qrc)$" AND NOT _src MATCHES "(^|/)main\\.cpp$")
            if(IS_ABSOLUTE "${_src}")
                list(APPEND IMAGE_WRITER_TEST_SOURCES "${_src}")
            else()
                list(APPEND IMAGE_WRITER_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../${_src}")
            endif()
        endif()
    endforeach()

    # ...and the QML-declared types, which a GUI build compiles into the QML
    # module rather than into SOURCES.
    set(IMAGER_QML_CPP_TYPES_ABS)
    foreach(_src IN LISTS IMAGER_QML_CPP_TYPES)
        list(APPEND IMAGER_QML_CPP_TYPES_ABS "${CMAKE_CURRENT_SOURCE_DIR}/../${_src}")
    endforeach()

    # One static library for the whole application, shared by every test
    # target that needs it.
    #
    # Listing the sources per target meant compiling the same hundred files
    # once for each -- and, under coverage instrumentation, meant several
    # objects for the same source each writing its own .gcda. That is the
    # "overwriting an existing profile data with a different checksum"
    # warning: the counts were being split across duplicate objects and then
    # merged back by gcovr. One object per source removes both problems.
    add_library(rpi_imager_testable STATIC
        ${IMAGE_WRITER_TEST_SOURCES}
        ${IMAGER_QML_CPP_TYPES_ABS}
        ${DEPENDENCIES}
    )

    set_target_properties(rpi_imager_testable PROPERTIES AUTOMOC ON AUTORCC ON)

    # PUBLIC throughout: consumers are test binaries that need the same
    # headers and the same libraries, and should not have to restate them.
    target_include_directories(rpi_imager_testable PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${GNUTLS_INCLUDE_DIRS}
        ${LIBLZMA_INCLUDE_DIRS}
        ${LibArchive_INCLUDE_DIR}
        ${LIBUSB_INCLUDE_DIR}
    )

    target_link_libraries(rpi_imager_testable PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Svg
        Qt6::Network
        Qt6::Concurrent
        CURL::libcurl
        ${LibArchive_LIBRARIES}
        ${LIBLZMA_LIBRARIES}
        ${ZSTD_LIBRARIES}
        ${GNUTLS_LIBRARIES}
        ${LIBUSB_LIBRARIES}
        ${CMAKE_DL_LIBS}
    )

    # The platform libraries the application itself links. This target
    # compiles the application's own source list, which on macOS means
    # Objective-C++ against CoreWLAN, CoreLocation, SystemConfiguration and
    # Security -- and EXTRALIBS reaches the app target alone, so every binary
    # built on this library failed to link with a page of undefined
    # Objective-C classes. Taking the application's list rather than restating
    # it means a framework added there cannot be forgotten here.
    target_link_libraries(rpi_imager_testable PUBLIC ${EXTRALIBS})

    # customization_generator hashes passwords through the bundled yescrypt.
    if(TARGET yescrypt)
        target_link_libraries(rpi_imager_testable PUBLIC yescrypt)
    endif()

    # The Linux URI handler is a DBus adaptor, so the same guard the
    # application uses applies here.
    if(TARGET Qt6::DBus)
        target_link_libraries(rpi_imager_testable PUBLIC Qt6::DBus)
    endif()

    rpi_link_platform_file_ops(rpi_imager_testable)

    # The .qrc files taken from SOURCES name files the application target
    # generates -- the compiled translations, and the country and timezone
    # lists. The custom commands that produce them belong to that target, in
    # a different directory, so AUTORCC here can reach them only once it has
    # run. Without this a build tree that has never built the application
    # stops with "no rule to make target rpi-imager_ko.qm".
    add_dependencies(rpi_imager_testable ${PROJECT_NAME})

    target_compile_features(rpi_imager_testable PUBLIC cxx_std_20)

    add_executable(image_writer_test image_writer_test.cpp)
    set_target_properties(image_writer_test PROPERTIES AUTOMOC ON)

    # getPSK() walks NetworkManager's active connections on the system bus.
    # The real one answers on a developer machine and then refuses
    # GetSecrets for want of privilege, so the walk stops at the first guard
    # and the arms that matter -- including the one that hands over the
    # passphrase -- had never run. The probe registers a NetworkManager of
    # its own on a bus of the test's making (DBUS_SYSTEM_BUS_ADDRESS), one
    # shape per run.
    if(TARGET Qt6::DBus)
        add_executable(network_manager_psk_probe
            ${CMAKE_CURRENT_SOURCE_DIR}/../linux/networkmanagerapi.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/network_manager_psk_probe.cpp
        )
        set_target_properties(network_manager_psk_probe PROPERTIES AUTOMOC ON)
        target_link_libraries(network_manager_psk_probe PRIVATE Qt6::Core Qt6::DBus Qt6::Network)
        target_include_directories(network_manager_psk_probe PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/..)
        target_compile_features(network_manager_psk_probe PRIVATE cxx_std_20)
        target_compile_options(network_manager_psk_probe PRIVATE
            -Wall -Wextra -Wpedantic
            $<$<CONFIG:Debug>:-g -O0>
        )
        target_compile_definitions(image_writer_test PRIVATE
            NM_PSK_PROBE_BINARY="$<TARGET_FILE:network_manager_psk_probe>")
        add_dependencies(image_writer_test network_manager_psk_probe)
    endif()
    target_link_libraries(image_writer_test PRIVATE Catch2::Catch2 rpi_imager_testable)
if(WIN32)
        # virtdisk for vhd_device.h, reached through TestBlockDevice: a virtual
        # disk is what stands in for a loop device here.
        target_link_libraries(image_writer_test PRIVATE virtdisk)
    endif()

    target_compile_definitions(image_writer_test PRIVATE
        IMAGER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    rpi_discover_tests(image_writer_test FAULT_INJECTION)

# The keyboard probe is declared with the other Linux-only probes above; the
# definition has to come after this target exists.
if(TARGET keyboard_detect_probe)
    target_compile_definitions(image_writer_test PRIVATE
        KEYBOARD_PROBE_BINARY="$<TARGET_FILE:keyboard_detect_probe>")
    add_dependencies(image_writer_test keyboard_detect_probe)
endif()

# Likewise the settings-permissions probe, which runs the fix as a real root
# inside a user namespace.
if(TARGET settings_permissions_probe)
    target_compile_definitions(image_writer_test PRIVATE
        SETTINGS_PERMISSIONS_PROBE_BINARY="$<TARGET_FILE:settings_permissions_probe>")
    add_dependencies(image_writer_test settings_permissions_probe)
endif()

    # The command line, run as a subprocess.
    #
    # Cli::run() builds its own QCoreApplication, so it cannot be called from
    # a test process that already has one -- which is why the decisions inside
    # it were split into statics. Reaching run() itself means running the real
    # binary, so this target needs it built and needs to know where it is.
    # Every case ends in a refusal that returns before a device is opened.
    add_executable(cli_process_test cli_process_test.cpp)
    set_target_properties(cli_process_test PROPERTIES AUTOMOC ON)
    target_link_libraries(cli_process_test PRIVATE Catch2::Catch2WithMain Qt6::Core)
    target_compile_definitions(cli_process_test PRIVATE
        IMAGER_BINARY="$<TARGET_FILE:${PROJECT_NAME}>")
    add_dependencies(cli_process_test ${PROJECT_NAME})
    rpi_discover_tests(cli_process_test)

    # The picker models: same library, its own cases.
    add_executable(picker_models_test picker_models_test.cpp)
    set_target_properties(picker_models_test PROPERTIES AUTOMOC ON)
    target_link_libraries(picker_models_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(picker_models_test)

    # Does the user interface load at all?
    add_executable(qml_load_test qml_load_test.cpp)
    set_target_properties(qml_load_test PROPERTIES AUTOMOC ON)
    target_link_libraries(qml_load_test PRIVATE Catch2::Catch2 rpi_imager_testable Qt6::Quick)
    target_compile_definitions(qml_load_test PRIVATE
        IMAGER_QML_MODULE_PARENT="${CMAKE_BINARY_DIR}"
        IMAGER_QML_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
    rpi_discover_tests(qml_load_test)

    # Driven tests for the controls themselves: real clicks and keystrokes
    # against real instances. Needs a Qt built with testlib -- the Qt we ship
    # excludes it, so this target simply does not exist against that build.
    if(TARGET Qt6::QuickTest)
        add_executable(qml_ui_test qml_ui_test.cpp)
        set_target_properties(qml_ui_test PROPERTIES AUTOMOC ON)
        target_link_libraries(qml_ui_test PRIVATE
            rpi_imager_testable Qt6::Quick Qt6::QuickTest)
        target_compile_definitions(qml_ui_test PRIVATE
            IMAGER_QML_MODULE_PARENT="${CMAKE_BINARY_DIR}"
            IMAGER_QML_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
            QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
        add_test(NAME qml_ui_test
                 COMMAND qml_ui_test -platform offscreen)
        # One CTest entry, but a whole QtQuickTest suite behind it: over a
        # thousand cases, seven minutes on a laptop. The suite-wide --timeout
        # that stops a deadlocked case from costing the whole run is far below
        # that, so this one says how long it honestly needs.
        #
        # The two URLs are the same pair rpi_discover_tests() sets for every
        # Catch2 binary, and for the same reason: this suite drives the real
        # ImageWriter singleton, so anything it reaches that reports a write
        # or enrols a device would do so against production. Registered with
        # add_test() rather than through that function, it was the one binary
        # without them -- and it is the one that drives the wizard hardest.
        # One ENVIRONMENT list, not three: CMake keeps only the last of a
        # repeated property key.
        # Nearly all the suite's wall time, ctest running the rest behind it.
        # It timed out here at 1800.08s on 14 September 2026, and the cause
        # was not the storms: nested OS icons were reaching Image.source
        # unrouted, so Qt Quick fetched hundreds of them over the network.
        # Routed, the same suite passes in 692.61s under UBSan on a machine
        # also running three fuzzers. Left at 1800, which is now well over
        # twice what it needs -- doubling it would have hidden the fetches
        # rather than found them.
        set_tests_properties(qml_ui_test PROPERTIES
            ENVIRONMENT "QT_QUICK_BACKEND=software;RPI_IMAGER_TELEMETRY_URL=;RPI_IMAGER_CONNECT_URL=http://127.0.0.1:1;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/scratch-config;BROWSER=true"
            TIMEOUT 1800)

        # The same binary again, on one file, on its own.
        #
        # Every QtQuickTest file in a run shares one ImageWriter singleton, so
        # the device list holds whatever the files before it left behind. By
        # the time tst_device_selection_step runs in the suite above, another
        # file has fetched a list -- which is a state worth testing, but it
        # means the screen a user meets with no network is never reached, and
        # those cases skip. Running the file by itself reaches it.
        #
        # The file covers both states and says which each case needs, so
        # between the two entries neither is left out.
        add_test(NAME qml_ui_test_device_selection_offline
                 COMMAND qml_ui_test -platform offscreen
                         -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_device_selection_step.qml")
        set_tests_properties(qml_ui_test_device_selection_offline PROPERTIES
            ENVIRONMENT "QT_QUICK_BACKEND=software;RPI_IMAGER_TELEMETRY_URL=;RPI_IMAGER_CONNECT_URL=http://127.0.0.1:1;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/scratch-config;BROWSER=true")

        # These two are registered by hand rather than through
        # rpi_discover_tests(), so nothing has given them a DLL search path. On
        # Windows both exited 0xc0000135 before main(), which took the whole
        # QtQuickTest suite -- 981 test functions behind two CTest entries --
        # out of the run while reporting only two failures.
        #
        # ENVIRONMENT_MODIFICATION rather than an addition to ENVIRONMENT above:
        # it prepends to the PATH the runner already has instead of replacing
        # it, and it is a separate property, so neither of the two settings
        # overwrites the other.
        rpi_set_windows_test_env(qml_ui_test qml_ui_test_device_selection_offline)

        # `qml_coverage`: which of the QML actually ran.
        #
        # gcov cannot see QML. qmlcachegen emits bytecode as a byte array with
        # no line mapping, so the .cpp it generates is instrumented and says
        # nothing about what executed, and qmlsc -- which would emit real C++ --
        # is not in the open-source Qt. The UI was therefore not merely
        # uncovered but unmeasured, which is worse: a gap you cannot see does
        # not appear on any list of gaps.
        #
        # So the sites are counted directly. tools/qml_coverage_instrument.py
        # injects a probe into every function body and block-bodied signal
        # handler of a *copy* of the generated module, the driven tests run
        # against that copy, and the report says which probes fired. The
        # shipping QML is never touched, and the injection is inline after the
        # opening brace so line numbers do not move.
        #
        # Function and handler level, not statement level. Statement coverage
        # would need much more of the grammar understood, and a coverage number
        # that is wrong is worse than not having one.
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_custom_target(qml_coverage
                # Last run's counts first, or a run that fails part-way is
                # reported on top of the one before it -- the same reason the
                # C++ coverage target clears its .gcda files.
                COMMAND "${CMAKE_COMMAND}" -E rm -f
                        "${CMAKE_BINARY_DIR}/qmlcov/hits.json"
                        "${CMAKE_BINARY_DIR}/qmlcov/hits-offline.json"
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/qml_coverage_instrument.py"
                        "${CMAKE_BINARY_DIR}/RpiImager"
                        "${CMAKE_BINARY_DIR}/qmlcov/RpiImager"
                        --inventory "${CMAKE_BINARY_DIR}/qmlcov/inventory.json"
                COMMAND "${CMAKE_COMMAND}" -E env
                        "RPI_QML_MODULE_OVERRIDE=${CMAKE_BINARY_DIR}/qmlcov/RpiImager"
                        "RPI_QML_COVERAGE_HITS=${CMAKE_BINARY_DIR}/qmlcov/hits.json"
                        "QT_QUICK_BACKEND=software"
                        $<TARGET_FILE:qml_ui_test> -platform offscreen
                # And again on the one file CTest also runs on its own. Every
                # file in a QtQuickTest run shares one ImageWriter, so the
                # cases about the screen a user meets with no OS list only run
                # when nothing has fetched one -- measuring the whole-suite run
                # alone reports the sites they cover as never reached, which
                # is how a tested Retry button reads as untested.
                COMMAND "${CMAKE_COMMAND}" -E env
                        "RPI_QML_MODULE_OVERRIDE=${CMAKE_BINARY_DIR}/qmlcov/RpiImager"
                        "RPI_QML_COVERAGE_HITS=${CMAKE_BINARY_DIR}/qmlcov/hits-offline.json"
                        "QT_QUICK_BACKEND=software"
                        $<TARGET_FILE:qml_ui_test> -platform offscreen
                        -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_device_selection_step.qml"
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/qml_coverage_report.py"
                        "${CMAKE_BINARY_DIR}/qmlcov/inventory.json"
                        "${CMAKE_BINARY_DIR}/qmlcov/hits.json"
                        "${CMAKE_BINARY_DIR}/qmlcov/hits-offline.json"
                        --out "${CMAKE_BINARY_DIR}/qmlcov/summary.txt"
                DEPENDS qml_ui_test ${PROJECT_NAME}
                WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                COMMENT "Running the driven QML tests against an instrumented copy of the module"
                USES_TERMINAL
                VERBATIM
            )
        else()
            message(STATUS "Python3 not found -- skipping the qml_coverage target")
        endif()
    else()
        message(STATUS "Qt6::QuickTest not found -- skipping driven QML tests")
    endif()

    # Saving a copy of the image while it downloads.
    add_executable(async_cache_writer_test async_cache_writer_test.cpp)
    set_target_properties(async_cache_writer_test PROPERTIES AUTOMOC ON)
    target_link_libraries(async_cache_writer_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(async_cache_writer_test)

    # The one place every libcurl handle is configured.
    add_executable(curl_network_config_test curl_network_config_test.cpp)
    set_target_properties(curl_network_config_test PROPERTIES AUTOMOC ON)
    target_link_libraries(curl_network_config_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(curl_network_config_test)

    # Keeping the drive chooser up to date.
    add_executable(drive_poll_thread_test drive_poll_thread_test.cpp)
    set_target_properties(drive_poll_thread_test PROPERTIES AUTOMOC ON)
    target_link_libraries(drive_poll_thread_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(drive_poll_thread_test)

    # Every icon in the OS chooser comes through this.
    add_executable(icon_fetcher_test icon_fetcher_test.cpp)
    set_target_properties(icon_fetcher_test PROPERTIES AUTOMOC ON)
    target_link_libraries(icon_fetcher_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(icon_fetcher_test)

    # The stall detector behind "it stopped at 47%".
    add_executable(write_progress_watchdog_test write_progress_watchdog_test.cpp)
    set_target_properties(write_progress_watchdog_test PROPERTIES AUTOMOC ON)
    target_link_libraries(write_progress_watchdog_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(write_progress_watchdog_test)
endif()

# ── OS list parsing ─────────────────────────────────────────────────────────
# Pure JSON transformations that decide what the user sees in the OS chooser.
add_executable(oslist_parser_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../oslistparser.cpp
    oslist_parser_test.cpp
)

target_include_directories(oslist_parser_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(oslist_parser_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(oslist_parser_test PRIVATE cxx_std_20)

rpi_discover_tests(oslist_parser_test)

# ── config.txt merging ──────────────────────────────────────────────────────
# Header-only, and shared by both write paths so they cannot drift apart
# again -- drifting apart is how the substring bug came to exist in two
# places at once.
# ── hardware detection ──────────────────────────────────────────────────────
# The embedded build reads the board's revision code to decide which images it
# is offered. The desktop build links a stub instead, so the real one is
# compiled only here; it reads /proc/cpuinfo from a hardcoded path, which the
# driver replaces inside a mount namespace.
add_executable(device_info_probe
    ${CMAKE_CURRENT_SOURCE_DIR}/../embedded/device_info.cpp
    device_info_probe.cpp
)

target_include_directories(device_info_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(device_info_probe PRIVATE Qt6::Core)
target_compile_features(device_info_probe PRIVATE cxx_std_20)

add_executable(device_info_test device_info_test.cpp)

target_include_directories(device_info_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(device_info_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_definitions(device_info_test PRIVATE
    DEVICE_INFO_PROBE_BINARY="$<TARGET_FILE:device_info_probe>")

target_compile_features(device_info_test PRIVATE cxx_std_20)

add_dependencies(device_info_test device_info_probe)

rpi_discover_tests(device_info_test)

# ── screenshot page list ────────────────────────────────────────────────────
# The runner itself is a custom target below, but the list it is driven from
# names steps in WizardContainer.qml and published names in the metainfo, and
# is silently wrong when either moves. Reading three files costs nothing.
add_executable(screenshot_pages_test screenshot_pages_test.cpp)

target_link_libraries(screenshot_pages_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_definitions(screenshot_pages_test PRIVATE
    SCREENSHOT_PAGES_FILE="${CMAKE_CURRENT_SOURCE_DIR}/screenshots/pages.txt"
    WIZARD_CONTAINER_QML="${CMAKE_CURRENT_SOURCE_DIR}/../wizard/WizardContainer.qml"
    METAINFO_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../../debian/com.raspberrypi.rpi-imager.metainfo.xml.in"
)

target_compile_features(screenshot_pages_test PRIVATE cxx_std_20)

rpi_discover_tests(screenshot_pages_test)

# ── suspend inhibition ──────────────────────────────────────────────────────
# The machine must not go to sleep while a card is being written, and letting
# go of the inhibit must not hold the application open afterwards. The FIFO it
# uses lives in /run, which an ordinary user cannot write to, so the probe is
# run under `unshare -rm` with a tmpfs over it.
if(TARGET Qt6::DBus)
    add_executable(suspend_inhibitor_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/linux_suspend_inhibitor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../suspend_inhibitor.cpp
        ${PLATFORM_QUIRKS_SRC}
        suspend_inhibitor_probe.cpp
    )

    # LinuxSuspendInhibitor derives from SuspendInhibitor, so the probe needs
    # the base's constructor and destructor, and AUTOMOC to emit the vtable
    # entries its Q_OBJECT declares.
    set_target_properties(suspend_inhibitor_probe PROPERTIES AUTOMOC ON)

    target_include_directories(suspend_inhibitor_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(suspend_inhibitor_probe PRIVATE
        Qt6::Core
        Qt6::DBus
    )

    target_compile_features(suspend_inhibitor_probe PRIVATE cxx_std_20)

    # And the GNOME half, which is a D-Bus call rather than a held FIFO. The
    # probe provides its own session manager to talk to; the test runs it
    # under dbus-run-session so the bus is private to the case.
    add_executable(gnome_inhibitor_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/linux_suspend_inhibitor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../suspend_inhibitor.cpp
        ${PLATFORM_QUIRKS_SRC}
        gnome_inhibitor_probe.cpp
    )

    set_target_properties(gnome_inhibitor_probe PROPERTIES AUTOMOC ON)

    target_include_directories(gnome_inhibitor_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(gnome_inhibitor_probe PRIVATE
        Qt6::Core
        Qt6::DBus
    )

    target_compile_features(gnome_inhibitor_probe PRIVATE cxx_std_20)

    add_executable(suspend_inhibitor_test suspend_inhibitor_test.cpp)

    target_include_directories(suspend_inhibitor_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(suspend_inhibitor_test PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
    )

    target_compile_definitions(suspend_inhibitor_test PRIVATE
        SUSPEND_INHIBITOR_PROBE_BINARY="$<TARGET_FILE:suspend_inhibitor_probe>"
        GNOME_INHIBITOR_PROBE_BINARY="$<TARGET_FILE:gnome_inhibitor_probe>")

    target_compile_features(suspend_inhibitor_test PRIVATE cxx_std_20)

    add_dependencies(suspend_inhibitor_test suspend_inhibitor_probe gnome_inhibitor_probe)

    rpi_discover_tests(suspend_inhibitor_test)
endif()

# ── spanning-tree detection ─────────────────────────────────────────────────
# The embedded build warns that a switch running STP will hold the port down
# for its forward delay before an address arrives. Getting the decision wrong
# either warns on every managed switch or leaves the user watching a board
# that looks like it has failed to reach the network.
if(UNIX AND NOT APPLE)
add_executable(stp_analyzer_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/stpanalyzer.cpp
    stp_analyzer_test.cpp
)

set_target_properties(stp_analyzer_test PROPERTIES AUTOMOC ON)

target_include_directories(stp_analyzer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Catch2 without its main(): the analyser owns a QSocketNotifier, which needs
# a QCoreApplication to exist before it is constructed.
target_link_libraries(stp_analyzer_test PRIVATE
    Catch2::Catch2
    Qt6::Core
)

target_compile_features(stp_analyzer_test PRIVATE cxx_std_20)

rpi_discover_tests(stp_analyzer_test)

# ============================================================================
# Arming the spanning-tree watch
# ============================================================================
# startListening() needs an AF_PACKET socket and so CAP_NET_RAW, which the
# suite does not have. The probe is run under `unshare -rn` by the test: a
# user namespace to be root, and a network namespace of its own so the only
# interface in sight is that namespace's loopback.

if(TARGET Qt6::Network)
    add_executable(stp_listener_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/stpanalyzer.cpp
        stp_listener_probe.cpp
    )

    set_target_properties(stp_listener_probe PROPERTIES AUTOMOC ON)

    target_include_directories(stp_listener_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(stp_listener_probe PRIVATE Qt6::Core Qt6::Network)
    target_compile_features(stp_listener_probe PRIVATE cxx_std_20)

    add_dependencies(stp_analyzer_test stp_listener_probe)
    target_compile_definitions(stp_analyzer_test PRIVATE
        STP_LISTENER_PROBE_BINARY="$<TARGET_FILE:stp_listener_probe>")
endif()
endif()


add_executable(config_txt_merge_test config_txt_merge_test.cpp)
target_link_libraries(config_txt_merge_test PRIVATE Catch2::Catch2WithMain Qt6::Core)
target_include_directories(config_txt_merge_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(config_txt_merge_test PRIVATE cxx_std_20)
rpi_discover_tests(config_txt_merge_test)

# ── rpiboot thread ──────────────────────────────────────────────────────────
# The CM4/CM5 sideload sequence. Reachable since the bus went behind
# IUsbContext; before that every line of it needed a compute module in boot
# mode plugged in.
# rpibootthread.cpp is already in rpi_imager_testable; listing it here too
# gives two copies of its moc output and a duplicate-symbol link failure.
add_executable(rpiboot_thread_test rpiboot_thread_test.cpp)
set_target_properties(rpiboot_thread_test PROPERTIES AUTOMOC ON)
target_link_libraries(rpiboot_thread_test PRIVATE Catch2::Catch2 rpi_imager_testable)
rpi_discover_tests(rpiboot_thread_test)

# ── fastboot flash thread ───────────────────────────────────────────────────
# Covers the erase path: the most destructive operation in the fastboot flow.
add_executable(fastboot_flash_thread_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastbootflashthread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/sparse_encoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/bmap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/pieeprom.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/eeprom_signer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/libusb_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../connect_device_registrar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../systemmemorymanager.cpp
    ${PLATFORM_ACCEL_HASH_SRC}
    ${PLATFORM_SECUREBOOT_CRYPTO_SRC}
    ${PLATFORM_QUIRKS_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/../ringbuffer.cpp
    fastboot_flash_thread_test.cpp
)

set_target_properties(fastboot_flash_thread_test PROPERTIES AUTOMOC ON)

target_include_directories(fastboot_flash_thread_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS}
    ${LIBUSB_INCLUDE_DIR}
    ${LibArchive_INCLUDE_DIR}
)

# Catch2 without its main(): this target provides its own so a
# QCoreApplication exists before any case runs (see the bottom of the test).
target_link_libraries(fastboot_flash_thread_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    CURL::libcurl
    ${LibArchive_LIBRARIES}
    ${GNUTLS_LIBRARIES}
    ${LIBUSB_LIBRARIES}
)
rpi_link_platform_backends(fastboot_flash_thread_test)

target_compile_features(fastboot_flash_thread_test PRIVATE cxx_std_20)

rpi_discover_tests(fastboot_flash_thread_test)

add_executable(download_thread_test
    ${RPI_DOWNLOAD_PIPELINE_SOURCES}
    download_thread_test.cpp
)

set_target_properties(download_thread_test PROPERTIES AUTOMOC ON)

target_include_directories(download_thread_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS}
)

# Catch2 without its main(): this target provides its own so a
# QCoreApplication exists before any case runs (see the bottom of the test).
target_link_libraries(download_thread_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    CURL::libcurl
    ${GNUTLS_LIBRARIES}
)
rpi_link_platform_backends(download_thread_test)

rpi_link_platform_file_ops(download_thread_test)

if(WIN32)
    # virtdisk for vhd_device.h, reached through TestBlockDevice: a virtual
    # disk is what stands in for a loop device here.
    target_link_libraries(download_thread_test PRIVATE virtdisk)
endif()


target_compile_features(download_thread_test PRIVATE cxx_std_20)

rpi_discover_tests(download_thread_test FAULT_INJECTION)

# ============================================================================
# DownloadExtractThread
# ============================================================================
# The compressed-image path: the same write pipeline with libarchive in front
# of it, so the bytes going to the card are the decompressed ones. 759
# branches, none of them covered.
#
# Driven the same way as DownloadThread -- a file:// URL and a scratch image
# for a destination -- with the archives built at test time by xz, gzip and
# zip, so the decompressors are fed real containers rather than fixtures
# checked into the tree.

add_executable(download_extract_thread_test
    ${RPI_DOWNLOAD_PIPELINE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../downloadextractthread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../localfileextractthread.cpp
    # Sizing asks libarchive the same question the write does, and one case
    # here sets the two verdicts side by side.
    ${CMAKE_CURRENT_SOURCE_DIR}/../imagesizeparser.cpp
    # The stub inhibitor rather than the DBus one: nothing here needs to stop
    # the machine suspending, and the real implementation wants a session bus.
    ${CMAKE_CURRENT_SOURCE_DIR}/../suspend_inhibitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../suspend_inhibitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/suspend_inhibitor_stub.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ringbuffer.cpp
    download_extract_thread_test.cpp
)

set_target_properties(download_extract_thread_test PROPERTIES AUTOMOC ON)

target_include_directories(download_extract_thread_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS}
    ${LibArchive_INCLUDE_DIR}
)

# Catch2 without its main(): this target provides its own so a
# QCoreApplication exists before any case runs.
target_link_libraries(download_extract_thread_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    CURL::libcurl
    ${LibArchive_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${GNUTLS_LIBRARIES}
)
rpi_link_platform_backends(download_extract_thread_test)

rpi_link_platform_file_ops(download_extract_thread_test)

if(WIN32)
    # virtdisk for vhd_device.h: the mounted-FAT fixture attaches a virtual
    # disk where the POSIX hosts take a loop device.
    target_link_libraries(download_extract_thread_test PRIVATE virtdisk)
endif()

target_compile_features(download_extract_thread_test PRIVATE cxx_std_20)

rpi_discover_tests(download_extract_thread_test)

# The manual-mount arm of extractMultiFileRun(): what runs when the OS has
# not auto-mounted the card Imager just formatted. Mounting needs
# CAP_SYS_ADMIN, and any device the suite mounts beforehand is found by the
# auto-mount scan instead, so neither the mount nor the unmount-and-rmdir
# that closes the arm could be reached in-process. The test runs this probe
# inside an unprivileged namespace with a "mount" of its own on PATH.
if(UNIX AND NOT APPLE)
    add_executable(download_extract_multifile_probe
        ${RPI_DOWNLOAD_PIPELINE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/../downloadextractthread.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../suspend_inhibitor.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../suspend_inhibitor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/suspend_inhibitor_stub.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ringbuffer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/download_extract_multifile_probe.cpp
    )

    set_target_properties(download_extract_multifile_probe PROPERTIES AUTOMOC ON)

    target_include_directories(download_extract_multifile_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${GNUTLS_INCLUDE_DIRS}
        ${LibArchive_INCLUDE_DIR}
    )

    target_link_libraries(download_extract_multifile_probe PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Concurrent
        CURL::libcurl
        ${LibArchive_LIBRARIES}
        ${ZSTD_LIBRARIES}
        ${LIBLZMA_LIBRARIES}
        ${GNUTLS_LIBRARIES}
    )
    rpi_link_platform_backends(download_extract_multifile_probe)
    rpi_link_platform_file_ops(download_extract_multifile_probe)

    target_compile_features(download_extract_multifile_probe PRIVATE cxx_std_20)

    target_compile_definitions(download_extract_thread_test PRIVATE
        MULTIFILE_PROBE_BINARY="$<TARGET_FILE:download_extract_multifile_probe>")
    add_dependencies(download_extract_thread_test download_extract_multifile_probe)
endif()

# ============================================================================
# CacheManager
# ============================================================================
# Decides whether a previously downloaded image can be reused. A false
# positive here writes the wrong image to a card, so the negative cases carry
# most of the weight.
#
# Runs under QStandardPaths test mode so the cache directory is a throwaway
# under the test tree: no case can find, trust or delete a real cached image.
# Verification happens on a worker thread, so this target supplies its own
# main() with a QCoreApplication.

add_executable(cache_manager_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../cachemanager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../cachemanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../systemmemorymanager.cpp
    cache_manager_test.cpp
)

set_target_properties(cache_manager_test PROPERTIES AUTOMOC ON)

target_include_directories(cache_manager_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(cache_manager_test PRIVATE
    Catch2::Catch2
    Qt6::Core
)

target_compile_features(cache_manager_test PRIVATE cxx_std_20)

rpi_discover_tests(cache_manager_test)

# ============================================================================
# CurlFetcher
# ============================================================================
# How the OS list and its sublists are retrieved. Reporting success on a 404
# hands the caller an HTML error page to parse as JSON, and never reporting at
# all leaves the UI waiting, so the failure cases carry the weight here.
#
# Driven against a throwaway HTTP server on 127.0.0.1 bound to port 0: real
# requests and real status handling, no network, no fixed port.

add_executable(curl_fetcher_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlfetcher.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlfetcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${PLATFORM_QUIRKS_SRC}
    curl_fetcher_test.cpp
)

set_target_properties(curl_fetcher_test PROPERTIES AUTOMOC ON)

target_include_directories(curl_fetcher_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(curl_fetcher_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    CURL::libcurl
)
rpi_link_platform_backends(curl_fetcher_test)

target_compile_features(curl_fetcher_test PRIVATE cxx_std_20)

rpi_discover_tests(curl_fetcher_test)

# ============================================================================
# SecureBootProvisioner
# ============================================================================
# Burns a customer key hash into a Compute Module's OTP and signs the firmware
# it will accept afterwards. OTP is write-once, so a wrong key hash bricks the
# module permanently -- which makes these the highest-consequence functions in
# the tree and they had no tests.
#
# provision() needs a device on the USB bus, but key generation, the OTP hash,
# boot-image signing and signed-recovery preparation all work on files and are
# covered here. The OTP hash is cross-checked against openssl rather than
# against a value this code produced.

add_executable(secure_boot_provisioner_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/secure_boot_provisioner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_protocol.cpp  # the provisioner runs the boot protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/file_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootcode_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootloader_image.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.cpp
    ${PLATFORM_SECUREBOOT_CRYPTO_SRC}
    ${PLATFORM_BOOTIMGCREATOR_SRC}
    ${PLATFORM_ACCEL_HASH_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    secure_boot_provisioner_test.cpp
)

set_target_properties(secure_boot_provisioner_test PROPERTIES AUTOMOC ON)
target_compile_definitions(secure_boot_provisioner_test PRIVATE SECUREBOOT_ENABLE_TEST_API)

target_include_directories(secure_boot_provisioner_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS}
)

target_link_libraries(secure_boot_provisioner_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${GNUTLS_LIBRARIES}
    # Bootfiles reads the boot image out of an archive.
    ${LibArchive_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${ZSTD_LIBRARIES}
)
rpi_link_platform_backends(secure_boot_provisioner_test)

rpi_link_platform_file_ops(secure_boot_provisioner_test)

target_compile_features(secure_boot_provisioner_test PRIVATE cxx_std_20)

# A real executable rather than a shell script: Windows appends ".exe" to a
# bare program name and nothing else, so a shebang script named "openssl" was
# never found and the cases that plant one passed by taking the "not
# installed" path instead.
add_executable(fake_openssl_probe
    fake_openssl_probe.cpp
)
target_compile_features(fake_openssl_probe PRIVATE cxx_std_20)

target_compile_definitions(secure_boot_provisioner_test PRIVATE
    FAKE_OPENSSL_BINARY="$<TARGET_FILE:fake_openssl_probe>")
add_dependencies(secure_boot_provisioner_test fake_openssl_probe)

rpi_discover_tests(secure_boot_provisioner_test)

# ============================================================================
# DownloadStatsTelemetry
# ============================================================================
# Reports which image was written. Two properties matter more than the
# reporting: it must send nothing when the user has opted out, and it must
# never be able to fail a write -- it runs fire-and-forget after the card is
# already done.
#
# Both are testable without the real endpoint: the opt-out is a QSettings
# value and the POST goes wherever it is pointed, so a throwaway server on
# 127.0.0.1 stands in. Nothing here contacts Raspberry Pi, and the settings
# are scoped to a test-mode file so no case can switch on a developer's real
# telemetry preference.

add_executable(download_stats_telemetry_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../downloadstatstelemetry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../downloadstatstelemetry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${PLATFORM_QUIRKS_SRC}
    download_stats_telemetry_test.cpp
)

set_target_properties(download_stats_telemetry_test PROPERTIES AUTOMOC ON)

target_include_directories(download_stats_telemetry_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(download_stats_telemetry_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    CURL::libcurl
)
rpi_link_platform_backends(download_stats_telemetry_test)

target_compile_features(download_stats_telemetry_test PRIVATE cxx_std_20)

rpi_discover_tests(download_stats_telemetry_test)

# ============================================================================
# ConnectDeviceRegistrar
# ============================================================================
# Enrols a board into Raspberry Pi Connect. It looked like it needed hardware
# and the live API; it needs neither. The device side is an IUsbTransport, and
# rpiboot/test/mock_usb_transport.h already provides one with fault injection
# built in; the API side takes a baseUrl that can point at 127.0.0.1.
#
# Nothing here contacts Raspberry Pi Connect and no real credential is used.

add_executable(connect_device_registrar_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../connect_device_registrar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${PLATFORM_QUIRKS_SRC}
    connect_device_registrar_test.cpp
)

set_target_properties(connect_device_registrar_test PROPERTIES AUTOMOC ON)

target_include_directories(connect_device_registrar_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(connect_device_registrar_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    CURL::libcurl
)
rpi_link_platform_backends(connect_device_registrar_test)

target_compile_features(connect_device_registrar_test PRIVATE cxx_std_20)

rpi_discover_tests(connect_device_registrar_test)

# ============================================================================
# FirmwareManager
# ============================================================================
# Decides which firmware a board needs and whether what is already cached can
# be trusted for it. The second question is the dangerous one: accepting a
# cache directory that is incomplete, zero-length, or populated for a
# different chip sideloads the wrong firmware to a Compute Module.
#
# The only public way in is ensureAvailable(), which downloads first, so the
# cache logic was unreachable. Those helpers are protected rather than private
# now and the test subclasses to reach them -- all filesystem work against a
# scratch cache, no network.

add_executable(firmware_manager_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/firmware_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootcode_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/secure_boot_provisioner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootloader_image.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/file_server.cpp  # RpibootProtocol::execute serves files through it
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${PLATFORM_SECUREBOOT_CRYPTO_SRC}
    ${PLATFORM_BOOTIMGCREATOR_SRC}
    ${PLATFORM_ACCEL_HASH_SRC}
    ${PLATFORM_QUIRKS_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    firmware_manager_test.cpp
)

set_target_properties(firmware_manager_test PROPERTIES AUTOMOC ON)

target_include_directories(firmware_manager_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS}
    ${LibArchive_INCLUDE_DIR}
)

target_link_libraries(firmware_manager_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    CURL::libcurl
    ${LibArchive_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${GNUTLS_LIBRARIES}
)
rpi_link_platform_backends(firmware_manager_test)

rpi_link_platform_file_ops(firmware_manager_test)

target_compile_features(firmware_manager_test PRIVATE cxx_std_20)

rpi_discover_tests(firmware_manager_test)
