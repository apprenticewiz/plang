add_library(PlangDriver STATIC Driver.cpp)
target_link_libraries(PlangDriver PRIVATE plang_warnings)
target_compile_definitions(PlangDriver PRIVATE
    PLANG_RUNTIME_DIR="${CMAKE_BINARY_DIR}/runtime"
)
target_link_libraries(PlangDriver PUBLIC plang_frontend)
if(TARGET LLVM)
    target_link_libraries(PlangDriver PUBLIC LLVM)
else()
    find_library(LLVM_SHARED_LIB NAMES LLVM LLVM-${LLVM_PACKAGE_VERSION} REQUIRED)
    target_link_libraries(PlangDriver PUBLIC ${LLVM_SHARED_LIB})
endif()
