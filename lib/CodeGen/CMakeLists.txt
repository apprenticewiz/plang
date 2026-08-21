add_library(PlangCodeGen OBJECT
    ConstFold.cpp
    RuntimeFunctionCache.cpp
    CodeGen.cpp
    CodeGenTypes.cpp
    CodeGenRuntime.cpp
    CodeGenProcs.cpp
    CodeGenStmts.cpp
    CodeGenIO.cpp
    CodeGenExprs.cpp
    CodeGenSchema.cpp
    CodeGenProcParams.cpp
)
target_link_libraries(PlangCodeGen PRIVATE plang_warnings)
# Sema::byteSizeOf: the one cross-subsystem call this makes, the Sema side of
# every layout cross-check in CodeGenTypes.cpp.  Diagnostics never route
# through here at all -- codegenICE is llvm::report_fatal_error, not
# plang's DiagnosticsEngine -- so PlangLocale is deliberately not an edge.
target_link_libraries(PlangCodeGen PRIVATE PlangSema)
target_compile_options(PlangCodeGen PRIVATE -fno-rtti -fno-exceptions -fPIC)
