add_library(PlangSema OBJECT
    Sema.cpp
    SemaExpr.cpp
    SemaFlow.cpp
    SemaStmt.cpp
    SemaType.cpp
    SymbolTable.cpp
)
target_link_libraries(PlangSema PRIVATE plang_warnings)
target_compile_options(PlangSema PRIVATE -fno-rtti -fno-exceptions -fPIC)
