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
# Parser owns a Scanner by value and drives it token by token (PlangLex);
# token/diagnostic descriptions route through the catalog (PlangLocale).
target_link_libraries(PlangParse PRIVATE PlangLex PlangLocale)
target_compile_options(PlangParse PRIVATE -fno-rtti -fno-exceptions -fPIC)
