cmake_minimum_required(VERSION 3.16)

# ---- Version ----------------------------------------------------------------
#
# The numeric version lives in one place, the top-level VERSION file, and
# nowhere else -- it used to be written by hand in four places (this file's
# library VERSION, Version.h, the man page header, and the changelog), and
# cutting 0.1.2 updated two of them, so that release shipped a compiler
# calling itself 0.1.2, a shared library built as 0.1.1, and a man page whose
# header agreed with the library.
#
# VERSION holds a bare MAJOR.MINOR.PATCH, no suffix -- CMake will not accept
# one in project(VERSION ...), and a shared library named
# libplang-frontend.so.0.3.1-pre is not something anyone wants either. What a
# human actually reads (PLANG_VERSION_STRING, below) is never hand-maintained
# at all in the common case: it is git-describe's own answer to "how far is
# the working tree from the last release tag," computed fresh at every
# configure. Sitting exactly on a release tag with a clean tree gives the
# bare number back ("0.3.2"); N commits past it gives "0.3.2+N.g<hash>"
# (SemVer build metadata, which per the spec's own §10 carries no precedence
# meaning -- exactly right here, since this string is diagnostic, not a
# promise about what the next real release will be numbered); a dirty tree
# appends ".dirty".
#
# On `main`, VERSION itself is a deliberate, non-committal sentinel --
# "99.99.99" -- not the last real release number. Two reasons: it is what
# actually drives project(VERSION ...) and the shared library's SONAME
# unconditionally, regardless of whether git-describe succeeds, so a plain
# `main` build's library has never silently claimed to BE a shipped release;
# and it is the only version string a source tarball export (no git, or no
# matching tag) ever falls back to, where the previous scheme's "sits at the
# last real release" behavior meant an export of `main` well past 0.3.2
# claimed to be "0.3.0" with nothing to say otherwise. This mirrors Redis's
# own long-standing "999.999.999" convention, surveyed and initially passed
# over in favor of "just let it sit," then revisited once main's staleness
# was seen as actively misleading rather than merely imprecise.
#
# Recomputed at configure time only, not on every incremental build --
# `cmake`'s own version string has the identical property (its git-hash
# suffix is likewise configure-time-cached). CI always configures fresh, so
# this only means a local incremental build's --version can lag one commit
# behind until the next `cmake -S . -B build`.
#
# VERSION only ever holds a real number on a release branch, set at the
# point a release is actually being prepared, immediately reverting to
# 99.99.99 on `main` (which is never itself touched by a release cut):
#
#   1. cut the release branch (or reuse an existing one, for a patch release)
#   2. on that branch, set VERSION to the exact target number
#   3. set PLANG_MANPAGE_DATE to the release date
#   4. move the changelog's [Unreleased] section under the new version
#   5. tag from that branch once ready; `main` is untouched throughout
#
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PLANG_BASE_VERSION LIMIT_COUNT 1)
string(STRIP "${PLANG_BASE_VERSION}" PLANG_BASE_VERSION)

project(plang VERSION ${PLANG_BASE_VERSION} LANGUAGES C CXX)

set(PLANG_VERSION_STRING "${PLANG_BASE_VERSION}")
find_package(Git QUIET)
if(GIT_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --match "v[0-9]*"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE PLANG_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE PLANG_GIT_DESCRIBE_RESULT
    )
    # Matches e.g. "v0.3.0" (exactly on the tag), "v0.3.0-12-gabc1234" (12
    # commits past it), either optionally followed by "-dirty". A tarball
    # export or a shallow clone with no reachable tag makes describe fail
    # (nonzero exit, or no match) -- PLANG_VERSION_STRING keeps its plain
    # VERSION-file fallback from above in that case.
    if(PLANG_GIT_DESCRIBE_RESULT EQUAL 0 AND PLANG_GIT_DESCRIBE MATCHES
       "^v?([0-9]+\\.[0-9]+\\.[0-9]+)(-([0-9]+)-g([0-9a-f]+))?(-dirty)?$")
        set(PLANG_VERSION_STRING "${CMAKE_MATCH_1}")
        if(CMAKE_MATCH_3)
            set(PLANG_VERSION_STRING "${PLANG_VERSION_STRING}+${CMAKE_MATCH_3}.g${CMAKE_MATCH_4}")
        endif()
        if(CMAKE_MATCH_5)
            set(PLANG_VERSION_STRING "${PLANG_VERSION_STRING}.dirty")
        endif()
    endif()
