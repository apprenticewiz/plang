add_library(PlangFrontend OBJECT Frontend.cpp)
target_link_libraries(PlangFrontend PRIVATE plang_warnings)
target_compile_options(PlangFrontend PRIVATE -fno-rtti -fno-exceptions -fPIC)
