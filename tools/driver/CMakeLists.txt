add_executable(plang driver.cpp)
target_link_libraries(plang PRIVATE PlangDriver)

# Installed binary finds libplang-frontend.so relative to itself.
set_target_properties(plang PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
)
add_dependencies(plang plang_runtime)
