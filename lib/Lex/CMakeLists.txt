add_library(PlangLex OBJECT Scanner.cpp)
target_link_libraries(PlangLex PRIVATE plang_warnings)
target_compile_options(PlangLex PRIVATE -fno-rtti -fno-exceptions -fPIC)
