add_subdirectory(Basic)  # locale-specific diagnostic messages
add_subdirectory(AST)
add_subdirectory(Lex)
add_subdirectory(Parse)
add_subdirectory(Sema)
add_subdirectory(CodeGen)
add_subdirectory(Frontend)
add_subdirectory(Driver)

# ---- libplang-frontend.so ------------------------------------------------
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
    VERSION     0.1.1
)
if(TARGET LLVM)
    target_link_libraries(plang_frontend PRIVATE LLVM)
else()
    find_library(LLVM_SHARED_LIB NAMES LLVM LLVM-${LLVM_PACKAGE_VERSION} REQUIRED)
    target_link_libraries(plang_frontend PRIVATE ${LLVM_SHARED_LIB})
endif()
