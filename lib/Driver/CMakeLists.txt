add_library(PlangDriver STATIC Driver.cpp)
target_link_libraries(PlangDriver PRIVATE plang_warnings)
target_compile_definitions(PlangDriver PRIVATE
    PLANG_RUNTIME_DIR="${CMAKE_BINARY_DIR}/runtime"
    # findRuntimeLib (Driver.cpp) needs to know where an INSTALLED layout
    # really put libplang.a/libplang.so, relative to PREFIX -- that is
    # CMAKE_INSTALL_LIBDIR, which GNUInstallDirs resolves to "lib64" (not
    # plain "lib") on Fedora, openSUSE, and other 64-bit multi-lib distros
    # (issue #805). Baking the value in mirrors PLANG_RUNTIME_DIR just
    # above: both are "where CMake's own build put/installed the runtime"
    # facts that only CMake actually knows, fed to the driver at compile
    # time rather than hardcoded or re-derived at run time.
    PLANG_INSTALL_LIBDIR="${CMAKE_INSTALL_LIBDIR}"
)
if(PLANG_ENABLE_RUNTIME_SANITIZER_TESTS)
    # -sanitize-runtime (issue #190 part B option 2): linkSanitized
    # (Driver.cpp) hands the final link of a -sanitize-runtime compile to a
    # real clang rather than ld.lld/ld directly (see its own long comment
    # for why). Baked in from CMAKE_C_COMPILER -- the same compiler
    # plang_runtime_sanitized (runtime/CMakeLists.txt) was itself built
    # with, validated Clang by PLANG_ENABLE_RUNTIME_SANITIZER_TESTS's own
    # configure-time check in the top-level CMakeLists.txt -- rather than
    # whatever "clang" first resolves to on PATH at run time, which could be
    # a different, ABI-incompatible sanitizer runtime version.
    target_compile_definitions(PlangDriver PRIVATE
        PLANG_SANITIZER_CLANG="${CMAKE_C_COMPILER}"
    )
endif()
target_link_libraries(PlangDriver PUBLIC plang_frontend)
if(TARGET LLVM)
    target_link_libraries(PlangDriver PUBLIC LLVM)
else()
    find_library(LLVM_SHARED_LIB NAMES LLVM LLVM-${LLVM_PACKAGE_VERSION} REQUIRED)
    target_link_libraries(PlangDriver PUBLIC ${LLVM_SHARED_LIB})
endif()
