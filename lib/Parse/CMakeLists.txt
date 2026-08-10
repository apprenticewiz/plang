add_library(PlangParse OBJECT
    Parser.cpp
    ParseDecl.cpp
    ParseExpr.cpp
    ParseInit.cpp
    ParseModule.cpp
    ParseStmt.cpp
    ParseType.cpp
)
target_link_libraries(PlangParse PRIVATE plang_warnings)
target_compile_options(PlangParse PRIVATE -fno-rtti -fno-exceptions -fPIC)
