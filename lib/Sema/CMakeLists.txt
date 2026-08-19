add_library(PlangSema OBJECT
    Sema.cpp
    SemaExpr.cpp
    SemaFlow.cpp
    SemaStmt.cpp
    SemaType.cpp
    SymbolTable.cpp
)
target_link_libraries(PlangSema PRIVATE plang_warnings)
# EP §6.11: Sema::loadPMI builds and runs a Scanner+Parser directly over a
# .pmi module-interface file (PlangLex, PlangParse); diagnostics route
# through the catalog (PlangLocale).  PlangBasic arrives transitively through
# PlangLex/PlangParse's own edges to it -- Sema never calls a SourceManager
# method itself, only constructs one to hand to the Scanner it builds.
target_link_libraries(PlangSema PRIVATE PlangLex PlangParse PlangLocale)
target_compile_options(PlangSema PRIVATE -fno-rtti -fno-exceptions -fPIC)
