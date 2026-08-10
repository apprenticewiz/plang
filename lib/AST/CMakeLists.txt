add_library(PlangAST OBJECT AstPrinter.cpp)
target_link_libraries(PlangAST PRIVATE plang_warnings)
target_compile_options(PlangAST PRIVATE -fno-rtti -fno-exceptions -fPIC)
