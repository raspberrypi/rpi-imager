# libFuzzer harnesses. Off unless asked for: the tree builds with GCC, which
# has no -fsanitize=fuzzer, so turning this on requires configuring with Clang.
#
#   cmake -B build-fuzz -DRPI_IMAGER_FUZZERS=ON \
#         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ...
#   cmake --build build-fuzz --target fuzzers
#
# Until this existed every harness was compiled by hand from the README, which
# is how a harness quietly stops building: nothing in the tree refers to it.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "RPI_IMAGER_FUZZERS needs Clang for -fsanitize=fuzzer; "
        "this build is using ${CMAKE_CXX_COMPILER_ID}.")
endif()

# Linux only, and deliberately so rather than by omission: the FAT and
# partition-table harnesses hold their image in a memfd, and they link the
# Linux file-operations backend directly. Porting them means giving them a
# scratch file through the platform layer instead.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "RPI_IMAGER_FUZZERS is Linux-only; this build targets "
        "${CMAKE_SYSTEM_NAME}.")
endif()

# unsigned-integer-overflow is not in the default UBSan set, because wrapping
# is defined behaviour rather than undefined -- but it is still a wrong answer
# where a length or a digit accumulation is meant to be bounded, and several
# defects here were exactly that. It found parseBootOrder() accepting a value
# too long for the field as a truncated one, in forty-five seconds.
#
# Cheap to carry: over forty-second runs of six other targets it reported
# nothing at all, and the one report from fuzz_imagesize is ZSTD's own
# sentinel, which is (0ULL - 2) by design.
#
# implicit-conversion was tried alongside it and dropped. Its only report was
# the char-to-quint8 cast every byte extraction in this tree uses on purpose.
#
# -fno-sanitize-recover=undefined makes a *genuine* finding abort, so libFuzzer
# writes the input that caused it into findings/ and it can be replayed. The
# runner sets halt_on_error=0 for the reason above, which was carrying real
# undefined behaviour out with the deliberate wraps: the diagnostic printed and
# the target exited 0, so nothing kept the input. This splits them, because
# unsigned-integer-overflow is not in the `undefined` group -- that is why it
# is named separately on the line below -- and so stays recoverable.
#
# _GLIBCXX_ASSERTIONS is the one detector the sanitisers do not provide. An
# index past size() but inside the allocation is a wrong answer that ASan
# cannot see, because nothing was read outside the block it manages -- fed a
# std::vector of 64 bytes and asked for element 100, an ASan build and an
# ASan+UBSan build both print rubbish and exit 0. The assertion turns that
# into an abort libFuzzer keeps. It is the assertions mode, not _GLIBCXX_DEBUG:
# container layout is unchanged, so it does not break the ABI against Qt.
set(RPI_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined,unsigned-integer-overflow
                   -fno-sanitize-recover=undefined
                   -fno-omit-frame-pointer
                   -D_GLIBCXX_ASSERTIONS)

