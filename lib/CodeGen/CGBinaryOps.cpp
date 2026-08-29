#include "CGBinaryOps.h"

#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/Token.h"

#include "CodegenICE.h"

using namespace plang;

bool CGBinaryOps::exprIsStringLike(const ExprNode& e) {
    return ExprIsVarStr(e) || ExprIsCharStr(e)
        || (e.ResolvedType && e.ResolvedType->Kind == TypeKind::String);
}

bool CGBinaryOps::exprIsSet(const ExprNode& e) {
    return e.ResolvedType && e.ResolvedType->Kind == TypeKind::Set;
}

llvm::Value* CGBinaryOps::emitShortCircuit(const BinaryExpr& e, bool isAnd) {
    auto* lv      = EnsureI1(EmitExpr(*e.Left));
    auto* rhsBB   = llvm::BasicBlock::Create(Ctx, isAnd ? "sc.and.rhs"  : "sc.or.rhs",  CurFn);
    auto* endBB   = llvm::BasicBlock::Create(Ctx, isAnd ? "sc.and.end"  : "sc.or.end",  CurFn);
    auto* shortBB = llvm::BasicBlock::Create(Ctx, isAnd ? "sc.and.skip" : "sc.or.skip", CurFn);
    // and-shaped: if left is false, skip right; or-shaped: if left is true, skip right.
    B.CreateCondBr(lv, isAnd ? rhsBB : shortBB,
                             isAnd ? shortBB : rhsBB);
    B.SetInsertPoint(rhsBB);
    auto* rv = EnsureI1(EmitExpr(*e.Right));
    B.CreateBr(endBB);
    // Re-fetched rather than reusing rhsBB: EmitExpr's right-operand call
    // just above may itself have split blocks (e.g. a nested and_then, or a
    // nested plain and/or that also takes this short-circuit path), which
    // would leave the builder somewhere other than rhsBB by the time control
    // reaches here.  See emitExpr's own documented invariant (CGExprCore.h).
    auto* fromRhs = B.GetInsertBlock();
    B.SetInsertPoint(shortBB);
    B.CreateBr(endBB);
    B.SetInsertPoint(endBB);
    auto* phi = B.CreatePHI(I1Ty, 2, isAnd ? "sc.and" : "sc.or");
    // and-shaped shortcut value: false; or-shaped shortcut value: true.
    phi->addIncoming(llvm::ConstantInt::get(I1Ty, isAnd ? 0 : 1), shortBB);
    phi->addIncoming(rv, fromRhs);
    return phi;
}

