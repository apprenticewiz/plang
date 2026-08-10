cmake_minimum_required(VERSION 3.16)
project(plang C CXX)
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

# All plang headers live under include/; LLVM headers come from the LLVM install.
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_subdirectory(lib)
add_subdirectory(runtime)
add_subdirectory(tools)

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
# Public headers for embedders (exclude internal AST detail headers)
install(DIRECTORY include/plang/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/plang
    FILES_MATCHING PATTERN "*.h"
    PATTERN "AST/Ast*.h" EXCLUDE
)
# Man page
install(FILES docs/plang.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
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
