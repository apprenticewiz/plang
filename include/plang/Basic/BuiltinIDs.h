#pragma once

/// BuiltinIDs.h — the identity of each required procedure and function
///
/// Generated from Builtins.def, which is also where their arity, their result
/// type and the dialects that require them are written.  A call resolved to a
/// builtin records one of these, so that what Sema decided is carried forward
/// as an identity rather than re-derived from the spelling.

#include "plang/Basic/LangOptions.h"

#include <cstddef>
#include <string_view>

namespace plang {

enum class BuiltinID {
    None,
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result) Id,
#include "plang/Basic/Builtins.def"
};

/// How many there are, not counting None.  The switches below are over the
/// enumeration itself, so the compiler already makes them cover a new entry;
/// this is for a caller that walks the list and has to show it walked all of
/// it, which a loop written over a .def cannot otherwise demonstrate.
inline constexpr std::size_t NumBuiltins = 0
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result) + 1
#include "plang/Basic/Builtins.def"
    ;

/// What a builtin is called in the source, folded to lower case.
[[nodiscard]] constexpr std::string_view builtinSpelling(BuiltinID ID) {
    switch (ID) {
    case BuiltinID::None: return "";
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result) \
    case BuiltinID::Id: return Spelling;
#include "plang/Basic/Builtins.def"
    }
    return "";
}

/// The dialects that require \p ID.  A name is declared in every dialect and
/// refused in the ones this does not name, so that using it where it does not
/// belong says what it is rather than that it is undefined.
[[nodiscard]] constexpr unsigned builtinDialects(BuiltinID ID) {
    switch (ID) {
    case BuiltinID::None: return 0;
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result) \
    case BuiltinID::Id: return (Dialects);
#include "plang/Basic/Builtins.def"
    }
    return 0;
}

/// Fewest and most arguments accepted; Max of -1 means no upper bound, and
/// also marks the ones whose shape is checked where they are lowered.
struct BuiltinArity { int Min, Max; };

[[nodiscard]] constexpr BuiltinArity builtinArity(BuiltinID ID) {
    switch (ID) {
    case BuiltinID::None: return {0, -1};
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result) \
    case BuiltinID::Id: return {MinArgs, MaxArgs};
#include "plang/Basic/Builtins.def"
    }
    return {0, -1};
}

[[nodiscard]] constexpr bool builtinIsFunction(BuiltinID ID) {
    switch (ID) {
    case BuiltinID::None: return false;
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result) \
    case BuiltinID::Id: return std::string_view(#Kind) == "Func";
#include "plang/Basic/Builtins.def"
    }
    return false;
}

/// The result type of a function builtin, as a tag Sema turns into a Type.
enum class BuiltinResult { None, Int, Real, Char, Bool, Str, Complex, BindingType };

[[nodiscard]] constexpr BuiltinResult builtinResult(BuiltinID ID) {
#define R_None        BuiltinResult::None
#define R_Int         BuiltinResult::Int
#define R_Real        BuiltinResult::Real
#define R_Char        BuiltinResult::Char
#define R_Bool        BuiltinResult::Bool
#define R_Str         BuiltinResult::Str
#define R_Complex     BuiltinResult::Complex
#define R_BindingType BuiltinResult::BindingType
    switch (ID) {
    case BuiltinID::None: return BuiltinResult::None;
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result) \
    case BuiltinID::Id: return (Result);
#include "plang/Basic/Builtins.def"
    }
    return BuiltinResult::None;
#undef R_BindingType
#undef R_Complex
#undef R_Str
#undef R_Bool
#undef R_Char
#undef R_Real
#undef R_Int
#undef R_None
}

} // namespace plang
