# Diagnostic message text.
#
# en_US.cpp is the compiled-in English, expanded from the DiagnosticKinds .def
# catalogs; MessageCatalog.cpp reads a translation of it from a .po file at
# run time and falls back here whenever it has nothing to say.  Which language
# plang speaks is therefore a question answered when it runs, not when it is
# built, and adding one no longer needs a rebuild.
#
# The target keeps the name PlangLocale because lib/CMakeLists.txt links it by
# that name; it is no longer "the selected locale", it is the message layer.
#
# The DiagID enum values are the stable keys, and DiagnosticMessages.def fixes
# the order the four catalogs are concatenated in.  A .po file keys on the
# identifier rather than on that number, so reordering the catalogs cannot
# silently repoint a translation at the wrong message.

add_library(PlangLocale OBJECT
    en_US.cpp           # compiled-in English: the fallback, and the base text
    MessageCatalog.cpp  # the .po reader, and getDiagFormat itself
)
target_compile_options(PlangLocale PRIVATE -fno-rtti -fno-exceptions -fPIC)
target_link_libraries(PlangLocale PRIVATE plang_warnings)
# Where the catalogs are in the build tree.  An installed plang finds them
# relative to its own binary; an uninstalled one -- and every test binary,
# which is its own main executable and so has no such relation -- finds them
# here.  Same two-step the driver uses for its runtime library.
target_compile_definitions(PlangLocale PRIVATE
    PLANG_CATALOG_DIR="${CMAKE_BINARY_DIR}/share/plang/locale")

# Language-independent support: source buffers and positions.
add_library(PlangBasic OBJECT SourceManager.cpp)
target_compile_options(PlangBasic PRIVATE -fno-rtti -fno-exceptions -fPIC)
target_link_libraries(PlangBasic PRIVATE plang_warnings)
