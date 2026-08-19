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
# Sema::byteSizeOf: the one cross-subsystem call this makes, the Sema side of
# every layout cross-check in CodegenTypes.cpp.  Diagnostics never route
# through here at all -- codegenICE is llvm::report_fatal_error, not
# plang's DiagnosticsEngine -- so PlangLocale is deliberately not an edge.
target_link_libraries(PlangCodeGen PRIVATE PlangSema)
target_compile_options(PlangCodeGen PRIVATE -fno-rtti -fno-exceptions -fPIC)
