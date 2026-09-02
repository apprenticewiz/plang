#include "CGBinaryOps.h"

#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/Token.h"
#include "plang/Sema/Sema.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"

using namespace plang;

bool CGBinaryOps::exprIsStringLike(const ExprNode& e) {
    return ExprIsVarStr(e) || ExprIsCharStr(e)
        || (e.ResolvedType && e.ResolvedType->Kind == TypeKind::String);
}

bool CGBinaryOps::exprIsSet(const ExprNode& e) {
    // Issue #584: a schema-instantiated set (`s(5)`) carries TypeKind::
    // Schema/SchemaInstance on the expression -- the real Set type is one
    // schemaUnderlying hop away, mirroring Sema's own unwrap.
    const Type* t = schemaUnderlying(e.ResolvedType.get());
    return t && t->Kind == TypeKind::Set;
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
                fn, {ToI64(EmitExpr(*e.Left), operandIsSigned(*e.Left)),
                     ToI64(EmitExpr(*e.Right), operandIsSigned(*e.Right))}, "ipow");
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

    // Turbo string[N] concatenation -- decided FIRST and separately from the
    // EP block just below, which only ever recognizes VarString/String/
    // char-string-type operands (ExprIsVarStr is false for ShortString by
    // construction -- see its own comment, CodeGenImpl.h) and always builds
    // an EP-shaped, eight-byte-headed result temporary via plang_str_*.
    // Mirrors the EP block's own operand-to-(addr,cap) shape but calls the
    // plang_sstr_* family throughout and sizes the result off Sema's own
    // ShortString result type (Type::makeShortString, SemaExpr.cpp's
    // checkBinary Plus case) rather than re-deriving it here -- Sema has
    // already summed and clamped the two operands' capacities at 255.
    if (e.Op == TokenKind::Plus && ExprIsShortStr(e)) {
        // A non-ShortString operand is a char or a plain literal/String --
        // Sema's checkBinary Plus case is what limits it to exactly those,
        // so unlike the EP lambda below this has no ExprIsCharStr arm to
        // mirror. A literal's OWN length sizes its temporary (not a bare
        // "one character" guess) so a multi-character literal operand
        // ('abc' + s) is not silently truncated to its first character.
        auto sstrOperand = [&](const ExprNode& x) -> std::pair<llvm::Value*, llvm::Value*> {
            // StrCall.emitStrAddr, not EmitLValue: an operand may itself be a
            // computed ShortString value with no storage of its own -- a
            // nested concatenation (`(s + t) + u`), most concretely -- and
            // emitStrAddr already falls back to EmitExpr for exactly that.
            if (ExprIsShortStr(x)) return {StrCall.emitStrAddr(x), i64c(ExprShortStrCap(x))};
            // A literal's OWN length is the byte count to copy -- NOT the
            // capacity floor below, which exists only so a 0-length literal
            // ('', legal under Turbo too: see SemaExpr.cpp's checkExpr
            // StringLitExpr arm) still gets a real (1-byte-minimum) alloca to
            // point at.  Conflating the two (using the floored capacity as
            // the memcpy length too, as this used to) copied ONE byte out of
            // an empty literal's interned (zero-length, so off-the-end) data
            // for `s + ''`/`s = ''` under Turbo -- unreachable before '' had
            // a Turbo-usable type at all, and live from the moment it does.
            int64_t litLen = 0;
            bool isLit = false;
            if (auto* sl = llvm::dyn_cast<StringLitExpr>(&x)) {
                isLit  = true;
                litLen = static_cast<int64_t>(sl->Value.size());
            }
            const int64_t cap = isLit ? std::max<int64_t>(1, litLen) : 1;
            auto* val = EmitExpr(x);
            auto* tmp = CreateEntryAlloca(Types.sstrStructType(cap), "sstr.operand");
            if (val && val->getType()->isIntegerTy(8))
                Strings.emitSstrFromChar(tmp, i64c(cap), val);
            else if (isLit)
                Strings.emitSstrFromBytes(tmp, i64c(cap), val, i64c(litLen));
            else if (val)
                Strings.emitSstrFromBytes(tmp, i64c(cap), val, i64c(cap));
            return {tmp, i64c(cap)};
        };
        auto [lv, capL] = sstrOperand(*e.Left);
        auto [rv, capR] = sstrOperand(*e.Right);
        const int64_t capRes = ExprShortStrCap(e);
        auto* capResV = i64c(capRes);
        auto* resPtr  = CreateEntryAlloca(Types.sstrStructType(capRes), "sstr.concat");
        auto* fn = Strings.getStrFn("plang_sstr_concat", llvm::Type::getVoidTy(Ctx),
            {PtrTy, I64Ty, PtrTy, I64Ty, PtrTy, I64Ty});
        B.CreateCall(fn, {resPtr, capResV, lv, capL, rv, capR});
        return resPtr;
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

    // Turbo string[N] comparison -- PREFIX lexicographic order, SHORTER is
    // LESS (plang_sstr_eq and siblings, plang_sstr.cpp): the OPPOSITE of
    // EP's space-padded plang_str_eq family just below ('a' < 'a ' is TRUE
    // here, but 'a' = 'a ' there), so the two must never share a runtime
    // entry point or an operand-shaping lambda.  Checked FIRST and
    // unconditionally returns when it fires -- critical, not cosmetic: the
    // EP block's own gate, exprIsStringLike, is true for a plain String/Char
    // operand even when the OTHER side is ShortString (e.g. `shortStr =
    // 'hi'`), so without deciding this case first, that comparison would
    // fall into the EP block below, which addresses a ShortString operand
    // as if it had EP's eight-byte length header instead of ShortString's
    // one-byte one -- the exact StringCallMarshalling-shaped corruption this
    // whole work item exists to avoid, here in a second place.  A
    // comparison with NO ShortString operand at all leaves this condition
    // false and reaches the EP block exactly as before.
    //
    // Issue #636: under -std=turbo, a multi-character string LITERAL is
    // typed Kind::String (SemaExpr.cpp's checkExpr — TyStr is what such a
    // literal resolves to outside EP, same as ISO 7185's), never
    // Kind::ShortString, since a literal carries no variable of its own to
    // stamp with a ShortString capacity.  So a comparison between two
    // literals (or a Char literal and a multi-char one, e.g. 'a' = 'a ')
    // has NEITHER operand ExprIsShortStr, even though Turbo has no EP
    // string semantics at all to fall back on -- there is no ShortString
    // variable anywhere in sight for the EP block below to be a correct
    // fallback FROM.  isTurboCmpKind mirrors Sema::checkBinary's own
    // isShortStrLike twin of exprIsStringLike (SemaExpr.cpp) that decided
    // this comparison was legal in the first place; requiring one side to
    // be Kind::String (an actual literal) keeps a same-dialect Char-vs-Char
    // comparison (e.g. 'a' = 'b') on its existing ordinal-compare path
    // instead of routing it through the ShortString runtime for no reason.
    auto isTurboCmpKind = [](const ExprNode& expr) {
        return expr.ResolvedType
            && (expr.ResolvedType->Kind == TypeKind::ShortString
                || expr.ResolvedType->Kind == TypeKind::Char
                || expr.ResolvedType->Kind == TypeKind::String);
    };
    const bool eitherIsStrLit =
        (e.Left->ResolvedType  && e.Left->ResolvedType->Kind  == TypeKind::String)
     || (e.Right->ResolvedType && e.Right->ResolvedType->Kind == TypeKind::String);
    if ((e.Op == TokenKind::Equal || e.Op == TokenKind::NotEqual
         || e.Op == TokenKind::LessThan || e.Op == TokenKind::LessThanOrEqual
         || e.Op == TokenKind::GreaterThan || e.Op == TokenKind::GreaterThanOrEqual)
            && (ExprIsShortStr(*e.Left) || ExprIsShortStr(*e.Right)
                || (RangeGuards.isTurbo() && eitherIsStrLit
                    && isTurboCmpKind(*e.Left) && isTurboCmpKind(*e.Right)))) {
        const char* fnName =
            e.Op == TokenKind::Equal           ? "plang_sstr_eq" :
            e.Op == TokenKind::NotEqual        ? "plang_sstr_ne" :
            e.Op == TokenKind::LessThan        ? "plang_sstr_lt" :
            e.Op == TokenKind::LessThanOrEqual ? "plang_sstr_le" :
            e.Op == TokenKind::GreaterThan     ? "plang_sstr_gt" : "plang_sstr_ge";
        auto toSstrPtr = [&](const ExprNode& expr) -> std::pair<llvm::Value*, llvm::Value*> {
            // See sstrOperand's identical comment (this file's concatenation
            // block, just above): an operand may be a computed ShortString
            // value (e.g. `(s + t) = u`) with no lvalue of its own.
            if (ExprIsShortStr(expr)) return {StrCall.emitStrAddr(expr), i64c(ExprShortStrCap(expr))};
            // See sstrOperand's identical fix, just above in this file's
            // concatenation block, for why litLen (not the floored capacity)
            // is the memcpy length: an empty literal's true length is 0, and
            // conflating the two copied one stray byte out of it.
            int64_t litLen = 0;
            bool isLit = false;
            if (auto* sl = llvm::dyn_cast<StringLitExpr>(&expr)) {
                isLit  = true;
                litLen = static_cast<int64_t>(sl->Value.size());
            }
            const int64_t cap = isLit ? std::max<int64_t>(1, litLen) : 1;
            auto* val = EmitExpr(expr);
            auto* tmp = CreateEntryAlloca(Types.sstrStructType(cap), "sstr.cmp.tmp");
            if (val && val->getType()->isIntegerTy(8))
                Strings.emitSstrFromChar(tmp, i64c(cap), val);
            else if (isLit)
                Strings.emitSstrFromBytes(tmp, i64c(cap), val, i64c(litLen));
            else if (val)
                Strings.emitSstrFromBytes(tmp, i64c(cap), val, i64c(cap));
            return {tmp, i64c(cap)};
        };
        auto [la, capL] = toSstrPtr(*e.Left);
        auto [ra, capR] = toSstrPtr(*e.Right);
        auto* fn  = Strings.getStrFn(fnName, I8Ty, {PtrTy, I64Ty, PtrTy, I64Ty});
        auto* raw = B.CreateCall(fn, {la, capL, ra, capR}, "sstr.cmp");
        return EnsureI1(raw);
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
        const bool ResultIsSet = exprIsSet(e);
        const int64_t Base =
            ResultIsSet ? Sets.setBaseOf(e)
                        : std::min(Sets.setBaseOf(*e.Left), Sets.setBaseOf(*e.Right));
        a = Sets.alignSet(a, Sets.setBaseOf(*e.Left),  Base);
        b = Sets.alignSet(b, Sets.setBaseOf(*e.Right), Base);
        if (auto* r = Sets.emitSetBinary(e.Op, a, b)) return r;
        codegenICE("unhandled set operator '" + std::string(opSpelling(e.Op)) + "'");
    }

    // Turbo: PChar-like pointer arithmetic -- `p + n`, `p - n`, `p1 - p2`.
    // Sema::checkBinary (SemaExpr.cpp) is the only thing that accepts one of
    // these operator/operand shapes at all, and only under -std=turbo for a
    // pointer whose pointee is Char (isCharPointerType, Type.h), so reaching
    // here already means Sema approved it -- an ISO/EP `^char` never gets
    // this far with a Plus/Minus BinaryExpr, and no further gating belongs
    // here.  Intercepted before the generic scalar path below the same way
    // the set-binary block just above intercepts before the generic path's
    // integer instructions would treat two bitmasks as numbers -- the
    // generic path's CreateAdd/CreateSub would otherwise run on a raw
    // pointer SSA value as though it were an integer.
    if (e.Op == TokenKind::Plus || e.Op == TokenKind::Minus) {
        auto isCharPtr = [](const ExprNode& X) {
            const Type* T = X.ResolvedType.get();
            return T && T->Kind == TypeKind::Pointer && T->PointeeType
                && T->PointeeType->Kind == TypeKind::Char;
        };
        const bool LPtr = isCharPtr(*e.Left);
        const bool RPtr = isCharPtr(*e.Right);
        if (LPtr || RPtr) {
            if (LPtr && RPtr) {
                // p1 - p2: byte difference divided by the pointee's byte
                // size.  Char is always 1 today, but this is written off
                // Sema::byteSizeOf rather than hardcoded so the same shape
                // still works if a future pointee besides Char ever reuses
                // this path.
                //
                // Result width: FPC's own answer here is Longint (32-bit
                // signed) -- confirmed against fpc -Mtp (issue #713: a real
                // 40000-Char span came back truncated and wrapped through
                // plang's 16-bit Integer instead).  Sema::checkBinary's own
                // p1-p2 case now returns that same LongInt (Ctx_.getInt(32,
                // true), see its comment) instead of TyInt, so resTy below --
                // derived from e.ResolvedType, not hardcoded -- follows
                // automatically; nothing here needed to change once the
                // Turbo sized-integer ladder made LongInt nameable.
                auto* lp = EmitExpr(*e.Left);
                auto* rp = EmitExpr(*e.Right);
                auto* li = B.CreatePtrToInt(lp, I64Ty, "pdiff.l");
                auto* ri = B.CreatePtrToInt(rp, I64Ty, "pdiff.r");
                auto* byteDiff = B.CreateSub(li, ri, "pdiff.bytes");
                const uint64_t ElemSz =
                    Sema::byteSizeOf(*e.Left->ResolvedType->PointeeType).value_or(1);
                llvm::Value* diff = byteDiff;
                if (ElemSz > 1)
                    diff = B.CreateSDiv(byteDiff,
                        llvm::ConstantInt::get(I64Ty, ElemSz), "pdiff.elems");
                llvm::Type* resTy = e.ResolvedType
                    ? Types.llvmTypeOfSemaType(*e.ResolvedType) : I64Ty;
                return B.CreateTrunc(diff, resTy, "pdiff");
            }
            // p + n or p - n: a GEP scaled by the pointee LLVM type's own
            // ABI size -- the identical implicit scaling every other typed
            // GEP in this codebase already relies on (see e.g.
            // CGIndexAccess::emitIndexGEP's array-element GEPs), so no
            // explicit byte-size multiply is needed for this direction.
            // Sema has already refused every shape but pointer-then-integer
            // (LPtr with an integral right operand for Plus; either LPtr
            // with an integral right operand or LPtr&&RPtr, just handled
            // above, for Minus) -- see checkBinary's own comment for why
            // `n + p`/`n - p` are refused even though fpc allows ordinary
            // `+` to commute.
            const ExprNode& PtrOperand = LPtr ? *e.Left  : *e.Right;
            const ExprNode& IdxOperand = LPtr ? *e.Right : *e.Left;
            auto* base = EmitExpr(PtrOperand);
            auto* idx  = ToI64(EmitExpr(IdxOperand), operandIsSigned(IdxOperand));
            if (e.Op == TokenKind::Minus)
                idx = B.CreateNeg(idx, "pchar.sub.idx");
            llvm::Type* elemLLVMTy =
                Types.llvmTypeOfSemaType(*PtrOperand.ResolvedType->PointeeType);
            return B.CreateGEP(elemLLVMTy, base, {idx}, "pchar.add");
        }
    }

    // ISO §6.7.2.5: membership takes an ordinal on the left, so exprIsSet is
    // only true of the right operand.
    if (e.Op == TokenKind::In)
        // Issue #637: declaredRangeOf(*e.Right)/e.Loc let this range-check
        // the left operand against the set's own declared base type under
        // {$R+}, the same guard the constructor paths (+  [x], Include) get.
        return Sets.emitSetMember(EmitExpr(*e.Left), EmitExpr(*e.Right),
                             Sets.setBaseOf(*e.Right),
                             Sets.declaredRangeOf(*e.Right), e.Loc,
                             operandIsSigned(*e.Left));

    auto* lv = EmitExpr(*e.Left);
    auto* rv = EmitExpr(*e.Right);
    if (!lv || !rv) codegenICE("binary operator with an unlowerable operand");

    // Issue #577 (reopened): Plus/Minus/Times need the ORIGINAL, un-narrowed
    // operand values -- see the switch's Plus/Minus/Times arms below for why
    // -- so they are captured here, before `lv`/`rv` are ever overwritten by
    // needFP's ToDouble or the integer coercion block further down.
    llvm::Value* const lvRaw = lv;
    llvm::Value* const rvRaw = rv;

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

    // Integer-to-real promotion.  isFloatingPointTy, not isDoubleTy: Turbo's
    // Single (float) is a second floating type alongside Real (double), and
    // testing isDoubleTy alone let a Single operand miss this gate entirely
    // -- it fell through to the INTEGER arithmetic further down instead,
    // silently computing the wrong thing on a value LLVM's verifier does
    // not reject (float and integer are both "not a pointer", so nothing
    // catches this at the IR level; it just computes nonsense).
    bool needFP = lv->getType()->isFloatingPointTy() || rv->getType()->isFloatingPointTy();
    if (needFP) {
        lv = ToDouble(lv);
        rv = ToDouble(rv);
    }

    // Two compatible ordinals need not share a width: a subrange is lowered to
    // i64 whatever it was cut from, while char stays i8 and boolean i1, so
    // comparing a subrange of char against char brings an i64 and an i8
    // together and icmp takes only one type.  Each side widens with its OWN
    // Sema-resolved signedness (operandIsSigned), not a blanket "narrow
    // ordinals are all non-negative" assumption: that used to be true when
    // Char/Boolean/plain-signed-Integer were the only narrower-than-i64
    // ordinals in existence, but Turbo's sized-integer ladder added narrow
    // UNSIGNED rungs wider than i8 (Word/Cardinal/LongWord) and a narrow
    // SIGNED rung at i8 (ShortInt) -- e.g. `QWord + Cardinal` sign-extending
    // the Cardinal here instead of zero-extending it silently subtracted
    // 2^32 from the result.
    //
    // Unlike a plain width mismatch, though, a SIGNEDNESS mismatch at the
    // SAME width is not safe to leave alone even when lv/rv's LLVM types
    // already agree: a same-width signed type can never hold an unsigned
    // operand's own full range (2^(w-1)-1 is always < 2^w-1), so comparing
    // or computing at that shared width forces one side's true value to be
    // silently reinterpreted -- issue #629's repro (`Integer(-1) = Word
    // (65535)`, both 16 bits) compared as UNSIGNED at 16 bits purely
    // because Word's own OrdinalIsUnsigned outvoted Integer's, reading -1's
    // bit pattern as 65535 and calling the two equal.  This mirrors Sema::
    // commonIntType's own promotion table EXACTLY (SemaExpr.cpp's comment
    // there has the full derivation and the real-`fpc -Mtp` confirmations)
    // -- comparison and arithmetic genuinely share one table, confirmed
    // even at the one rung that looked at first like it might not (the
    // capped top rung, Unsigned.Width == 64: `Int64 < QWord` compares
    // SIGNED, matching `Int64 + QWord`'s SIGNED result type, while
    // `ShortInt < QWord` compares UNSIGNED, matching `ShortInt + QWord`'s
    // UNSIGNED result type -- both checked directly against real
    // `fpc -Mtp`, not assumed from the arithmetic case alone).  It has to
    // be reimplemented here rather than called, because a COMPARISON's own
    // e.ResolvedType is always plain Boolean, never the promoted operand
    // type commonIntType computes for arithmetic, so there is no
    // ResolvedType-based width to widen to the way Div/Mod/Shl/Shr below
    // use e.ResolvedType->Width.  Any change to one table must stay in
    // lockstep with the other.
    //
    // Defaults to unsigned, matching the pre-fix behavior's OR-of-both-
    // operands rule whenever this block does not run at all (needFP, or
    // either side is somehow not an integer type) -- every switch arm below
    // that reads `uns` only does so inside a `!needFP` branch, so a stale
    // default here is never actually read in the needFP case, but a real
    // value beats an uninitialized read if that ever stops being true.
    bool uns = true;
    if (!needFP && lv->getType()->isIntegerTy() && rv->getType()->isIntegerTy()) {
        unsigned lw = lv->getType()->getIntegerBitWidth();
        unsigned rw = rv->getType()->getIntegerBitWidth();
        // Issue #577: an integer LITERAL operand's own LLVM constant is
        // always i64 (CGExprCore.cpp's IntLitExpr arm -- see its own
        // comment for why that has to stay true regardless of Sema's own
        // ResolvedType for the literal, which is always the dialect's
        // plain default Integer no matter the literal's actual magnitude,
        // and so is not a safe width to emit the CONSTANT at). Left as-is,
        // that spurious i64 width would always win this function's
        // "promote to whichever is WIDER" rule below, silently promoting
        // e.g. a 16-bit Turbo Integer variable's arithmetic/comparison
        // against ANY literal up to 64 bits -- unlike the identical
        // arithmetic against a second 16-bit VARIABLE holding the exact
        // same value, which stays 16-bit and wraps/compares correctly.
        // Narrowing a literal operand's WIDTH (for promotion purposes
        // only; the underlying i64 llvm::Value itself is untouched, so no
        // precision is lost if this doesn't apply) down to the other,
        // non-literal operand's own width -- but ONLY when the literal's
        // actual value still fits there under that operand's own
        // signedness -- makes a literal participate in width-unification
        // exactly the way a variable of its own value would: adopts the
        // narrower type when the value fits it, falls back to its full i64
        // width (the pre-fix behavior) otherwise, e.g. a 32-bit-or-wider
        // literal paired with a Byte variable still correctly promotes
        // past Byte's own width rather than truncating the literal to fit.
        auto literalFitsWidth = [](int64_t v, unsigned w, bool wUnsigned) {
            if (w == 0 || w >= 64) return true;
            if (wUnsigned)
                return v >= 0 && static_cast<uint64_t>(v) < (uint64_t{1} << w);
            const int64_t lo = -(static_cast<int64_t>(1) << (w - 1));
            const int64_t hi = (static_cast<int64_t>(1) << (w - 1)) - 1;
            return v >= lo && v <= hi;
        };
        if (auto* LitL = llvm::dyn_cast<IntLitExpr>(e.Left.get());
            LitL && !llvm::isa<IntLitExpr>(e.Right.get())) {
            if (literalFitsWidth(LitL->Value, rw, !operandIsSigned(*e.Right)))
                lw = rw;
        } else if (auto* LitR = llvm::dyn_cast<IntLitExpr>(e.Right.get());
                   LitR && !llvm::isa<IntLitExpr>(e.Left.get())) {
            if (literalFitsWidth(LitR->Value, lw, !operandIsSigned(*e.Left)))
                rw = lw;
        }
        const bool lSigned = operandIsSigned(*e.Left);
        const bool rSigned = operandIsSigned(*e.Right);
        unsigned targetWidth;
        bool targetSigned;
        if (lSigned == rSigned) {
            targetWidth = std::max(lw, rw);
            targetSigned = lSigned;
        } else {
            const unsigned uw = lSigned ? rw : lw; // the unsigned operand's width
            const unsigned sw = lSigned ? lw : rw; // the signed operand's width
            if (sw > uw) {
                targetWidth = sw;
                targetSigned = true;
            } else {
                const unsigned promoted = uw * 2;
                if (promoted <= 64) {
                    targetWidth = promoted;
                    targetSigned = true;
                } else {
                    // uw == 64: no wider integer type exists to double
                    // into.  A tied rank (sw == uw, e.g. Int64 vs QWord)
                    // still resolves SIGNED despite the cap; a strictly
                    // narrower signed operand (e.g. ShortInt vs QWord)
                    // falls back to the unsigned operand's own type --
                    // both confirmed against real `fpc -Mtp` (see Sema::
                    // commonIntType's identical branch for the full
                    // derivation).
                    targetWidth = uw;
                    targetSigned = (sw == uw);
                }
            }
        }
        llvm::Type* const wide = llvm::IntegerType::get(Ctx, targetWidth);
        lv = CoerceToType(lv, wide, lSigned);
        rv = CoerceToType(rv, wide, rSigned);
        uns = !targetSigned;
    }

    // Issue #577 (reopened) -- Plus/Minus/Times, integer case: computed at
    // full i64 precision from the RAW operands (each widened per its OWN
    // Sema-resolved signedness, exactly what Div/Mod's own `n`/`d` already
    // do just below), NOT at `wide` -- the narrower, mutually-unified width
    // the block above computed for comparisons/bitwise ops.  This is a
    // deliberate divergence from that block, confirmed against extensive
    // real `fpc -Mtp` sweeps (var+var, var+literal, stored, and inline/
    // unstored, across every sized-integer width):
    //
    //   - fpc's own code generator never actually computes narrow-integer
    //     arithmetic at the variables' declared width at all -- it always
    //     widens to full register precision first, and ONLY narrows back
    //     down when the result is STORED into a fixed-width l-value (a
    //     variable, a value parameter, a typed constant) or otherwise
    //     consumed by something that reads a specific width.  A bare
    //     `Writeln(byteVar + byteVar2)` for two Byte(200) variables prints
    //     400, not a wrapped 144 -- even though BOTH operands are ordinary
    //     variables, no literal in sight.  `by3 := byteVar + byteVar2;
    //     Writeln(by3)` DOES print 144, because the 8-bit STORE into by3
    //     (not the addition itself) is what narrows it -- CGAssign's own
    //     dstTy SExtOrTrunc/ZExtOrTrunc (CGAssign.cpp) already does exactly
    //     that once this function hands back an i64 value wider than the
    //     target, and StringCallMarshalling::emitCallArg's identical
    //     CoerceToType does the same for a value-parameter actual.  Nothing
    //     downstream needed to change for the STORED case to keep working;
    //     the previous eager truncation done HERE was simply narrowing too
    //     early, before any real consumer had a chance to ask for a
    //     specific width.
    //   - This also *automatically* resolves the reopened issue's first
    //     confirmed bug (Byte/ShortInt still wrapping inconsistently between
    //     var+var and var+literal): since neither operand shape reaches
    //     `wide`'s width-unification at all anymore for Plus/Minus/Times,
    //     there is no literal-vs-variable asymmetry left to go wrong -- both
    //     shapes now take the identical "widen own operand to i64, add,
    //     leave narrowing to whoever consumes the result" path.
    //   - And it resolves the regression PR #756 introduced (a narrow
    //     SIGNED type's overflowing INLINE-use expression, e.g. `ii := 32767;
    //     Writeln(ii + 1)`, wrongly wrapping to -32768 where real fpc prints
    //     32768): an unstored expression has no consumer to narrow it, so it
    //     simply stays at i64 -- ToI64 (BuiltinIO's write-argument path,
    //     already called on every write parameter) is then a true no-op,
    //     matching fpc exactly.
    //
    // Comparisons and the Turbo bitwise and/or/xor/shl/shr operators below
    // are UNCHANGED by this: they still read `lv`/`rv` (the `wide`-coerced
    // values), since a comparison's correctness genuinely depends on the
    // SAME-DECLARED-WIDTH mixed-sign unification issue #629/#630 fixed (see
    // that block's own comment) -- widening a comparison operand further
    // than necessary is always safe (it only ever adds unambiguous
    // precision), but narrowing arithmetic's OWN result early, as this used
    // to, is exactly what caused both bugs above.
    switch (e.Op) {
        case TokenKind::Plus:
            return needFP ? B.CreateFAdd(lv, rv, "fadd")
                          : B.CreateAdd(ToI64(lvRaw, operandIsSigned(*e.Left)),
                                        ToI64(rvRaw, operandIsSigned(*e.Right)), "add");
        case TokenKind::Minus:
            return needFP ? B.CreateFSub(lv, rv, "fsub")
                          : B.CreateSub(ToI64(lvRaw, operandIsSigned(*e.Left)),
                                        ToI64(rvRaw, operandIsSigned(*e.Right)), "sub");
        case TokenKind::Times:
            return needFP ? B.CreateFMul(lv, rv, "fmul")
                          : B.CreateMul(ToI64(lvRaw, operandIsSigned(*e.Left)),
                                        ToI64(rvRaw, operandIsSigned(*e.Right)), "mul");
        case TokenKind::Divide:
            return B.CreateFDiv(ToDouble(lv), ToDouble(rv), "fdiv");
        case TokenKind::Div: {
            auto* n = ToI64(lv, operandIsSigned(*e.Left));
            auto* d = ToI64(rv, operandIsSigned(*e.Right));
            // The guards below compare against width-specific constants --
            // emitDivOverflowCheck's minint bit pattern most of all -- so
            // they need operands at the div's REAL width, not n/d above:
            // those are already sign-extended to i64 for the SDiv itself,
            // and MinInt16 sign-extended to i64 does not equal INT64_MIN, so
            // a check run on n/d as-is would never catch a 16-bit Turbo
            // `MinInt div -1`.  e.ResolvedType->Width is now Sema::
            // commonIntType's answer -- the WIDER of the two operands' own
            // Width, not unconditionally the dialect's default TyInt -- so
            // e.g. `Int64Var div ByteVar` checks at 64 bits, not 16 or 8.
            // Computing the division itself at i64 stays correct once these
            // guards pass: for any in-range divisor/dividend pair other than
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
            // ToI64 above already did.  The signedness passed to CoerceToType
            // here is a don't-care either way -- n/d are already i64 and
            // divBitsTy's width is never wider than 64, so this is always a
            // narrowing (or exact) *OrTrunc, which does not read it -- but a
            // real answer is threaded through anyway rather than an
            // arbitrary placeholder.
            llvm::Type* divBitsTy = llvm::IntegerType::get(
                Ctx, e.ResolvedType ? e.ResolvedType->Width : 64);
            const unsigned divWidth = divBitsTy->getIntegerBitWidth();
            const bool nSigned = operandIsSigned(*e.Left), dSigned = operandIsSigned(*e.Right);
            auto* nAtWidth = CoerceToType(n, divBitsTy, nSigned);
            auto* dAtWidth = CoerceToType(d, divBitsTy, dSigned);
            RangeGuards.emitDivZeroCheck(dAtWidth, "div", divWidth);
            // minint div -1: the one nonzero-divisor case that still
            // overflows AT THE OPERATION'S OWN WIDTH, since minint's
            // magnitude has no positive same-width representation (same UB
            // shape as abs(minint)).
            //
            // Turbo does NOT trap on this (issue #638): confirmed against
            // `fpc -Mtp` at every width up to Int64, `MinInt div -1`
            // computes silently -- even under `{$Q+}`, so this is not
            // something OverflowChecks gates either; fpc's own div overflow
            // is simply never checked, at any width. Unlike Plus/Minus/
            // Times's own silent wraparound (this switch's own comment,
            // above), the RIGHT answer here is not "wrap immediately": `n`
            // and `d` are already sign-extended to i64 (ToI64, above), so
            // for any divWidth < 64 the division computed at that full i64
            // precision is safe hardware-wise (a narrower minint's
            // sign-extended magnitude always fits i64, e.g. minint16's
            // 32768 quotient) AND gives the exact answer fpc itself prints
            // when the quotient is never narrower-stored (`writeln(i div
            // (-1))` alone prints 32768, not -32768) -- narrowing back to
            // -32768 only happens later, on assignment into a 16-bit
            // variable, through the ordinary truncate-on-store every other
            // overflow already goes through. So below divWidth 64 this is a
            // plain, unguarded SDiv, exactly like every in-range case.
            //
            // At divWidth == 64 (Int64/QWord, or ISO 7185/EP's own Integer,
            // whose one width always is 64) there is no such escape: `n`/`d`
            // ARE the actual i64 division operands, 2^63 has no i64
            // representation at ANY precision to compute first and narrow
            // later, and an unguarded SDiv here is real x86 idiv overflow
            // UB. Turbo still must not trap on it (same field-practice
            // requirement above, confirmed directly against fpc's own
            // Int64 case, which prints MinInt64 back unchanged) -- so the
            // divisor is forced to 1 in exactly this one bad case, making
            // `n div safeD` compute `n div 1 == n`, i.e. MinInt64 right
            // back, the same answer fpc gives, without ever executing the
            // trapping division. ISO 7185 and Extended Pascal keep the
            // unconditional runtime-error trap instead (unaffected by this
            // issue, and not what fpc -Mtp is a reference for).
            if (RangeGuards.isTurbo()) {
                if (divWidth < 64) return B.CreateSDiv(n, d, "sdiv");
                auto* isMinInt = B.CreateICmpEQ(nAtWidth,
                    llvm::ConstantInt::get(divBitsTy, llvm::APInt::getSignedMinValue(divWidth)),
                    "div.ismin");
                auto* isNegOne = B.CreateICmpEQ(dAtWidth,
                    llvm::ConstantInt::getSigned(divBitsTy, -1), "div.isnegone");
                auto* bad = B.CreateAnd(isMinInt, isNegOne, "div.overflow");
                auto* safeD = B.CreateSelect(bad,
                    llvm::ConstantInt::get(d->getType(), 1), d, "div.safed");
                return B.CreateSDiv(n, safeD, "sdiv");
            }
            RangeGuards.emitDivOverflowCheck(nAtWidth, dAtWidth, divWidth);
            return B.CreateSDiv(n, d, "sdiv");
        }
        case TokenKind::Mod: {
            auto* d = ToI64(rv, operandIsSigned(*e.Right));
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
            RangeGuards.emitModDivisorCheck(
                CoerceToType(d, modBitsTy, operandIsSigned(*e.Right)),
                modBitsTy->getIntegerBitWidth());
            auto* r = B.CreateSRem(ToI64(lv, operandIsSigned(*e.Left)), d, "srem");
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
            // masked 16-bit one. e.ResolvedType->Width is Sema::checkBinary's
            // Shl/Shr answer -- the LEFT operand's own Width promoted up to
            // at least the dialect default (`Int64Var shl N` stays 64-bit;
            // `ByteVar shl N` promotes to the default, both confirmed against
            // real `fpc -Mtp`) -- so operands are re-coerced to it explicitly
            // rather than trusting whatever width they arrived in.  `l`'s
            // signedness genuinely matters here (a signed left operand's
            // vacated high bits must be sign-extended before the shift, or
            // e.g. `ShortInt(-1) shl 1` comes out positive instead of -2);
            // `r`'s does not (the mask below keeps only r's low `width` bits,
            // which sign- vs. zero-extension never differ on), but is
            // threaded through anyway rather than passed an arbitrary
            // placeholder.
            llvm::Type* bitsTy = llvm::IntegerType::get(
                Ctx, e.ResolvedType ? e.ResolvedType->Width : 64);
            auto* l = CoerceToType(lv, bitsTy, operandIsSigned(*e.Left));
            auto* r = CoerceToType(rv, bitsTy, operandIsSigned(*e.Right));
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
            // isFloatingPointTy, not isDoubleTy: see emitBinary's needFP
            // comment -- a Single (float) operand needs the identical FNeg
            // double already got, not the integer CreateNeg below.
            if (v->getType()->isFloatingPointTy())
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