# One harness. SOURCES are the production files it needs beyond its own; MOC
# turns on AUTOMOC for the ones whose classes carry signals.
function(rpi_add_fuzzer name)
    cmake_parse_arguments(ARG "MOC" "" "SOURCES;LIBS" ${ARGN})

    add_executable(${name} EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp
        ${ARG_SOURCES}
    )
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../linux
    )
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_compile_options(${name} PRIVATE ${RPI_FUZZ_FLAGS})
    target_link_options(${name} PRIVATE ${RPI_FUZZ_FLAGS})
    target_link_libraries(${name} PRIVATE Qt6::Core ${ARG_LIBS})
    # linux/Platform.cmake adds -DHAVE_LIBURING for every target here but puts
    # the library only in EXTRALIBS, which reaches the app alone. A harness
    # compiling file_operations_linux.cpp gets the io_uring paths with nothing
    # to resolve them against. Same reason test/CMakeLists.txt carries
    # rpi_link_platform_file_ops(); that helper is out of scope here because
    # the fuzzers are built without BUILD_TESTING.
    if(LIBURING_USABLE)
        target_include_directories(${name} PRIVATE ${LIBURING_INCLUDE_DIRS})
        target_link_libraries(${name} PRIVATE ${LIBURING_LIBRARIES})
    endif()
    # The tree turns LTO on globally for release builds. The sanitiser probe
    # in src/CMakeLists.txt only sees flags in CMAKE_CXX_FLAGS, and these are
    # set per target, so it would not fire -- pin it off here instead.
    set_target_properties(${name} PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(ARG_MOC)
        set_target_properties(${name} PROPERTIES AUTOMOC ON)
    endif()
    add_dependencies(fuzzers ${name})
endfunction()

add_custom_target(fuzzers)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Parsers that are a single translation unit.
rpi_add_fuzzer(fuzz_bmap             SOURCES ${SRC}/fastboot/bmap.cpp)
rpi_add_fuzzer(fuzz_pieeprom         SOURCES ${SRC}/fastboot/pieeprom.cpp)
rpi_add_fuzzer(fuzz_sparse_roundtrip SOURCES ${SRC}/fastboot/sparse_encoder.cpp)
# The JSON that decides which drives are offered as a write target. lsblk is
# not an attacker, but it is a separate program whose output this parses, and
# the field types move between its versions.
# The ASN.1 length reader on its own. fuzz_der reaches it only with bytes
# shaped like a key, and the lengths that matter are the ones no key carries.
rpi_add_fuzzer(fuzz_asn1_length)
rpi_add_fuzzer(fuzz_drivelist       SOURCES ${SRC}/drivelist/drivelist_linux.cpp)
# The parse entry point sits behind the same define drivelist_test uses;
# without it the harness links against nothing.
target_compile_definitions(fuzz_drivelist PRIVATE DRIVELIST_ENABLE_TEST_API)
# The encoder driven with a block map, which is how it runs on a real write.
# Separate from the round-trip target rather than folded into it: that one
# asks what comes out equals what went in, and with a map that is no longer
# true by design -- an unmapped block is meant to be skipped.
rpi_add_fuzzer(fuzz_sparse_bmap      SOURCES ${SRC}/fastboot/sparse_encoder.cpp
                                             ${SRC}/fastboot/bmap.cpp)
rpi_add_fuzzer(fuzz_fastboot         SOURCES ${SRC}/fastboot/fastboot_protocol.cpp)
rpi_add_fuzzer(fuzz_bootloader_image SOURCES ${SRC}/rpiboot/bootloader_image.cpp)

# The ASN.1 reader for a public key. secureboot.cpp is the only file that
# matters here, but it will not link alone: it reaches the accelerated hash,
# the platform crypto, the boot image writer and the FAT driver, so those come
# with it. GnuTLS is what the Linux hash backend uses.
rpi_add_fuzzer(fuzz_der MOC
    SOURCES ${SRC}/secureboot.cpp
            ${SRC}/linux/acceleratedcryptographichash_gnutls.cpp
            ${SRC}/linux/secureboot_crypto_linux.cpp
            ${SRC}/linux/bootimgcreator_linux.cpp
            ${SRC}/devicewrapper.cpp
            ${SRC}/devicewrapperpartition.cpp
            ${SRC}/devicewrapperfatpartition.cpp
            ${SRC}/devicewrapperblockcacheentry.cpp
            ${SRC}/file_operations.cpp
            ${SRC}/linux/file_operations_linux.cpp
    LIBS gnutls)

# The TAR reader for a downloaded firmware bundle. libarchive does the
# decoding; what is fuzzed here is the walk over what it reports.
rpi_add_fuzzer(fuzz_bootfiles        SOURCES ${SRC}/rpiboot/bootfiles.cpp
                                     LIBS ${LibArchive_LIBRARIES})
rpi_add_fuzzer(fuzz_oslist           SOURCES ${SRC}/oslistparser.cpp)
rpi_add_fuzzer(fuzz_imagesize        SOURCES ${SRC}/imagesizeparser.cpp
                                     LIBS ${LibArchive_LIBRARIES} ${ZSTD_LIBRARIES}
                                          ${LIBLZMA_LIBRARIES} ${ZLIB_LIBRARIES})
# The generator derives the account credential, so it needs the bundled
# password hashing: sha256crypt as a source and yescrypt as a library, exactly
# as customization_generator_test does. QPasswordDigestor is in Qt Network
# rather than Core.
#
# Two targets over the same code, because they run at wildly different
# speeds: the quoting property manages tens of thousands of executions a
# second, and the generators behind it a handful, because yescrypt is
# memory-hard. Sampling them inside one target was tried and the cheap half
# still starved. Both need the same libraries.
rpi_add_fuzzer(fuzz_customisation
    SOURCES ${SRC}/customization_generator.cpp
            ${SRC}/dependencies/sha256crypt/sha256crypt.c
    LIBS Qt6::Network ${YESCRYPT_LIBRARIES})

rpi_add_fuzzer(fuzz_customisation_gen
    SOURCES ${SRC}/customization_generator.cpp
            ${SRC}/dependencies/sha256crypt/sha256crypt.c
    LIBS Qt6::Network ${YESCRYPT_LIBRARIES})

# The bootloader configuration in flash, which can name the OS list
# repository. Text followed by whatever the region held before.
rpi_add_fuzzer(fuzz_blconfig SOURCES ${SRC}/eeprom_repo_override.cpp)

# The buffer between the download and the disk. A stateful subject rather
# than a parse: the input is the order the operations arrive in.
rpi_add_fuzzer(fuzz_ringbuffer SOURCES ${SRC}/ringbuffer.cpp)

# The diff that rebuilds a list model in place. A pure function whose caller
# indexes one list by the other's length.
rpi_add_fuzzer(fuzz_rowdiff SOURCES ${SRC}/model_row_diff.cpp)

# The filename a booting device asks for. The subject is the guard around it,
# not a parse: the name reaches the filesystem.
rpi_add_fuzzer(fuzz_fileserver SOURCES ${SRC}/rpiboot/file_server.cpp)

# Header-only subject; the harness is the whole translation unit.
rpi_add_fuzzer(fuzz_configtxt)

# rpiboot_types.h carries the URI building inline.
rpi_add_fuzzer(fuzz_rpiboot_uri)

# The FAT driver pulls in the device wrapper stack, whose classes are QObjects
# with signals -- without moc output they link but have no metaobject.
set(RPI_FUZZ_FAT_SOURCES
    ${SRC}/devicewrapper.cpp
    ${SRC}/devicewrapperpartition.cpp
    ${SRC}/devicewrapperfatpartition.cpp
    ${SRC}/devicewrapperblockcacheentry.cpp
    ${SRC}/file_operations.cpp
    ${SRC}/linux/file_operations_linux.cpp
)
rpi_add_fuzzer(fuzz_fatdir    MOC SOURCES ${RPI_FUZZ_FAT_SOURCES})
rpi_add_fuzzer(fuzz_parttable MOC SOURCES ${RPI_FUZZ_FAT_SOURCES})
