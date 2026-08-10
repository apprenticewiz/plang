# Locale-specific diagnostic message strings.
#
# Each locale is a single .cpp file that implements getDiagFormat(DiagID).
# Only one locale is compiled into the binary at a time; choose it with:
#
#   cmake -DPLANG_LOCALE=en_US ...   (default)
#   cmake -DPLANG_LOCALE=fr_FR ...   (when a French translation exists)
#
# To add a new translation:
#   1. Copy lib/Basic/en_US.cpp to lib/Basic/<locale>.cpp
#   2. Translate every string returned by getDiagFormat()
#   3. Rebuild with -DPLANG_LOCALE=<locale>
#
# The DiagID enum values in include/plang/Basic/DiagnosticIDs.h are the stable
# keys; they must never be renumbered without updating all locale files.

set(PLANG_LOCALE "en_US" CACHE STRING
    "Diagnostic message locale to compile in (e.g. en_US, fr_FR)")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${PLANG_LOCALE}.cpp")
    message(FATAL_ERROR
        "No diagnostic message file for locale '${PLANG_LOCALE}' "
        "(looked for lib/Basic/${PLANG_LOCALE}.cpp). "
        "Available: en_US")
endif()

add_library(PlangLocale OBJECT ${PLANG_LOCALE}.cpp)
target_compile_options(PlangLocale PRIVATE -fno-rtti -fno-exceptions -fPIC)
target_link_libraries(PlangLocale PRIVATE plang_warnings)

# Language-independent support: source buffers and positions.
add_library(PlangBasic OBJECT SourceManager.cpp)
target_compile_options(PlangBasic PRIVATE -fno-rtti -fno-exceptions -fPIC)
target_link_libraries(PlangBasic PRIVATE plang_warnings)
