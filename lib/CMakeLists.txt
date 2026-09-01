# Each subsystem below declares target_link_libraries edges to the other
# subsystems it actually calls into (Sema depending on Lex and Parse to run
# them over a .pmi module interface, CodeGen depending on Sema for
# Sema::byteSizeOf, and so on) -- not because the OBJECT libraries assembled
# below need it to compile (every target shares one global include path, set
# once in the top-level CMakeLists.txt, so a missing edge would not by itself
# break the build) but because nothing else records the real dependency
# direction anywhere.  Without it, every subsystem's object files land in one
# shared library with no layering encoded at all, and a future accidental
# violation -- Sema code reaching into CodeGen internals, say -- is exactly as
# silent as a missing edge here would be.  Getting the graph right now also
# means a future split of any of these into a real separate static or shared
# library fails at exactly the missing edge, rather than needing the whole
# graph rediscovered by trial and error.
add_subdirectory(Basic)  # locale-specific diagnostic messages
add_subdirectory(AST)
add_subdirectory(Lex)
add_subdirectory(Parse)
add_subdirectory(Sema)
add_subdirectory(CodeGen)
add_subdirectory(Frontend)
add_subdirectory(Driver)

# ---- libplang-frontend ----------------------------------------------------
# Shared library that bundles all subsystems for the driver and for embedders.
# Each subsystem is compiled as an OBJECT library so their translation units
# are linked directly into the shared library without intermediate archives.
add_library(plang_frontend SHARED
    $<TARGET_OBJECTS:PlangLocale>   # locale-selected diagnostic messages
    $<TARGET_OBJECTS:PlangBasic>    # source buffers and positions
    $<TARGET_OBJECTS:PlangAST>
    $<TARGET_OBJECTS:PlangLex>
    $<TARGET_OBJECTS:PlangParse>
    $<TARGET_OBJECTS:PlangSema>
    $<TARGET_OBJECTS:PlangCodeGen>
    $<TARGET_OBJECTS:PlangFrontend>
)
set_target_properties(plang_frontend PROPERTIES
    OUTPUT_NAME "plang-frontend"
    SOVERSION   0
    VERSION     ${PROJECT_VERSION}
)
# Usage requirement for the EXPORT'd plang::plang_frontend target (issue
# #191): the top-level CMakeLists.txt's own include_directories() calls are
# directory-scoped and don't propagate through install(EXPORT ...), so an
# external consumer needs this recorded on the target itself. The
# BUILD_INTERFACE entries just mirror what every other target already gets
# from the directory-scoped include_directories() above; INSTALL_INTERFACE
# is the one path only an external consumer of the installed package uses.
target_include_directories(plang_frontend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(TARGET LLVM)
    target_link_libraries(plang_frontend PRIVATE LLVM)
else()
    find_library(LLVM_SHARED_LIB NAMES LLVM LLVM-${LLVM_PACKAGE_VERSION} REQUIRED)
    target_link_libraries(plang_frontend PRIVATE ${LLVM_SHARED_LIB})
endif()
