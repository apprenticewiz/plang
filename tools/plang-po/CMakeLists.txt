# The catalog generator.  A build-time tool: it is not installed, because it
# is of no use without the .def files it reads.
add_executable(plang-po plang-po.cpp)
target_link_libraries(plang-po PRIVATE plang_warnings)
