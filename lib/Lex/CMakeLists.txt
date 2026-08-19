add_library(PlangLex OBJECT Scanner.cpp)
target_link_libraries(PlangLex PRIVATE plang_warnings)
# SourceManager (PlangBasic) for buffer/position tracking; DiagnosticsEngine's
# catalog lookup (PlangLocale) for the messages a scan error reports.
target_link_libraries(PlangLex PRIVATE PlangBasic PlangLocale)
target_compile_options(PlangLex PRIVATE -fno-rtti -fno-exceptions -fPIC)
