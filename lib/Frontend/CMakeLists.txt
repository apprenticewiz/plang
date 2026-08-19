add_library(PlangFrontend OBJECT Frontend.cpp)
target_link_libraries(PlangFrontend PRIVATE plang_warnings)
# The orchestrator: builds and runs a Scanner (PlangLex) and Parser
# (PlangParse), a Sema (PlangSema), and a Codegen (PlangCodeGen) in sequence;
# calls printAst for -dump-ast (PlangAST); and prints diagnostics through the
# header-only DiagnosticPrinter, whose calls into the catalog (PlangLocale)
# and SourceManager (PlangBasic) therefore land directly in Frontend.cpp's
# own object code rather than some intermediary's.
target_link_libraries(PlangFrontend PRIVATE
    PlangLex PlangParse PlangSema PlangCodeGen PlangAST PlangLocale PlangBasic)
target_compile_options(PlangFrontend PRIVATE -fno-rtti -fno-exceptions -fPIC)
