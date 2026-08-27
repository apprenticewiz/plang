#include "SetOps.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"

#include "CodegenICE.h"

using namespace plang;

llvm::Value* SetOps::toSetWidth(llvm::Value* v) {
    auto* st = setTy();
    if (v->getType() == st) return v;
    if (v->getType()->isIntegerTy())
        return B.CreateZExtOrTrunc(v, st, "set.cast");
    codegenICE("expected an integer value where a set was required");
}

namespace {
/// True when 0 <= ord < PlangMaxSetElements, plus the ordinal clamped into
/// that window so the caller can shift by it unconditionally.
llvm::Value* clampOrdinal(llvm::IRBuilder<>& b, llvm::Type* i64Ty,
                           llvm::Value* ord, llvm::Value*& inRange) {
    auto* lo = b.CreateICmpSGE(ord, llvm::ConstantInt::get(i64Ty, 0), "set.ge0");
    auto* hi = b.CreateICmpSLT(ord,
        llvm::ConstantInt::get(i64Ty, PlangMaxSetElements), "set.ltmax");
    inRange  = b.CreateAnd(lo, hi, "set.inrange");
    return b.CreateSelect(inRange, ord, llvm::ConstantInt::get(i64Ty, 0), "set.ord");
}
} // namespace

llvm::Value* SetOps::clampToSetWidth(llvm::Value* v) {
    auto* zero = llvm::ConstantInt::get(i64Ty(), 0);
    auto* max  = llvm::ConstantInt::get(i64Ty(), PlangMaxSetElements - 1);
    auto  pick = [&](llvm::Intrinsic::ID id, llvm::Value* a, llvm::Value* b) {
        return B.CreateCall(
            llvm::Intrinsic::getOrInsertDeclaration(&Mod, id, {i64Ty()}), {a, b});
    };
    return pick(llvm::Intrinsic::smin, pick(llvm::Intrinsic::smax, v, zero), max);
}

int64_t SetOps::setBaseOf(const ExprNode& e) {
    const auto& t = e.ResolvedType;
    return (t && t->Kind == TypeKind::Set) ? setOffsetOf(*t) : 0;
}

std::optional<std::pair<int64_t, int64_t>>
SetOps::declaredRangeOf(const ExprNode& e) {
    const auto& t = e.ResolvedType;
    if (!t || t->Kind != TypeKind::Set || !t->ElemType) return std::nullopt;
    return ordinalRange(*t->ElemType);
}

llvm::Value* SetOps::alignSet(llvm::Value* v, int64_t from, int64_t to) {
    if (!v || from == to) return v;
    // A member sits at the bit given by its ordinal less the window's origin,
    // so moving a value from a window based at `from` into one based at `to`
    // moves every bit by the difference between the two origins.
    //
    // ISO §6.4.5 c) makes two set types compatible when their base types are,
    // and two compatible bases need not begin at the same ordinal: `set of
    // -5..10` and `set of 0..10` are compatible and their windows are five bits
    // apart.  Without this the bits were carried across unmoved and every
    // member came out shifted — {1, 3} read back as {-4, -2}.
    auto* st  = setTy();
    auto* amt = llvm::ConstantInt::get(
        st, static_cast<uint64_t>(from > to ? from - to : to - from));
    auto* x = toSetWidth(v);
    return from > to ? B.CreateShl (x, amt, "set.align")
                     : B.CreateLShr(x, amt, "set.align");
}

llvm::Value* SetOps::alignSetArg(llvm::Value* v, const ExprNode& arg,
                                  std::optional<int64_t> destSetBase) {
    // A var parameter arrives as an address, and ISO §6.6.3.3 requires its
    // actual to be of the parameter's own type, so no window is crossed there.
    if (!v || v->getType() != setTy() || !destSetBase) return v;
    return alignSet(v, setBaseOf(arg), *destSetBase);
}

llvm::Value* SetOps::setBitIndex(llvm::Value* ordinal, int64_t base) {
    auto* ord = ToI64(ordinal);
    if (base == 0) return ord;
    return B.CreateSub(ord, llvm::ConstantInt::get(i64Ty(), base, true),
                       "set.rebase");
}

llvm::Value* SetOps::emitSetSingleton(llvm::Value* ordinal, int64_t base,
        std::optional<std::pair<int64_t, int64_t>> declaredRange,
        plang::SourceLocation Loc) {
    // Checked against the set's own declared base type, not merely clamped
    // into [0, PlangMaxSetElements) below: that window is the bitmask's
    // physical width, wider than most base types actually declare, so an
    // ordinal like 999 for a `set of 1..10` cleared clampOrdinal's check
    // and was folded into the bitmask as an unrelated, undeclared member.
    if (declaredRange)
        RangeGuards.emitRangeCheck(ordinal, declaredRange->first,
                                    declaredRange->second, /*isIndex=*/false, Loc);
    auto* st    = setTy();
    auto* zero  = llvm::ConstantInt::get(st, 0);
    llvm::Value* inRange = nullptr;
    auto* ord   = clampOrdinal(B, i64Ty(), setBitIndex(ordinal, base), inRange);
    auto* bit   = B.CreateShl(llvm::ConstantInt::get(st, 1),
                              B.CreateZExt(ord, st), "set.bit");
    return B.CreateSelect(inRange, bit, zero, "set.single");
}

