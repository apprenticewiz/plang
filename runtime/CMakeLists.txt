set(PLANG_RUNTIME_SOURCES
    plang_io.cpp
    plang_real.cpp
    plang_sys.cpp
    plang_math.cpp
    plang_file.cpp
    plang_str.cpp
    plang_time.cpp
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

# The runtime is linked into the Pascal programs that plang compiles, and those
# links are performed by ld.lld without any sanitizer runtime.  Instrumenting
# the runtime would leave every compiled program with undefined __asan_*
# symbols, so opt these targets out of the project-wide sanitizer flags.
if(PLANG_SANITIZE)
    foreach(rt_target plang_runtime plang_runtime_shared)
        target_compile_options(${rt_target} PRIVATE -fno-sanitize=all)
        target_link_options(${rt_target}    PRIVATE -fno-sanitize=all)
    endforeach()
endif()
