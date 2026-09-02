#include "CodeGenImpl.h"
using namespace plang;

// See NumExprKinds in AstBase.h.
static_assert(NumExprKinds == 20, "a new expression needs a case in emitExpr");

// ====================================================================
// Type coercion helpers
// ====================================================================

llvm::Value* Codegen::Impl::ensureI1(llvm::Value* v) {
    if (!v) codegenICE("boolean conversion of an unlowerable expression");
    if (v->getType()->isIntegerTy(1)) return v;
    // A nonzero-test, not a truncation of the low bit: every call site here
    // is a Boolean-kind value however it is stored (ISO/EP strict Boolean is
    // always i1 and never reaches this branch at all), and Turbo's loose
    // ByteBool/WordBool/LongBool (Type::IsLooseBool) can legally hold any
    // bit pattern -- `ByteBool(200)` is stored as the literal byte 200, not
    // truncated to 0/1 -- so "is this value true" has to look at the WHOLE
    // value, not just its low bit.  This used to CreateTrunc, which read
    // 200 (0b11001000, low bit 0) as false: exactly backwards.  Every
    // existing caller before ByteBool/WordBool/LongBool existed only ever
    // passed a value already guaranteed to be exactly 0 or 1, for which a
    // nonzero test and a low-bit truncation agree, so this is not a
    // behavior change for any of them.
    return builder.CreateICmpNE(
        v, llvm::ConstantInt::get(v->getType(), 0), "to.i1");
}

llvm::Value* Codegen::Impl::toDouble(llvm::Value* v) {
    if (!v) codegenICE("real conversion of an unlowerable expression");
    if (v->getType()->isDoubleTy()) return v;
    // Turbo `Single` (float) widens like any other Real, not like an
    // integer -- CreateSIToFP on an already-floating value is a verifier
    // abort, not a silently wrong answer, so the floating case has to be
    // told apart from the integer one before picking the conversion.
    if (v->getType()->isFloatingPointTy())
        return builder.CreateFPExt(v, dblTy, "to.dbl");
    return builder.CreateSIToFP(v, dblTy, "to.dbl");
}

llvm::Value* Codegen::Impl::toI64(llvm::Value* v, std::optional<bool> srcSigned) {
    if (!v) codegenICE("integer conversion of an unlowerable expression");
    if (v->getType()->isIntegerTy(64)) return v;
    // isFloatingPointTy, not isDoubleTy: Turbo's Single (float) needs the
    // identical FPToSI conversion double already got, just at its own
    // width -- CreateFPToSI accepts either source width directly.
    if (v->getType()->isFloatingPointTy())
        return builder.CreateFPToSI(v, i64Ty, "to.i64");
    // srcSigned given: consult the operand's actual Sema-resolved
    // Type::IsSigned directly rather than guessing from LLVM width -- see
    // the declaration's (CodeGenImpl.h) comment for why the guess below is
    // wrong for Turbo's sized-integer ladder's unsigned rungs wider than i8
    // (Word/Cardinal/LongWord/QWord) and its signed 8-bit rung (ShortInt).
    if (srcSigned.has_value())
        return *srcSigned ? builder.CreateSExt(v, i64Ty, "to.i64")
                           : builder.CreateZExt(v, i64Ty, "to.i64");
    // No operand-type context at this call site (srcSigned omitted): fall
    // back to the pre-ladder heuristic.  Char (i8) and Boolean (i1) are the
    // only narrow ordinals that are genuinely non-negative; everything else
    // narrower than i64 that reaches here today is Turbo's own Integer,
    // which is signed (16-bit, IsSigned true -- see LangOptions::
    // defaultIntWidth()).  Zero-extending it here silently turned a negative
    // i16 into a huge positive i64 (this used to do exactly that,
    // unconditionally, before Turbo's Integer existed to ever be negative at
    // a narrower-than-64 width).
    if (v->getType()->isIntegerTy(8) || v->getType()->isIntegerTy(1))
        return builder.CreateZExt(v, i64Ty, "to.i64");
    return builder.CreateSExt(v, i64Ty, "to.i64");
}

llvm::Value* Codegen::Impl::coerceToType(llvm::Value* v, llvm::Type* dst,
                                          std::optional<bool> srcSigned) {
    if (!v || v->getType() == dst) return v;
    // Widening/narrowing between an ordinal and a floating destination: dst
    // (or v) may now be float (Turbo's Single) as well as double (Real), so
    // this targets/reads whichever floating type is actually in play rather
    // than assuming double on both sides.
    if (dst->isFloatingPointTy() && v->getType()->isIntegerTy())
        return builder.CreateSIToFP(v, dst, "widen");
    if (dst->isIntegerTy() && v->getType()->isFloatingPointTy())
        return builder.CreateFPToSI(v, dst, "narrow");
    // Real <-> Single width mismatch, either direction -- see the identical
    // pair in emitAssign (CGAssign.cpp) for why both directions arise.
    if (dst->isDoubleTy() && v->getType()->isFloatTy())
        return builder.CreateFPExt(v, dst, "widen.fp");
    if (dst->isFloatTy() && v->getType()->isDoubleTy())
        return builder.CreateFPTrunc(v, dst, "narrow.fp");
    // Ordinals of different widths meet whenever a char or boolean is stored
    // where an integer was computed, the reverse, or (under Turbo) two
    // sized-integer ladder rungs of different widths meet.  A narrowing
    // truncation is exact regardless of signedness, hence *OrTrunc either
    // way; only an actual WIDENING needs to get sign- vs. zero-extension
    // right, which is exactly what srcSigned (see toI64's identical
    // parameter, just above) is for.
    if (dst->isIntegerTy() && v->getType()->isIntegerTy()) {
        const bool srcNonNegative = srcSigned.has_value()
            ? !*srcSigned
            // No operand-type context given: the pre-ladder heuristic --
            // see toI64's identical fallback for why i8/i1 alone used to be
            // the whole answer and no longer is in general.
            : (v->getType()->isIntegerTy(8) || v->getType()->isIntegerTy(1));
        return srcNonNegative ? builder.CreateZExtOrTrunc(v, dst, "conv")
                               : builder.CreateSExtOrTrunc(v, dst, "conv");
    }
    return v;
}

// ====================================================================
// EP §6.8.7: Structured value constructor emission
// ====================================================================

// EP §6.4.1: the denoter a value is a value of, with any names it is written
// through followed to the declaration that gives its shape.  Denotes first,
// same reasoning as llvmTypeOfNode's own NamedTypeNode case and
// initialStateShapeOf's identical hop loop: it is what Sema resolved this
// name to where it was WRITTEN, not a flat table rebuilt per procedure.
// typeAliases is a fallback for the node it cannot reach, not the first
// answer to ask.
const TypeNode* Codegen::Impl::denoterOf(const TypeNode* tn) const {
    for (int hops = 0; tn && hops < 32; ++hops) {
        auto* named = llvm::dyn_cast<NamedTypeNode>(tn);
        if (!named) return tn;
        const TypeNode* next = named->Denotes;
        if (!next) {
            auto it = typeAliases.find(toLower(named->Name));
            if (it == typeAliases.end()) return tn;
            next = it->second;
        }
        if (next == tn) return tn;
        tn = next;
    }
    return tn;
}
