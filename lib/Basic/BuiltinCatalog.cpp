/// BuiltinCatalog.cpp — the compiled-in English diagnostic messages
///
/// This is the text plang falls back to, and the text every translation is a
/// translation *of*.  It is not a table anyone edits: the strings live in the
/// four DiagnosticKinds .def catalogs, and this expands them into an array
/// indexed by DiagID.  Editing a message means editing the .def entry.
///
/// It is compiled in rather than read from a file so that the compiler can
/// always say what is wrong with a program.  A catalog is a data file, and a
/// data file can be missing, truncated, or from another version; a compiler
/// that answered any of those by falling silent would be worse than one that
/// answered in the wrong language.  MessageCatalog reads the translations and
/// comes back here whenever it has nothing to offer — see getDiagFormat in
/// MessageCatalog.cpp.
///
/// This file used to be the whole mechanism: -DPLANG_LOCALE picked one of
/// several such files and a translator was told to copy and translate it,
/// which never worked, because the strings it appears to hold are the .def's.
/// Translations are .po files now and are chosen when plang runs, not when it
/// is built.

#include "plang/Basic/MessageCatalog.h"

namespace plang {

const char* builtinDiagFormat(DiagID ID) {
    // Index matches the DiagID enum value: slot 0 is diag::none, and slot n is
    // the n-th DIAG the aggregator expands.  DiagnosticMessages.def fixes the
    // order the four catalogs are concatenated in, so this cannot drift.
    static const char* const Messages[] = {
        "",  // diag::none
#define DIAG(ID, LEVEL, MSG) MSG,
#include "plang/Basic/DiagnosticMessages.def"
#undef DIAG
    };
    const auto Idx = static_cast<std::size_t>(ID);
    return Idx < (sizeof(Messages) / sizeof(Messages[0]))
               ? Messages[Idx]
               : "<unknown diagnostic>";
}

/// The English severity labels.  Here rather than in Diagnostic.h so that all
/// the compiled-in English is in one place, and so that the catalog has an
/// English to fall back to and to key `label/` entries by.
const char* builtinSeverityLabel(DiagSeverity Sev) {
    switch (Sev) {
    case DiagSeverity::Error:   return "error";
    case DiagSeverity::Warning: return "warning";
    case DiagSeverity::Info:    return "note";
    }
    return "error";
}

} // namespace plang
