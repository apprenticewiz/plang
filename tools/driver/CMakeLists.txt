add_executable(plang driver.cpp)
target_link_libraries(plang PRIVATE PlangDriver)

# Installed binary finds the shared front-end library relative to itself, so
# that it is found wherever the tree was installed rather than only under the
# prefix it was configured with.  The two loaders spell "the directory this
# came from" differently.
if(APPLE)
    set(plang_loader_dir "@loader_path")
else()
    set(plang_loader_dir "$ORIGIN")
endif()
set_target_properties(plang PROPERTIES
    INSTALL_RPATH "${plang_loader_dir}/../lib"
)
add_dependencies(plang plang_runtime)
