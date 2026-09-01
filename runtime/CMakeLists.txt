# The runtime is linked into every program plang compiles, and those may be
# built for an older macOS than plang itself was — MACOSX_DEPLOYMENT_TARGET
# says which.  ld warns about each object in the archive that was built for a
# newer macOS than the link is for, so a runtime built for the host would put
# a page of warnings in front of anyone targeting anything but it.  Building it
# for the oldest macOS that runs on either architecture makes it usable by all
# of them; nothing in here needs a recent one.  An explicit setting is left
# alone, since someone who gave one has said what they want.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
endif()

set(PLANG_RUNTIME_SOURCES
    plang_io.cpp
    plang_real.cpp
    plang_sys.cpp
    plang_math.cpp
    plang_file.cpp
    plang_str.cpp
    plang_sstr.cpp
    plang_val.cpp
    plang_time.cpp
    plang_crt.cpp
    plang_strings.cpp
    plang_printer.cpp
    plang_dos.cpp
)
add_library(plang_runtime        STATIC ${PLANG_RUNTIME_SOURCES})
add_library(plang_runtime_shared SHARED ${PLANG_RUNTIME_SOURCES})
set_target_properties(plang_runtime        PROPERTIES OUTPUT_NAME "plang")
set_target_properties(plang_runtime_shared PROPERTIES OUTPUT_NAME "plang")

# plang/Basic/Arith.h holds the integer operations whose Pascal meaning differs
# from C's.  The runtime and the constant folders share it so that a value
# computed at run time and one folded at compile time cannot disagree.
foreach(rt_target plang_runtime plang_runtime_shared)
    target_include_directories(${rt_target}
        PRIVATE ${CMAKE_SOURCE_DIR}/include)
endforeach()

# The runtime is linked into the Pascal programs that plang compiles, and the
# driver performs those links without any sanitizer runtime.  Instrumenting the
# runtime would leave every compiled program with undefined __asan_* symbols,
# so opt these targets out of the project-wide sanitizer flags.
if(PLANG_SANITIZE)
    foreach(rt_target plang_runtime plang_runtime_shared)
        target_compile_options(${rt_target} PRIVATE -fno-sanitize=all)
        target_link_options(${rt_target}    PRIVATE -fno-sanitize=all)
    endforeach()
endif()

# ---- Sanitized runtime variant for the runtime-exerciser harness (issue
# #190 part B option 1; PLANG_ENABLE_RUNTIME_SANITIZER_TESTS itself is
# declared, and validated, in the top-level CMakeLists.txt -- see that
# option's own long comment for the full "why a separate target" story). ----
#
# A SEPARATE target, plang_runtime_sanitized, built from the exact same
# PLANG_RUNTIME_SOURCES list the two targets above already share -- so it
# cannot silently drift out of sync with the real runtime -- but compiled
# independently with ASan+UBSan and never linked into plang_runtime,
# plang_runtime_shared, or anything either of those two feeds (the driver,
# every compiled Pascal program). test/unittests/RuntimeSanitized/ is the
# only thing that ever links this.
#
# PUBLIC (not PRIVATE) on both target_compile_options and target_link_options
# below: a static library's own compile flags never apply to a consumer's own
# translation units, so PUBLIC is what makes -fsanitize=address,undefined
# also apply to test/unittests/RuntimeSanitized/'s own .cpp files (stack-
# buffer and use-after-scope checks need the CALLER instrumented too, not
# just plang_runtime_sanitized's own sources) -- and PUBLIC on
# target_link_options is what pulls the ASan/UBSan runtime itself into the
# final test executable's link, since a static library carries no runtime of
# its own the way a shared one would.
if(PLANG_ENABLE_RUNTIME_SANITIZER_TESTS)
    add_library(plang_runtime_sanitized STATIC ${PLANG_RUNTIME_SOURCES})
    target_include_directories(plang_runtime_sanitized
        PRIVATE ${CMAKE_SOURCE_DIR}/include)
    # -fno-sanitize=vptr: same reason the sanitizers CI job's own PLANG_SANITIZE
    # configure step disables it (see that job's own comment) -- the runtime is
    # built -fno-rtti-equivalent (plain C structs, no polymorphism), and UBSan's
    # vptr check needs RTTI to have anything to check in the first place.
    target_compile_options(plang_runtime_sanitized PUBLIC
        -fsanitize=address,undefined -fno-sanitize=vptr
        -fno-omit-frame-pointer -g)
    target_link_options(plang_runtime_sanitized PUBLIC
        -fsanitize=address,undefined -fno-sanitize=vptr)
endif()
