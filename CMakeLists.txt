cmake_minimum_required(VERSION 3.16)

# ---- Version ----------------------------------------------------------------
#
# The version is written here and nowhere else.  It used to be written in four
# places by hand — this file's library VERSION, Version.h, the man page header
# and the changelog — and cutting 0.1.2 updated two of them, so that release
# shipped a compiler calling itself 0.1.2, a shared library built as 0.1.1 and a
# man page whose header agreed with the library.
#
# CMake will not accept a pre-release suffix in a project version, and a shared
# library named libplang-frontend.so.0.2.0-pre is not something anyone wants
# either, so the two are separate: PROJECT_VERSION is the numeric one, and only
# what a human reads carries the suffix.
#
# To cut a release:
#   1. set the suffix below to ""      (a development build says 0.2.0-pre;
#                                       the release says 0.2.0)
#   2. set PLANG_MANPAGE_DATE to the release date
#   3. move the changelog's [Unreleased] section under the new version
#   4. after tagging, bump VERSION here and set the suffix back to "-pre"
#
project(plang VERSION 0.2.0 LANGUAGES C CXX)

# Empty in a release, "-pre" between them, so that a development snapshot
# cannot be mistaken for the release it is heading towards.  Semantic
# Versioning §9 gives a pre-release lower precedence than the version it
# qualifies, which is the right way round: 0.1.3 < 0.2.0-pre < 0.2.0.
set(PLANG_VERSION_SUFFIX "-pre")
set(PLANG_VERSION_STRING "${PROJECT_VERSION}${PLANG_VERSION_SUFFIX}")

# The date in the man page header, which by convention is when the page last
# changed rather than when it was built — a build date would make two builds of
# the same source differ.
set(PLANG_MANPAGE_DATE "2026-08-14")

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
    add_subdirectory(test)
else()
    message(STATUS "Tests: disabled (-DPLANG_ENABLE_TESTS=ON to build them)")
endif()