llvm::Value* CGBinaryOps::emitBinary(const BinaryExpr& e) {
    // Boolean and/or: ISO §6.7.2.1 requires BOTH operands evaluated, always
    // -- no dialect-specific case here changes that for ISO 7185 or Extended
    // Pascal.  Turbo Pascal's `{$B-}` (the default) short-circuits instead,
    // the same shape EP's and_then/or_else always uses; `{$B+}` restores
    // full evaluation from that point in the source forward.
    //
    // Bitwise and/or on Integer operands: operand type checked FIRST, short-
    // circuit dispatch second -- which is exactly the shape below.
    // Short-circuiting only makes sense for a Boolean operand (EnsureI1
    // below assumes an i1-shaped value), so bothBoolean is checked before
    // any switch is even asked.  Sema::checkBinary has already refused every
    // operand pairing except "both Boolean" or, under Turbo, "both
    // Integer" -- so !bothBoolean here means both operands ARE Integer, and
    // falling through (no return) below sends them to the generic integer
    // path further down, the same one Plus/Minus/Times already share: it
    // does the width-unification a bitwise op needs the identical way an
    // arithmetic one does, and its own switch has the And/Or bitwise cases.
    if (e.Op == TokenKind::And || e.Op == TokenKind::Or) {
        const bool bothBoolean =
            e.Left->ResolvedType  && e.Left->ResolvedType->Kind  == TypeKind::Boolean &&
            e.Right->ResolvedType && e.Right->ResolvedType->Kind == TypeKind::Boolean;
        if (bothBoolean) {
            const bool isAnd = e.Op == TokenKind::And;
            // RangeGuards.boolEvalAt alone is not the whole answer -- see its own
            // comment (RangeCheckGuards.h) for why isTurbo() has to gate it:
            // SwitchTable's default answers "short-circuit" for every dialect,
            // ISO 7185 and Extended Pascal included, and those two have no
            // `{$B}` directive to ever say otherwise.
            if (RangeGuards.isTurbo() && !RangeGuards.boolEvalAt(e.Loc))
                return emitShortCircuit(e, isAnd);
            auto* l = EnsureI1(EmitExpr(*e.Left));
            auto* r = EnsureI1(EmitExpr(*e.Right));
            return isAnd ? B.CreateAnd(l, r, "and") : B.CreateOr(l, r, "or");
        }
        // else: both Integer (Turbo bitwise and/or) -- fall through.
    }

    // EP §6.8.3.3: and_then / or_else always short-circuit, regardless of
    // any switch -- EP has no `{$B}` directive at all.
    if (e.Op == TokenKind::AndThen || e.Op == TokenKind::OrElse)
        return emitShortCircuit(e, /*isAnd=*/e.Op == TokenKind::AndThen);

    // EP exponentiation: ** and pow  → std::pow (result is real or complex)
    if (e.Op == TokenKind::StarStar || e.Op == TokenKind::Pow) {
        // Check for complex operands via Sema-annotated types.
        bool lCplx = e.Left->ResolvedType
                     && e.Left->ResolvedType->Kind == TypeKind::Complex;
        bool rCplx = e.Right->ResolvedType
                     && e.Right->ResolvedType->Kind == TypeKind::Complex;
        if (lCplx || rCplx) {
            auto* lv = EmitExpr(*e.Left);
            auto* rv = EmitExpr(*e.Right);
            auto* lc = Complex.coerceToComplex(lv);
            auto* rc = Complex.coerceToComplex(rv);
            // EP §6.8.3.2's "an error if x is zero and y is less than or
            // equal to zero" is shared with the real path above (guarded
            // there via plang_err_pow_domain), but this complex path
            // skipped it entirely: cmplx(0,0) ** cmplx(-1,0) silently rode
            // std::pow down to Inf/NaN instead of trapping.  Complex values
            // have no "negative base" case to add (they aren't ordered), so
            // only the zero-base check applies, keyed off the exponent's
            // real part -- the same shape as the zero-zero case already
            // caught for a real/integer base.
            auto* zero  = llvm::ConstantFP::get(DblTy, 0.0);
            auto* are   = B.CreateExtractValue(lc, 0, "cpow.a.re");
            auto* aim   = B.CreateExtractValue(lc, 1, "cpow.a.im");
            auto* bre   = B.CreateExtractValue(rc, 0, "cpow.b.re");
            auto* bim   = B.CreateExtractValue(rc, 1, "cpow.b.im");
            auto* aZero = B.CreateAnd(B.CreateFCmpOEQ(are, zero, "cpow.a.re.eq0"),
                                       B.CreateFCmpOEQ(aim, zero, "cpow.a.im.eq0"),
                                       "cpow.a.eq0");
            auto* bad   = B.CreateAnd(aZero,
                                       B.CreateFCmpOLE(bre, zero, "cpow.b.re.le0"),
                                       "cpow.bad");
            RangeGuards.emitGuard(bad, "cpow.domain", [&] {
                B.CreateCall(
                    RtFns.getExternFnN("plang_err_cpow_domain", llvm::Type::getVoidTy(Ctx),
                                 {DblTy, DblTy, DblTy, DblTy}),
                    {are, aim, bre, bim});
            });
            return Complex.emitComplexPow(lc, rc);
        }
        // EP §6.8.3.2: an integer base with pow keeps an integer result, so it
        // must not make the round trip through double that '**' does.
        if (e.ResolvedType && e.ResolvedType->Kind == TypeKind::Integer) {
            auto* fn = RtFns.getExternFnN("plang_ipow", I64Ty, {I64Ty, I64Ty});
            return B.CreateCall(
                fn, {ToI64(EmitExpr(*e.Left)), ToI64(EmitExpr(*e.Right))}, "ipow");
        }
        auto* powFn = RtFns.getExternFnN("pow", DblTy, {DblTy, DblTy});
        auto* base  = ToDouble(EmitExpr(*e.Left));
        auto* exp   = ToDouble(EmitExpr(*e.Right));
        // EP §6.8.3.2: "a factor of the form x**y shall be an error if x is
        // zero and y is less than or equal to zero" -- shared with `pow`,
        // whose integer path checks it above (plang_ipow) but whose OWN
        // real-base path did not; and, for `**` only (a real or integer, so
        // non-complex, base), "an error if x is negative" -- x**y is
        // exp(y*ln(x)), and ln has no real value at or below zero.  Neither
        // was checked, so std::pow's own C99 conventions answered instead:
        // 0.0**0.0 is 1 by its any**0 rule, (-2.0)**2.0 by its extension for
        // an integral exponent.
        auto* zero    = llvm::ConstantFP::get(DblTy, 0.0);
        auto* baseLT0 = B.CreateFCmpOLT(base, zero, "pow.base.lt0");
        auto* expLE0  = B.CreateFCmpOLE(exp,  zero, "pow.exp.le0");
        auto* zeroZero = B.CreateAnd(
            B.CreateFCmpOEQ(base, zero, "pow.base.eq0"), expLE0, "pow.00");
        // `**`: base < 0 (any exponent) OR base = 0 with exponent <= 0.
        // `pow`'s real-base path (a real or complex-mixed base -- the
        // integer*integer case took the ipow branch above): base = 0 with
        // exponent <= 0 only, since pow's recursive definition is well-formed
        // for a negative base.
        auto* bad = e.Op == TokenKind::StarStar
            ? B.CreateOr(baseLT0, zeroZero, "pow.bad")
            : zeroZero;
        RangeGuards.emitGuard(bad, "pow.domain", [&] {
            B.CreateCall(
                RtFns.getExternFnN("plang_err_pow_domain", llvm::Type::getVoidTy(Ctx),
                             {DblTy, DblTy}),
                {base, exp});
        });
        return B.CreateCall(powFn, {base, exp}, "pow");
    }

    // EP §6.8.3.6: string concatenation  a + b
    // Use Sema-annotated type to detect operands and read capacities.
    if (e.Op == TokenKind::Plus && ExprIsVarStr(e)) {
        // EP §6.8.3.2 makes a char operand string-compatible, so either side of
        // the concatenation may be one.  The runtime concatenates onto a string,
        // so a char on the left has to become a one-character string first.
        // The RESULT is a temporary and has to be sized by a constant, so a
        // discriminant-fixed operand contributes the widest capacity plang has
        // rather than the probe's one character.  What each operand is declared
        // to hold is told to the runtime separately, as a value.
        // ISO §6.4.3.3.1: "each string-type value is a value of the
        // canonical-string-type" -- a fixed-string-type operand (ISO
        // §6.4.3.2's packed array[1..n] of char) is as much a string operand
        // of `+` as a char or an EP string(n), and was missing here the same
        // way it was missing from length/substr/trim/index: treated as a
        // bare one-character operand, which truncated `charArr + b` to the
        // array's first character.
        auto strOperand = [&](const ExprNode& x) -> std::pair<llvm::Value*, llvm::Value*> {
            if (ExprIsVarStr(x)) return Schema.strAddrAndCap(x);
            if (ExprIsCharStr(x))
                return {StrCall.emitCharStrAsStr(x), i64c(ExprCharStrLen(x))};
            auto* v   = EmitExpr(x);
            auto* tmp = CreateEntryAlloca(Types.strStructType(1), "str.chr");
            if (v && v->getType()->isIntegerTy(8)) Strings.emitStrFromChar(tmp, i64c(1), v);
            else if (v)                            Strings.emitStrFromCStr(tmp, i64c(1), v);
            return {tmp, i64c(1)};
        };
        auto [lv, capL] = strOperand(*e.Left);
        // R6: the right operand's address and capacity used to be two
        // separate walks of its access path -- exprStrCapV here, then
        // emitStrAddr down where the runtime call is built -- so
        // `q^.a[next].s + 'x'` ran `next` twice over for a right operand
        // alone. strAddrAndCap answers both from the one walk its own fast
        // path already takes (and, unlike emitStrAddr's route through
        // emitLValue, never falls into CGFieldAccess::emitFieldGEP's own
        // extra walk of a varying-extent record field).
        llvm::Value* rv;
        llvm::Value* capR;
        if (ExprIsVarStr(*e.Right)) {
            auto rp = Schema.strAddrAndCap(*e.Right);
            rv = rp.first; capR = rp.second;
        } else if (ExprIsCharStr(*e.Right)) {
            rv   = StrCall.emitCharStrAsStr(*e.Right);
            capR = i64c(ExprCharStrLen(*e.Right));
        } else {
            rv   = EmitExpr(*e.Right);
            capR = i64c(1);
        }
        // A non-string operand is one character, as it was before: returning
        // zero for it sized the result temporary at 1 and cut 'x' + 'y' to "x".
        auto staticCap = [&](const ExprNode& x) -> int64_t {
            if (ExprIsVarStr(x)) return ExprStrCapStatic(x);
            if (ExprIsCharStr(x)) return ExprCharStrLen(x);
            return 1;
        };
        // No clamp: a declared capacity may exceed PlangMaxStringCapacity --
        // string(300) is legal and the corpus has one -- and capping the sum
        // here cut `n := n + 'x'` to 255.  That constant is the answer for a
        // capacity that is not known, not a ceiling on ones that are.
        //
        // And where a discriminant fixes it, nobody knows it at compile time
        // but somebody knows it: capL and capR are the real capacities, as
        // values.  Sizing the result by the static guess instead truncated
        // `q^ := q^ + 'x'` at 256 for a q^ of capacity 300 -- silently, and on
        // a program that is entirely legal.  So the result temporary is sized
        // by the same arithmetic the runtime is told about.
        auto varies = [&](const ExprNode& x) {
            return ExprIsVarStr(x) && x.ResolvedType->ExtentVaries;
        };
        const bool capVaries = varies(*e.Left) || varies(*e.Right);
        const int64_t capRes = staticCap(*e.Left) + staticCap(*e.Right);
        llvm::Value* capResV = capVaries
            ? B.CreateAdd(capL, capR, "concat.cap") : i64c(capRes);
        llvm::Value* resPtr  = capVaries
            ? CreateDynStrAlloca(capResV, "str.concat")
            : static_cast<llvm::Value*>(
                  CreateEntryAlloca(Types.strStructType(capRes), "str.concat"));
        if (ExprIsVarStr(*e.Right) || ExprIsCharStr(*e.Right)) {
            auto* fn = Strings.getStrFn("plang_str_concat", llvm::Type::getVoidTy(Ctx),
                {PtrTy, I64Ty, PtrTy, I64Ty, PtrTy, I64Ty});
            B.CreateCall(fn, {resPtr, capResV, lv, capL, rv, capR});
        } else if (rv && rv->getType()->isIntegerTy(8)) {
            auto* fn = Strings.getStrFn("plang_str_concat_char", llvm::Type::getVoidTy(Ctx),
                {PtrTy, I64Ty, PtrTy, I64Ty, I8Ty});
            B.CreateCall(fn, {resPtr, capResV, lv, capL, rv});
        } else {
            auto* fn = Strings.getStrFn("plang_str_concat_cstr", llvm::Type::getVoidTy(Ctx),
                {PtrTy, I64Ty, PtrTy, I64Ty, PtrTy});
            B.CreateCall(fn, {resPtr, capResV, lv, capL, rv});
        }
        return resPtr;
    }

    // EP §6.8.3.5: string comparison via runtime (lexicographic, space-padded).
    if (e.Op == TokenKind::Equal || e.Op == TokenKind::NotEqual
        || e.Op == TokenKind::LessThan || e.Op == TokenKind::LessThanOrEqual
        || e.Op == TokenKind::GreaterThan || e.Op == TokenKind::GreaterThanOrEqual) {
        // A string-type compares as the string value it is (ISO §6.4.3.2),
        // which the string comparison runtime does once it is shaped like one.
        // As an array it fell through to a pointer comparison, so two equal
        // strings in different variables came out unequal.
        if (exprIsStringLike(*e.Left) || exprIsStringLike(*e.Right)) {
            const char* fnName =
                e.Op == TokenKind::Equal           ? "plang_str_eq" :
                e.Op == TokenKind::NotEqual        ? "plang_str_ne" :
                e.Op == TokenKind::LessThan        ? "plang_str_lt" :
                e.Op == TokenKind::LessThanOrEqual ? "plang_str_le" :
                e.Op == TokenKind::GreaterThan     ? "plang_str_gt" : "plang_str_ge";
            // Convert each operand to a (ptr, cap) pair, wrapping literals in a temp.
            auto toStrPtr = [&](const ExprNode& expr) -> std::pair<llvm::Value*, llvm::Value*> {
                if (ExprIsVarStr(expr)) return Schema.strAddrAndCap(expr);
                if (ExprIsCharStr(expr))
                    return {StrCall.emitCharStrAsStr(expr), i64c(ExprCharStrLen(expr))};
                // String literal or char — wrap in a temporary VarString.
                int64_t cap = 1;
                if (auto* sl = llvm::dyn_cast<StringLitExpr>(&expr))
                    cap = (int64_t)sl->Value.size();
                auto* val = EmitExpr(expr);
                auto* tmp = CreateEntryAlloca(Types.strStructType(cap), "str.cmp.tmp");
                if (val && val->getType()->isIntegerTy(8))
                    Strings.emitStrFromChar(tmp, i64c(cap), val);
                else if (val) {
                    // R6: the capacity above is 1 unless the operand is a
                    // literal whose length we can read.  A char is genuinely
                    // one, and a literal brings its own; anything else reaching
                    // here would be compared at a capacity nobody derived from
                    // its TYPE -- truncated to one character, silently, in a
                    // comparison.  Measured at 0 tests, so it says so instead.
                    if (!llvm::isa<StringLitExpr>(&expr))
                        codegenICE("a string comparison operand whose capacity "
                                   "comes from neither its type nor a literal");
                    Strings.emitStrFromBytes(tmp, i64c(cap), val, i64c(cap));
                }
                return {tmp, i64c(cap)};
            };
            auto [la, capL] = toStrPtr(*e.Left);
            auto [ra, capR] = toStrPtr(*e.Right);
            auto* fn  = Strings.getStrFn(fnName, I8Ty, {PtrTy, I64Ty, PtrTy, I64Ty});
            auto* raw = B.CreateCall(fn, {la, capL, ra, capR}, "str.cmp");
            return EnsureI1(raw);
        }
    }

    // ISO §6.7.2.4/§6.7.2.5 and EP symmetric difference: intercept before the
    // generic path, whose integer instructions would treat the two bitmasks as
    // numbers.
    if (exprIsSet(*e.Left) && exprIsSet(*e.Right)) {
        auto* a = EmitExpr(*e.Left);
        auto* b = EmitExpr(*e.Right);
        if (!a || !b) codegenICE("set operator with an unlowerable operand");
        // Both operands are brought into one window before their bits meet.
        // A set-valued result is built in the window of the type Sema gave it,
        // since that is the window whoever receives it will read it in; a
        // comparison has no set type of its own, and the lower of the two
        // origins is chosen there because widening a window never drops a bit.
        const bool ResultIsSet =
            e.ResolvedType && e.ResolvedType->Kind == TypeKind::Set;
        const int64_t Base =
            ResultIsSet ? Sets.setBaseOf(e)
                        : std::min(Sets.setBaseOf(*e.Left), Sets.setBaseOf(*e.Right));
        a = Sets.alignSet(a, Sets.setBaseOf(*e.Left),  Base);
        b = Sets.alignSet(b, Sets.setBaseOf(*e.Right), Base);
        if (auto* r = Sets.emitSetBinary(e.Op, a, b)) return r;
        codegenICE("unhandled set operator '" + std::string(opSpelling(e.Op)) + "'");
    }

    // ISO §6.7.2.5: membership takes an ordinal on the left, so exprIsSet is
    // only true of the right operand.
    if (e.Op == TokenKind::In)
        return Sets.emitSetMember(EmitExpr(*e.Left), EmitExpr(*e.Right),
                             Sets.setBaseOf(*e.Right));

    auto* lv = EmitExpr(*e.Left);
    auto* rv = EmitExpr(*e.Right);
    if (!lv || !rv) codegenICE("binary operator with an unlowerable operand");

    // EP §6.8.3.2: complex arithmetic — intercept before the scalar path.
    if (lv->getType() == Complex.complexTy() || rv->getType() == Complex.complexTy()) {
        auto* lc = Complex.coerceToComplex(lv);
        auto* rc = Complex.coerceToComplex(rv);
        switch (e.Op) {
            case TokenKind::Plus:   return Complex.emitComplexAdd(lc, rc);
            case TokenKind::Minus:  return Complex.emitComplexSub(lc, rc);
            case TokenKind::Times:  return Complex.emitComplexMul(lc, rc);
            case TokenKind::Divide: return Complex.emitComplexDiv(lc, rc);
            case TokenKind::Equal: {
                // Component-wise equality.
                auto* req = B.CreateFCmpOEQ(
                    B.CreateExtractValue(lc, 0, "l.re"),
                    B.CreateExtractValue(rc, 0, "r.re"), "re.eq");
                auto* ieq = B.CreateFCmpOEQ(
                    B.CreateExtractValue(lc, 1, "l.im"),
                    B.CreateExtractValue(rc, 1, "r.im"), "im.eq");
                return B.CreateAnd(req, ieq, "cplx.eq");
            }
            case TokenKind::NotEqual: {
                auto* req = B.CreateFCmpOEQ(
                    B.CreateExtractValue(lc, 0, "l.re"),
                    B.CreateExtractValue(rc, 0, "r.re"), "re.eq");
                auto* ieq = B.CreateFCmpOEQ(
                    B.CreateExtractValue(lc, 1, "l.im"),
                    B.CreateExtractValue(rc, 1, "r.im"), "im.eq");
                auto* both = B.CreateAnd(req, ieq, "cplx.eq");
                return B.CreateNot(both, "cplx.ne");
            }
            default:
                codegenICE("unsupported operator on complex operands: "
                           + std::string(kindName(e.Op)));
        }
    }

    // Integer-to-real promotion.
    bool needFP = lv->getType()->isDoubleTy() || rv->getType()->isDoubleTy();
    if (needFP) {
        lv = ToDouble(lv);
        rv = ToDouble(rv);
    }

    // Two compatible ordinals need not share a width: a subrange is lowered to
    // i64 whatever it was cut from, while char stays i8 and boolean i1, so
    // comparing a subrange of char against char brings an i64 and an i8
    // together and icmp takes only one type.  The narrow ordinals are all
    // non-negative, so the widening is a zero-extend.
    if (!needFP && lv->getType()->isIntegerTy() && rv->getType()->isIntegerTy()
        && lv->getType() != rv->getType()) {
        llvm::Type* const wide = lv->getType()->getIntegerBitWidth()
                                         >= rv->getType()->getIntegerBitWidth()
                                     ? lv->getType()
                                     : rv->getType();
        lv = CoerceToType(lv, wide);
        rv = CoerceToType(rv, wide);
    }

    // Either side answers this, the two being compatible by now; the right one
    // is the fallback for an untyped left literal.
    bool uns = OrdinalIsUnsigned(e.Left->ResolvedType.get())
               || OrdinalIsUnsigned(e.Right->ResolvedType.get());

    switch (e.Op) {
        case TokenKind::Plus:
            return needFP ? B.CreateFAdd(lv, rv, "fadd")
                          : B.CreateAdd(lv, rv, "add");
        case TokenKind::Minus:
            return needFP ? B.CreateFSub(lv, rv, "fsub")
                          : B.CreateSub(lv, rv, "sub");
        case TokenKind::Times:
            return needFP ? B.CreateFMul(lv, rv, "fmul")
                          : B.CreateMul(lv, rv, "mul");
        case TokenKind::Divide:
            return B.CreateFDiv(ToDouble(lv), ToDouble(rv), "fdiv");
        case TokenKind::Div: {
            auto* n = ToI64(lv);
            auto* d = ToI64(rv);
            // The guards below compare against width-specific constants --
            // emitDivOverflowCheck's minint bit pattern most of all -- so
            // they need operands at the div's REAL width, not n/d above:
            // those are already sign-extended to i64 for the SDiv itself,
            // and MinInt16 sign-extended to i64 does not equal INT64_MIN, so
            // a check run on n/d as-is would never catch a 16-bit Turbo
            // `MinInt div -1`.  e.ResolvedType->Width is the same TyInt
            // answer (16 under Turbo, 64 otherwise) the Shl/Shr case below
            // already re-coerces to, for the identical reason.  Computing
            // the division itself at i64 stays correct once these guards
            // pass: for any in-range divisor/dividend pair other than
            // minint/-1 (which the overflow guard below catches first), a
            // narrower SDiv's quotient is exactly what the sign-extended,
            // wider-precision SDiv below computes too.
            //
            // Coerced from n/d (already i64) rather than the original lv/rv:
            // for ISO 7185 and Extended Pascal, divBitsTy is i64Ty already
            // (their one Integer is always Width 64), so CoerceToType is a
            // true no-op there -- it returns n/d themselves unchanged
            // (CodeGenExprs.cpp's coerceToType short-circuits whenever the
            // value's LLVM type already matches dst) rather than emitting a
            // second, redundant conversion of lv/rv that duplicates what
            // ToI64 above already did.
            llvm::Type* divBitsTy = llvm::IntegerType::get(
                Ctx, e.ResolvedType ? e.ResolvedType->Width : 64);
            const unsigned divWidth = divBitsTy->getIntegerBitWidth();
            RangeGuards.emitDivZeroCheck(CoerceToType(d, divBitsTy), "div", divWidth);
            // minint div -1: the one nonzero-divisor case that still
            // overflows, since minint's magnitude has no positive int64_t
            // representation (same UB shape as abs(minint)).
            RangeGuards.emitDivOverflowCheck(CoerceToType(n, divBitsTy),
                                              CoerceToType(d, divBitsTy), divWidth);
            return B.CreateSDiv(n, d, "sdiv");
        }
        case TokenKind::Mod: {
            auto* d = ToI64(rv);
            // Same Width contract as Div just above -- see its comment.
            // emitModDivisorCheck's own checks (== 0 for Turbo, <= 0
            // otherwise) happen to be sign-extension-invariant on their own
            // (a value's zero-ness and sign never change under sign
            // extension), unlike emitDivOverflowCheck's minint compare, but
            // the guard's operand type must still match its Width parameter
            // exactly (RangeCheckGuards.h) or building the icmp crashes.
            // Coerced from d (already i64), not rv -- see Div's identical
            // comment just above for why that keeps ISO/EP's Width=64 case a
            // true IR no-op instead of a redundant second conversion.
            llvm::Type* modBitsTy = llvm::IntegerType::get(
                Ctx, e.ResolvedType ? e.ResolvedType->Width : 64);
            RangeGuards.emitModDivisorCheck(CoerceToType(d, modBitsTy),
                                             modBitsTy->getIntegerBitWidth());
            auto* r = B.CreateSRem(ToI64(lv), d, "srem");
            // Turbo's mod takes its sign from the DIVIDEND -- exactly what
            // srem already computes -- rather than ISO's "0 <= mod <
            // divisor".  Confirmed against `fpc -Mtp`: (-7) mod 3 is -1,
            // not ISO's normalized 2; the divisor may be negative there too
            // (emitModDivisorCheck, just above, does not fire for Turbo).
            if (RangeGuards.isTurbo()) return r;
            // ISO §6.7.2.2 wants 0 <= i mod j < j, but srem takes its sign from
            // the dividend, so (-17) mod 5 comes back as -2 instead of 3.
            auto* neg = B.CreateICmpSLT(r, llvm::ConstantInt::get(I64Ty, 0),
                                              "mod.neg");
            return B.CreateSelect(neg, B.CreateAdd(r, d, "mod.adj"),
                                        r, "mod");
        }
        // Turbo bitwise and/or/xor on Integer operands (5 and 3 = 1).  lv/rv
        // are already unified to a common integer width by the "two
        // compatible ordinals" coercion above, so this is a plain LLVM
        // bitwise instruction at that width -- not forced to i64 the way
        // Div/Mod above are.  The And/Or arms are only ever reached here
        // with two Integer operands: emitBinary's own top-of-function block
        // already peeled off and returned the Boolean (logical) case.
        case TokenKind::And: return B.CreateAnd(lv, rv, "and.bits");
        case TokenKind::Or:  return B.CreateOr(lv, rv, "or.bits");
        // Xor is overloaded exactly like and/or, but never short-circuits
        // (both operands always matter to an exclusive-or), so it has no
        // top-of-function special case to peel off first -- CreateXor is
        // exactly right for either overload: on two i1 Booleans it is
        // logical exclusive-or, at any wider common integer width it is
        // bitwise.
        case TokenKind::Xor: return B.CreateXor(lv, rv, "xor");
        case TokenKind::Shl:
        case TokenKind::Shr: {
            // Unlike And/Or/Xor/Not just above -- which are bitwise
            // per-position, so computing them at a wider sign-extended width
            // and truncating back gives bit-identical low bits to computing
            // narrow directly -- a SHIFT's answer genuinely depends on which
            // bit is "the top one": both the (width-1) mask below and, for
            // 'shr', which bit fills in from the top are meaningless without
            // the TRUE width.  lv/rv's own LLVM type is not reliable for
            // this: an integer literal always lowers to an i64 ConstantInt
            // regardless of dialect (CGExprCore::emitExpr's IntLitExpr
            // case), so `1 shl 20` under Turbo (both operands literals) had
            // lv/rv already sitting at i64 by the time control reached here,
            // silently computing an unmasked 64-bit shift instead of a
            // masked 16-bit one. e.ResolvedType->Width is the answer Sema
            // already committed to (TyInt, i.e. LangOptions::
            // defaultIntWidth() -- 16 under Turbo, 64 otherwise), so operands
            // are re-coerced to it explicitly rather than trusting whatever
            // width they arrived in.
            llvm::Type* bitsTy = llvm::IntegerType::get(
                Ctx, e.ResolvedType ? e.ResolvedType->Width : 64);
            auto* l = CoerceToType(lv, bitsTy);
            auto* r = CoerceToType(rv, bitsTy);
            // LLVM's shl/lshr are POISON (not merely wrong) if the shift
            // amount is >= the operand's bit width -- e.g. `x shl 20` on a
            // 16-bit Turbo Integer -- so the count is masked to (width-1)
            // first, exactly what a hardware shift instruction (and FPC's
            // own codegen) does.
            const unsigned width = bitsTy->getIntegerBitWidth();
            auto* mask  = llvm::ConstantInt::get(bitsTy, width - 1);
            auto* count = B.CreateAnd(r, mask, "shift.count");
            // 'shr' is a LOGICAL shift even on a signed Integer: it does NOT
            // sign-extend the way `div` by a power of two would, so a
            // negative operand's top bit is filled with zero, not carried.
            return e.Op == TokenKind::Shl ? B.CreateShl(l, count, "shl")
                                           : B.CreateLShr(l, count, "shr");
        }
        case TokenKind::Equal:
            return needFP ? B.CreateFCmpOEQ(lv, rv, "feq")
                          : B.CreateICmpEQ(lv, rv, "eq");
        case TokenKind::NotEqual:
            return needFP ? B.CreateFCmpONE(lv, rv, "fne")
                          : B.CreateICmpNE(lv, rv, "ne");
        case TokenKind::LessThan:
            return needFP ? B.CreateFCmpOLT(lv, rv, "flt")
                   : uns   ? B.CreateICmpULT(lv, rv, "ult")
                           : B.CreateICmpSLT(lv, rv, "slt");
        case TokenKind::LessThanOrEqual:
            return needFP ? B.CreateFCmpOLE(lv, rv, "fle")
                   : uns   ? B.CreateICmpULE(lv, rv, "ule")
                           : B.CreateICmpSLE(lv, rv, "sle");
        case TokenKind::GreaterThan:
            return needFP ? B.CreateFCmpOGT(lv, rv, "fgt")
                   : uns   ? B.CreateICmpUGT(lv, rv, "ugt")
                           : B.CreateICmpSGT(lv, rv, "sgt");
        case TokenKind::GreaterThanOrEqual:
            return needFP ? B.CreateFCmpOGE(lv, rv, "fge")
                   : uns   ? B.CreateICmpUGE(lv, rv, "uge")
                           : B.CreateICmpSGE(lv, rv, "sge");
        default:
            codegenICE("unhandled binary operator '"
                       + std::string(kindName(e.Op)) + "'");
    }
}

