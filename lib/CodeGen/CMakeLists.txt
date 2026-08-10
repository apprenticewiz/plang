add_library(PlangCodeGen OBJECT
    Codegen.cpp
    CodegenTypes.cpp
    CodegenRuntime.cpp
    CodegenProcs.cpp
    CodegenStmts.cpp
    CodegenIO.cpp
    CodegenExprs.cpp
    CodegenSchema.cpp
    CodegenProcParams.cpp
)
target_link_libraries(PlangCodeGen PRIVATE plang_warnings)
target_compile_options(PlangCodeGen PRIVATE -fno-rtti -fno-exceptions -fPIC)
