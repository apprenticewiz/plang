// SetOps.h — ISO §6.7.2.4 sets: a flat bitmask, one bit per ordinal of the
// base type. Every operation here is emitted inline: the bitwise ones map
// directly to LLVM instructions, which avoids having to define a calling
// convention for a 256-bit value crossing into the C runtime.
//
// Membership and construction clamp their ordinal so an out-of-range value
// yields the empty set or false rather than a shift past the type width,
// which LLVM treats as poison.
#pragma once

#include <functional>
#include <optional>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/SourceLocation.h"
#include "plang/Basic/Token.h"
#include "plang/Sema/Type.h"

#include "RangeCheckGuards.h"

namespace plang { struct ExprNode; }

class SetOps {
public:
    SetOps(llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
           RangeCheckGuards& RangeGuards,
           std::function<llvm::Value*(llvm::Value*)> ToI64)
        : Ctx(Ctx), Mod(Mod), B(B), RangeGuards(RangeGuards), ToI64(std::move(ToI64)) {}

    /// Sets are a flat bitmask of PlangMaxSetElements bits.  Bit 0 stands for
    /// the base type's origin rather than for ordinal 0, so a base type
    /// reaching below zero still fits.  Sema rejects base types that span
    /// more ordinals than there are bits.
    llvm::IntegerType* setTy() const {
        return llvm::Type::getIntNTy(Ctx, plang::PlangMaxSetElements);
    }
    /// Widens/narrows an integer to the set width.  Sets never flow through
    /// ToI64, which would discard every ordinal above 63.
    llvm::Value* toSetWidth(llvm::Value* v);
    /// Clamps v into [0, PlangMaxSetElements - 1].
    llvm::Value* clampToSetWidth(llvm::Value* v);
    /// The ordinal that bit 0 of e's set type stands for; 0 when e has no set
    /// type, which is the layout every non-negative base type uses anyway.
    int64_t setBaseOf(const plang::ExprNode& e);
    /// e's set type's own base type's ordinal range -- e.g. {1, 10} for a
    /// `set of 1..10` -- or nothing when e has no set type or that base type
    /// has no bounded range (plain `integer`). Passed to emitSetSingleton/
    /// emitSetRange so a member can be checked against the range the set's
    /// declared TYPE promises, not merely against PlangMaxSetElements, the
    /// representation's own width.
    std::optional<std::pair<int64_t, int64_t>>
    declaredRangeOf(const plang::ExprNode& e);
    /// Moves a set value from the window based at `from` into the one based
    /// at `to`.  Two compatible set types may be based at different
    /// ordinals, and a value crossing between them has to be shifted to
    /// keep its members.
    llvm::Value* alignSet(llvm::Value* v, int64_t from, int64_t to);
    /// alignSet for an argument being passed by value, whose destination
    /// window is \p destSetBase (the callee parameter's recorded set base,
    /// already resolved by the caller -- this method has no knowledge of
    /// paramMeta_/mangled-name lookups). Leaves anything that is not a set
    /// value alone, a var parameter's address included.
    llvm::Value* alignSetArg(llvm::Value* v, const plang::ExprNode& arg,
                              std::optional<int64_t> destSetBase);
    /// Bit index for an ordinal in a set based at `base`.  \p ordinalSigned,
    /// when given, is the ordinal's own Sema-resolved signedness (an
    /// exprIsSigned answer, OrdinalSignedness.h) -- consulted instead of the
    /// shared ToI64 bridge's own guess-from-LLVM-width fallback whenever
    /// \p ordinal is not already i64.  Defaults to nullopt, preserving every
    /// pre-existing caller's exact behavior; a set whose base type reaches
    /// below zero (setBaseOf's own reason to exist) can carry a genuinely
    /// negative member ordinal, which -- like every other ToI64 call site
    /// issue #177's sibling audit covers -- needs this explicitly rather
    /// than guessed (e.g. a `set of ShortInt` member, or the set-typed `in`
    /// operator's left operand).
    llvm::Value* setBitIndex(llvm::Value* ordinal, int64_t base,
                              std::optional<bool> ordinalSigned = std::nullopt);
    /// \p declaredRange, when given, is checked against \p ordinal (or, for
    /// emitSetRange, against both \p lo and \p hi) with a RangeCheckGuards
    /// trap at \p Loc before the value is folded into the bitmask -- ISO
    /// §6.4.6/§6.7.2.4: a set member has to lie in the set's base type, and
    /// PlangMaxSetElements alone (what clampOrdinal/emitSetRange's own
    /// emptiness test enforce) is the representation's width, not that.
    /// \p ordinalSigned (\p loSigned/\p hiSigned for emitSetRange): as
    /// setBitIndex's own parameter above.
    llvm::Value* emitSetSingleton(llvm::Value* ordinal, int64_t base,
        std::optional<std::pair<int64_t, int64_t>> declaredRange = std::nullopt,
        plang::SourceLocation Loc = {},
        std::optional<bool> ordinalSigned = std::nullopt);
    llvm::Value* emitSetRange(llvm::Value* lo, llvm::Value* hi, int64_t base,
        std::optional<std::pair<int64_t, int64_t>> declaredRange = std::nullopt,
        plang::SourceLocation Loc = {},
        std::optional<bool> loSigned = std::nullopt,
        std::optional<bool> hiSigned = std::nullopt);
    llvm::Value* emitSetMember(llvm::Value* ordinal, llvm::Value* set, int64_t base,
                                std::optional<bool> ordinalSigned = std::nullopt);
    /// Lowers a set-valued or set-comparing binary operator; returns null if
    /// op is not one of them.
    llvm::Value* emitSetBinary(plang::TokenKind op, llvm::Value* a, llvm::Value* b);

private:
    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    RangeCheckGuards& RangeGuards;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    llvm::IntegerType* i64Ty() const { return llvm::Type::getInt64Ty(Ctx); }
};
