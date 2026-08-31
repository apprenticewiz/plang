// OrdinalSignedness.h — whether an ordinal type/expression's values are
// signed in their LLVM representation.
//
// Fully stateless: every dependency is an explicit parameter, nothing here
// touches Codegen::Impl.  See ConstFold.h's identical top-of-file note for
// why that shape was chosen there; the same reasoning applies here.
#pragma once

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

namespace plang {
struct Type;
struct ExprNode;
}

/// Whether an ordinal type's values are unsigned in their LLVM
/// representation.  ISO §6.4.2.2 orders every ordinal by its ordinal
/// number, which is never negative for Boolean/Char/Enum; a signed compare
/// would read boolean 'true' (i1 1) as -1 and the upper half of the char
/// set as negative.  Turbo's unsigned sized-integer rungs (Byte, Word,
/// Cardinal, LongWord, QWord) need the identical treatment for the
/// identical reason -- Word's 60000 read as a signed i16 is a large
/// negative number.
///
/// Consults Type::IsSigned rather than re-deriving the answer from Kind:
/// IsSigned is now correctly false on every ordinal it needs to be (Type::
/// makeBoolean/makeChar and the Enum construction site in SemaType.cpp all
/// set it explicitly; TypeContext::getInt sets it from the ladder's own
/// Bits/Signed key), so one flag now answers for all of them, including
/// Kind::Integer rungs a Kind-only dispatch could never have covered.
///
/// Reads \p t's OWN IsSigned directly, with NO Subrange-to-SubBase peeling:
/// TypeContext::getSubrange (the only place that ever mints a Subrange-kind
/// Type) always sets the subrange's own Width/IsSigned explicitly, in every
/// one of its branches -- from TP7 ch.19's narrowestStorage over the
/// subrange's OWN bounds under Turbo, not from its host/SubBase's width at
/// all.  A prior version of this function peeled past the subrange to read
/// SubBase->IsSigned instead (equivalent, back when a subrange's IsSigned
/// was always just its SubBase's, copied at construction -- still true of
/// ISO 7185/EP's own getSubrange branch today).  Once Turbo's narrowest
/// -Storage branch could pick an IsSigned DIFFERENT from the host's own
/// (e.g. `0..40000`'s bounds are both typed as plain 16-bit signed Integer
/// literals, i.e. SubBase is signed, but 40000 does not fit signed 16-bit
/// at all, so narrowestStorage instead picks {16, unsigned} -- Word-shaped
/// -- for the subrange itself), the peel started reading the WRONG flag:
/// confirmed empirically (`b: 0..40000; b := 40000; writeln(b + 0)`, under
/// -std=turbo, printed -25536, sign-extending Word's own all-ones top bit
/// instead of zero-extending it, where `fpc -Mtp` prints 40000) through
/// CGBinaryOps' own operandIsSigned -- which was, and remains, structurally
/// correct: it has always consulted this exact function for its answer, so
/// the actual defect was here, not in any of ITS call sites.
[[nodiscard]] bool ordinalIsUnsigned(const plang::Type* t);

/// The expression's actual Sema-resolved signedness (Type::IsSigned) --
/// consulted, not guessed from LLVM bit width, at every ToI64/CoerceToType
/// call site that has a real Sema-typed operand in hand.  Equivalent to (and
/// the hoisted, single home for what used to be) CGBinaryOps::
/// operandIsSigned's own one-line body -- moved here once CGAssign,
/// CGProcCall, StringCallMarshalling, and the other codegen units audited
/// alongside issue #177 needed the identical answer too, rather than each
/// re-deriving it (or, worse, re-implementing the guess-from-LLVM-width
/// fallback toI64/coerceToType fall back to when no caller supplies this).
[[nodiscard]] bool exprIsSigned(const plang::ExprNode& e);

/// Widens an ordinal LLVM value \p v to i64, honoring \p srcSigned (an
/// exprIsSigned answer for whatever Sema expression \p v came from) rather
/// than guessing sign- or zero-extension from \p v's own bit width.  A
/// small, Impl-independent sibling of Codegen::Impl::toI64's integer path
/// for codegen units that have no other need of Impl's own ToI64/
/// CoerceToType bridges (issue #177's sibling audit) -- its floating-point
/// branch is never needed here, since every call site this serves already
/// has an ordinal (isOrdinal()/isIntegral(), never a float) by the time it
/// reaches here, the same precondition toI64's own float check exists to
/// cover for its callers.
[[nodiscard]] inline llvm::Value* widenOrdinalToI64(
        llvm::IRBuilder<>& B, llvm::Value* v, llvm::IntegerType* i64Ty,
        bool srcSigned) {
    if (v->getType() == i64Ty) return v;
    return srcSigned ? B.CreateSExt(v, i64Ty, "to.i64")
                      : B.CreateZExt(v, i64Ty, "to.i64");
}