llvm::Value* CGBinaryOps::emitUnary(const UnaryExpr& e) {
    // Turbo `@x`: the result IS the operand's address, not a value computed
    // from it, so this takes the EmitLValue path instead of the EmitExpr one
    // every other unary operator shares below -- EmitExpr would load (or, for
    // an index/field path, needlessly re-walk) storage this only ever needs
    // the address of.  Sema's isLValue check (checkUnary) already guarantees
    // an addressable operand, so a null address here means the two disagree.
    if (e.Op == TokenKind::At) {
        auto* addr = EmitLValue(*e.Operand);
        if (!addr) codegenICE("'@' applied to a non-addressable operand");
        return addr;
    }
    auto* v = EmitExpr(*e.Operand);
    if (!v) codegenICE("unary operator with an unlowerable operand");
    switch (e.Op) {
        case TokenKind::Minus:
            // EP §6.4.2.2: unary minus on complex negates both components.
            if (v->getType() == Complex.complexTy()) {
                auto* re = B.CreateExtractValue(v, 0, "neg.re");
                auto* im = B.CreateExtractValue(v, 1, "neg.im");
                return Complex.makeComplex(B.CreateFNeg(re, "neg.re"),
                                   B.CreateFNeg(im, "neg.im"));
            }
            if (v->getType()->isDoubleTy())
                return B.CreateFNeg(v, "fneg");
            return B.CreateNeg(v, "neg");
        case TokenKind::Not: {
            // Turbo overloads 'not' like 'and'/'or'/'xor': bitwise
            // two's-complement negation (`not x` = `-x-1`) on an Integer
            // operand, logical negation on a Boolean one.  Sema::checkUnary
            // already refused every other operand type, so ResolvedType's
            // Kind alone tells the two apart -- the same way emitBinary's
            // bothBoolean check does for and/or.  LLVM's CreateNot is a
            // plain xor-with-all-ones at whatever width v already is, so the
            // bitwise case needs no EnsureI1 (which would wrongly truncate a
            // wide Integer down to its low bit).
            const bool isBool = e.Operand->ResolvedType
                              && e.Operand->ResolvedType->Kind == TypeKind::Boolean;
            if (!isBool) return B.CreateNot(v, "not.bits");
            auto* b = EnsureI1(v);
            return B.CreateNot(b, "not");
        }
        // Unary plus is the identity; anything else passing the operand
        // through unchanged would be the operator quietly going missing.
        case TokenKind::Plus: return v;
        default:
            codegenICE("unhandled unary operator '"
                       + std::string(opSpelling(e.Op)) + "'");
    }
}