llvm::Value* SetOps::emitSetRange(llvm::Value* lo, llvm::Value* hi, int64_t base,
        std::optional<std::pair<int64_t, int64_t>> declaredRange,
        plang::SourceLocation Loc) {
    // As emitSetSingleton: each endpoint is checked against the declared base
    // type before it is ever turned into a bit position, regardless of
    // whether lo > hi leaves the range empty -- an endpoint outside the base
    // type is still not a value of that type.
    if (declaredRange) {
        RangeGuards.emitRangeCheck(lo, declaredRange->first, declaredRange->second,
                                    /*isIndex=*/false, Loc);
        RangeGuards.emitRangeCheck(hi, declaredRange->first, declaredRange->second,
                                    /*isIndex=*/false, Loc);
    }
    auto* st       = setTy();
    auto* zero     = llvm::ConstantInt::get(st, 0);
    auto* allOnes  = llvm::ConstantInt::getAllOnesValue(st);
    auto* i64Zero  = llvm::ConstantInt::get(i64Ty(), 0);
    auto* i64Max   = llvm::ConstantInt::get(i64Ty(), PlangMaxSetElements - 1);

    // Emptiness is decided from the original bounds; the clamped copies below
    // only keep the shift amounts inside the type width, since a shift past it
    // is poison in LLVM even on a path whose result is discarded.
    auto* l = setBitIndex(lo, base);
    auto* h = setBitIndex(hi, base);
    auto* empty = B.CreateOr(
        B.CreateICmpSGT(l, h, "set.lo.gt.hi"),
        B.CreateOr(B.CreateICmpSGT(l, i64Max, "set.lo.big"),
                   B.CreateICmpSLT(h, i64Zero, "set.hi.neg")),
        "set.empty");
    auto* lClamped = clampToSetWidth(l);
    auto* hClamped = clampToSetWidth(h);

    // bits >= lo
    auto* lowBits = B.CreateShl(allOnes,
        B.CreateZExt(lClamped, st), "set.lowbits");

    // bits > hi, i.e. allOnes << (hi + 1).  hi == max would shift by the full
    // width, so substitute a shift of 0 and select the result away.
    auto* hiIsMax = B.CreateICmpEQ(hClamped, i64Max, "set.hi.ismax");
    auto* shAmt   = B.CreateSelect(hiIsMax, i64Zero,
        B.CreateAdd(hClamped, llvm::ConstantInt::get(i64Ty(), 1)), "set.hi.sh");
    auto* above   = B.CreateSelect(hiIsMax, zero,
        B.CreateShl(allOnes, B.CreateZExt(shAmt, st)), "set.above");

    auto* mask = B.CreateAnd(lowBits, B.CreateNot(above), "set.range");
    return B.CreateSelect(empty, zero, mask, "set.range.chk");
}

llvm::Value* SetOps::emitSetMember(llvm::Value* ordinal, llvm::Value* set, int64_t base) {
    auto* st = setTy();
    llvm::Value* inRange = nullptr;
    auto* ord   = clampOrdinal(B, i64Ty(), setBitIndex(ordinal, base), inRange);
    auto* shifted = B.CreateLShr(toSetWidth(set),
                                 B.CreateZExt(ord, st), "set.shr");
    auto* bit = B.CreateTrunc(shifted, llvm::Type::getInt1Ty(Ctx), "set.bit");
    return B.CreateAnd(inRange, bit, "set.in");
}

llvm::Value* SetOps::emitSetBinary(TokenKind op, llvm::Value* a, llvm::Value* b) {
    auto* x = toSetWidth(a);
    auto* y = toSetWidth(b);
    switch (op) {
        case TokenKind::Plus:     return B.CreateOr(x, y, "set.union");
        case TokenKind::Times:    return B.CreateAnd(x, y, "set.isect");
        case TokenKind::Minus:
            return B.CreateAnd(x, B.CreateNot(y), "set.diff");
        case TokenKind::SymDiff:  return B.CreateXor(x, y, "set.symdiff");
        case TokenKind::Equal:    return B.CreateICmpEQ(x, y, "set.eq");
        case TokenKind::NotEqual: return B.CreateICmpNE(x, y, "set.ne");
        // a <= b holds when a contributes no bits outside b.
        case TokenKind::LessThanOrEqual:
            return B.CreateICmpEQ(B.CreateAnd(x, y), x, "set.subset");
        case TokenKind::GreaterThanOrEqual:
            return B.CreateICmpEQ(B.CreateAnd(x, y), y, "set.supset");
        default: return nullptr;
    }
}
