/// en_US.cpp — English (United States) diagnostic messages for plang.
///
/// This file implements getDiagFormat(DiagID), returning the English
/// message template for every diagnostic ID.
///
/// HOW TO ADD A TRANSLATION
/// ------------------------
///   1. cp lib/Basic/en_US.cpp lib/Basic/<locale>.cpp
///   2. Translate every string in the Messages[] table below.
///      The DiagID integer values (array indices) must stay in the same order.
///      Keep %N placeholders — they can be reordered for your language's
///      word order, but must all still be present.
///   3. cmake -DPLANG_LOCALE=<locale> && cmake --build build
///
/// WHY THIS FILE EXISTS
/// --------------------
/// getDiagFormat() is declared in include/plang/Basic/DiagnosticIDs.h but
/// intentionally left undefined there.  Only one locale .cpp file is linked
/// per build (selected by -DPLANG_LOCALE=en_US).  This is the hook point
/// for future gettext / ICU / custom-catalog integration.

#include "plang/Basic/Diagnostic.h"

namespace plang {

const char* getDiagFormat(DiagID ID) {
    // String table generated from DiagnosticMessages.def.
    // Array index matches DiagID enum value — do not reorder.
    static const char* const Messages[] = {
        "",  // diag::none
#define DIAG(ID, LEVEL, MSG) MSG,
#include "plang/Basic/DiagnosticMessages.def"
#undef DIAG
    };
    auto Idx = static_cast<size_t>(ID);
    return Idx < (sizeof(Messages) / sizeof(Messages[0]))
               ? Messages[Idx]
               : "<unknown diagnostic>";
}

} // namespace plang