endif()

# The date in the man page header, which by convention is when the page last
# changed rather than when it was built — a build date would make two builds of
# the same source differ.
set(PLANG_MANPAGE_DATE "2026-08-26")

message(STATUS "plang version ${PLANG_VERSION_STRING}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS  OFF)

# ---- LLVM ----
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

# ---- Compiler warnings (applied to plang sources, not generated test files) ----
add_library(plang_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(plang_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter   # LLVM headers trigger this extensively
        -Wno-unused-function    # some inline helpers are conditionally used
    )
endif()

# ---- Sanitizer preset (-DPLANG_SANITIZE=address,undefined or similar) ----
option(PLANG_SANITIZE "Comma-separated sanitizer list, e.g. 'address,undefined'" "")
if(PLANG_SANITIZE)
    message(STATUS "Sanitizers: ${PLANG_SANITIZE}")
    # -fno-sanitize=vptr: the libraries are built -fno-rtti (LLVM convention) and
    # UBSan's vptr check needs RTTI, so leaving it on reports every shared_ptr
    # control block as having an invalid vptr.
    add_compile_options(-fsanitize=${PLANG_SANITIZE} -fno-sanitize=vptr
                        -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${PLANG_SANITIZE} -fno-sanitize=vptr)
endif()

# Version.h is the one generated header, so the build directory joins the
# include path — before the source one, though nothing there can shadow it now
# that the hand-written copy is gone.
configure_file(
    ${CMAKE_SOURCE_DIR}/include/plang/Basic/Version.h.in
    ${CMAKE_BINARY_DIR}/include/plang/Basic/Version.h
    @ONLY
)
configure_file(
    ${CMAKE_SOURCE_DIR}/docs/plang.1.in
    ${CMAKE_BINARY_DIR}/docs/plang.1
    @ONLY
)

# All plang headers live under include/; LLVM headers come from the LLVM install.
include_directories(${CMAKE_BINARY_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Every built executable (plang, plang-po, and the GoogleTest binaries under
# test/unittests/) lands in one directory with a configure-time-known path,
# rather than scattered across each target's own source-mirroring build
# subdirectory. test/'s lit.site.cfg.py needs to name plang's path in a plain
# configure_file() substitution, which cannot evaluate the
# $<TARGET_FILE:plang> generator expression every target already used before
# this existed -- collecting outputs here is what makes that substitution
# possible without a second CMake generation pass.
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

add_subdirectory(lib)
add_subdirectory(runtime)
add_subdirectory(tools)

# ---- Message catalogs ------------------------------------------------------
#
# The English a translator works from is generated from the same .def files the
# compiler compiles in, so the two cannot drift.  qps_ploc is not a language:
# every message is the English wrapped in [! !], which is the only way a test
# can tell a build that loaded a catalog from one that fell back to English --
# and falling back is silent by design, so without it a broken install passes.
set(PLANG_LOCALE_BUILD_DIR ${CMAKE_BINARY_DIR}/share/plang/locale)
set(PLANG_DEF_FILES
    ${CMAKE_SOURCE_DIR}/include/plang/Basic/DiagnosticDriverKinds.def
    ${CMAKE_SOURCE_DIR}/include/plang/Basic/DiagnosticLexKinds.def
    ${CMAKE_SOURCE_DIR}/include/plang/Basic/DiagnosticParseKinds.def
    ${CMAKE_SOURCE_DIR}/include/plang/Basic/DiagnosticSemaKinds.def
)
set(PLANG_DEF_ARGS)
foreach(d ${PLANG_DEF_FILES})
    list(APPEND PLANG_DEF_ARGS --def ${d})
endforeach()

add_custom_command(
    OUTPUT  ${PLANG_LOCALE_BUILD_DIR}/en_US.po
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PLANG_LOCALE_BUILD_DIR}
    COMMAND plang-po --output ${PLANG_LOCALE_BUILD_DIR}/en_US.po
                     --language en_US ${PLANG_DEF_ARGS}
    DEPENDS plang-po ${PLANG_DEF_FILES}
    COMMENT "Generating en_US.po"
    VERBATIM
)
add_custom_command(
    OUTPUT  ${PLANG_LOCALE_BUILD_DIR}/qps_ploc.po
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PLANG_LOCALE_BUILD_DIR}
    COMMAND plang-po --output ${PLANG_LOCALE_BUILD_DIR}/qps_ploc.po
                     --language qps_ploc --pseudo ${PLANG_DEF_ARGS}
    DEPENDS plang-po ${PLANG_DEF_FILES}
    COMMENT "Generating qps_ploc.po"
    VERBATIM
)
# The hand-written catalogs.  Unlike en_US.po these are source: a translator
# edits them, so they live in po/ and are copied into the build tree beside the
# generated ones, which is where an uninstalled plang and the test suite look.
file(GLOB PLANG_PO_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/po/*.po)
set(PLANG_STAGED_PO)
foreach(po ${PLANG_PO_FILES})
    get_filename_component(po_name ${po} NAME)
    add_custom_command(
        OUTPUT  ${PLANG_LOCALE_BUILD_DIR}/${po_name}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PLANG_LOCALE_BUILD_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${po} ${PLANG_LOCALE_BUILD_DIR}/${po_name}
        DEPENDS ${po}
        COMMENT "Staging ${po_name}"
        VERBATIM
    )
    list(APPEND PLANG_STAGED_PO ${PLANG_LOCALE_BUILD_DIR}/${po_name})
endforeach()

add_custom_target(plang_catalogs ALL
    DEPENDS ${PLANG_LOCALE_BUILD_DIR}/en_US.po
            ${PLANG_LOCALE_BUILD_DIR}/qps_ploc.po
            ${PLANG_STAGED_PO}
)

# ---- Installation ----
include(GNUInstallDirs)

install(TARGETS plang
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS plang_frontend
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(TARGETS plang_runtime plang_runtime_shared
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
# Public headers for embedders (exclude internal AST detail headers).
# Version.h.in is not matched, being a template rather than a header; the
# header it generates is installed from the build directory below.
install(DIRECTORY include/plang/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/plang
    FILES_MATCHING PATTERN "*.h"
    PATTERN "AST/Ast*.h" EXCLUDE
)
install(FILES ${CMAKE_BINARY_DIR}/include/plang/Basic/Version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/plang/Basic
)
# Man page, generated so that its header cannot disagree with the compiler
install(FILES ${CMAKE_BINARY_DIR}/docs/plang.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)
# Message catalogs.  The compiler finds these relative to its own binary, the
# way it already finds its runtime library, so bin/ and share/ have to stay in
# the relation the prefix put them in.
install(DIRECTORY ${PLANG_LOCALE_BUILD_DIR}/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/plang/locale
    FILES_MATCHING PATTERN "*.po"
)

# ---- Tests ----
# Off by default: the suite needs GoogleTest, which someone building the
# compiler to use it has no other reason to install, and the conformance and
# acceptance cases are the bulk of the build.
option(PLANG_ENABLE_TESTS "Build the test suite" OFF)

# enable_testing() unconditionally, so that ctest in a build without the tests
# says "No tests were found" rather than failing to find CTestTestfile.cmake.
enable_testing()
if(PLANG_ENABLE_TESTS)
    # Mirrors Clang's own split: test/ is lit+FileCheck, black-box, drives
    # the compiler as a subprocess (see test/README.md); test/unittests/ is
    # GoogleTest, in-process C++-API-level tests -- the plang analogue of
    # clang/test/ vs. clang/unittests/ (issue #34, issue #43).
    add_subdirectory(test/unittests)
    # test/CMakeLists.txt itself skips gracefully (a WARNING, not a configure
    # failure) when lit/FileCheck aren't found, since PLANG_ENABLE_TESTS is
    # already opt-in for exactly the "don't force a dependency on someone who
    # has no other reason to install it" reason, and a developer who only
    # wants the GoogleTest tier shouldn't be blocked by one lit tool's
    # absence.
    add_subdirectory(test)
else()
    message(STATUS "Tests: disabled (-DPLANG_ENABLE_TESTS=ON to build them)")
endif()
