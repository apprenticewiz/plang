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
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result, ArgKind) Id,
#include "plang/Basic/Builtins.def"
};

/// How many there are, not counting None.  The switches below are over the
/// enumeration itself, so the compiler already makes them cover a new entry;
/// this is for a caller that walks the list and has to show it walked all of
/// it, which a loop written over a .def cannot otherwise demonstrate.
inline constexpr std::size_t NumBuiltins = 0
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result, ArgKind) + 1
#include "plang/Basic/Builtins.def"
    ;

/// What a builtin is called in the source, folded to lower case.
[[nodiscard]] constexpr std::string_view builtinSpelling(BuiltinID ID) {
    switch (ID) {
    case BuiltinID::None: return "";
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result, ArgKind) \
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
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result, ArgKind) \
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
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result, ArgKind) \
    case BuiltinID::Id: return {MinArgs, MaxArgs};
#include "plang/Basic/Builtins.def"
    }
    return {0, -1};
}

[[nodiscard]] constexpr bool builtinIsFunction(BuiltinID ID) {
    switch (ID) {
    case BuiltinID::None: return false;
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result, ArgKind) \
    case BuiltinID::Id: return std::string_view(#Kind) == "Func";
#include "plang/Basic/Builtins.def"
    }
    return false;
}

/// What every argument of a builtin call must be, enforced generically by
/// Sema::checkBuiltinArgKinds right after the arity check -- see Builtins.def's
/// own header comment for the full vocabulary and why each tag exists.  Any
/// is the default: it means "no generic check", not "no arguments" -- a
/// builtin not yet migrated to this column keeps whatever hand-written check
/// (or lack of one) it already had in checkCallExpr/checkCallStmt.
enum class BuiltinArgKind { Any, Numeric, NumericNonComplex, Complex };

[[nodiscard]] constexpr BuiltinArgKind builtinArgKind(BuiltinID ID) {
#define AK_Any               BuiltinArgKind::Any
#define AK_Numeric            BuiltinArgKind::Numeric
#define AK_NumericNonComplex  BuiltinArgKind::NumericNonComplex
#define AK_Complex            BuiltinArgKind::Complex
    switch (ID) {
    case BuiltinID::None: return BuiltinArgKind::Any;
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result, ArgKind) \
    case BuiltinID::Id: return (ArgKind);
#include "plang/Basic/Builtins.def"
    }
    return BuiltinArgKind::Any;
#undef AK_Complex
#undef AK_NumericNonComplex
#undef AK_Numeric
#undef AK_Any
}

/// Whether a call to this builtin unconditionally leaves the statement
/// sequence it appears in, the same way a goto does: Halt and RunError end
/// the program, Exit ends the function or procedure it is written in, and
/// Break/Continue end the loop iteration they are written in.  Nothing that
/// textually follows one, in the sequence it is in, is a path the two
/// callers below need to consider live.
///
/// Not one of Builtins.def's own columns, unlike the queries above: it is
/// not a property every entry has an opinion on, and a builtin added later
/// defaults to false here (does not transfer) rather than needing a row
/// added to a table it would otherwise silently fall out of -- the same
/// one-way bias plang/Sema/SemaFlow.cpp's own header comment explains for
/// definite assignment: missing a warning costs a warning, and a warning
/// about correct code costs the reader's trust in all the others.
///
/// Two callers: SemaStmt.cpp's alwaysTransfers (warn_unreachable_code) and
/// SemaFlow.cpp's flowStmt (definite assignment across Break/Continue/Exit/
/// Halt/RunError), which is also why this lives beside BuiltinID rather than
/// in either Sema*.cpp: giving both the same one-line answer instead of two
/// hand-kept lists is the whole point.
[[nodiscard]] constexpr bool builtinAlwaysTransfers(BuiltinID ID) {
    switch (ID) {
    case BuiltinID::Halt:
    case BuiltinID::Exit:
    case BuiltinID::Break:
    case BuiltinID::Continue:
    case BuiltinID::RunError:
        return true;
    default:
        return false;
    }
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
#define BUILTIN(Id, Spelling, Kind, Dialects, MinArgs, MaxArgs, Result, ArgKind) \
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
