#include "CodegenImpl.h"
using namespace plang;

// See NumExprKinds in AstBase.h.
static_assert(NumExprKinds == 16, "a new expression needs a case in emitExpr");

// ====================================================================
// Expression emission — returns llvm::Value* (the computed rvalue)
// ====================================================================

llvm::Value* Codegen::Impl::emitExpr(const ExprNode& e) {
    if (auto* n = llvm::dyn_cast<IntLitExpr>(&e))
        return llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(n->Value), true);

    if (auto* n = llvm::dyn_cast<RealLitExpr>(&e))
        return llvm::ConstantFP::get(dblTy, n->Value);

    if (auto* n = llvm::dyn_cast<BoolLitExpr>(&e))
        return llvm::ConstantInt::getBool(ctx, n->Value);

    if (llvm::dyn_cast<NilExpr>(&e))
        return llvm::ConstantPointerNull::get(ptrTy);

    if (auto* n = llvm::dyn_cast<StringLitExpr>(&e)) {
        // Single-character literal → char value (i8).
        if (n->Value.size() == 1)
            return llvm::ConstantInt::get(i8Ty,
                static_cast<unsigned char>(n->Value[0]));
        // EP: string literals carry VarString type — materialize as a struct so
        // that the invariant "exprIsVarStr → emitExpr returns ptr to struct" holds.
        if (exprIsVarStr(e)) {
            int64_t cap  = (int64_t)n->Value.size();
            auto*   tmp  = createEntryAlloca(strStructType(cap), "str.lit");
            emitStrFromCStr(tmp, cap, internStrPtr(n->Value));
            return tmp;
        }
        return internStrPtr(n->Value);
    }

    if (auto* n = llvm::dyn_cast<IdentExpr>(&e)) {
        // In Pascal, eof and eoln may appear without parentheses.
        // The parser sees them as IdentExpr; route them to the runtime here.
        // A program that declares the name means its own, so what it declared
        // is looked for first (ISO §6.2.2.10).
        {
            std::string lo = toLower(n->Name);
            if ((lo == "eof" || lo == "eoln") && !findVar(n->Name)
                    && !consts.count(lo)) {
                auto* r = builder.CreateCall(
                    getRuntimeBoolFn(lo == "eof" ? "plang_eof_stdin"
                                                 : "plang_eoln_stdin"), {}, lo);
                return ensureI1(r);
            }
        }
        // Function result pseudo-variable (Pascal: assign to function name).
        if (curRetAlloca && toLower(n->Name) == toLower(curFuncName))
            return builder.CreateLoad(curRetType, curRetAlloca, "retval");
        // Variable table.
        auto* ve = findVar(n->Name);
        // Constant table.  A required constant stands only where the program
        // has not declared the name for something of its own: reading `pi`
        // gave 3.14159 in a program whose own `pi` had just been assigned to,
        // since the write went to the variable and the read never looked.
        auto cit = consts.find(toLower(n->Name));
        if (cit != consts.end()
                && !(ve && isRequiredConst(toLower(n->Name))))
            return cit->second;
        // ISO §6.6.3.1 with §6.8.2.2: a parameterless functional parameter
        // named in an expression is a call too.  It has a VarEntry, so it
        // would otherwise be loaded as if it were storage.
        if (ve && ve->isProcParam) {
            if (!ve->procType || !ve->procType->IsFunction)
                codegenICE("procedural parameter '" + n->Name
                           + "' used where a value is required");
            return emitProcParamCall(*ve, {});
        }
        if (!ve && (mod->getFunction(findMangledProc(n->Name))
                    || isImportedCallable(n->Name))) {
            // ISO §6.8.2.2: a parameterless function-identifier in an
            // expression is a call, not a variable.  Route it through the call
            // path so it still gets a static link when it is nested.
            // An imported one has nothing emitted here to recognize it by, so
            // the import table has to say; otherwise it is read as a variable
            // and the link fails on a global that was never a global.
            CallExpr Call;
            Call.Name         = n->Name;
            Call.Loc          = n->Loc;
            Call.ResolvedType = e.ResolvedType;
            return emitCallExpr(Call);
        }
        // Not declared here, so it is a variable imported from a module.
        if (!ve) ve = resolveImportedVar(n->Name, e.ResolvedType.get());
        if (!ve) {
            // Sema must have caught all undefined identifiers before codegen runs.
            std::string Msg = "plang codegen: identifier '" + n->Name
                            + "' not found in scope — Sema missed an undefined-identifier error";
            llvm::report_fatal_error(llvm::StringRef(Msg));
        }
        // VarString: return the struct address directly — callers use it as ptr.
        if (exprIsVarStr(e)) return ve->ptr;
        return builder.CreateLoad(ve->type, ve->ptr, n->Name);
    }

    if (auto* n = llvm::dyn_cast<BinaryExpr>(&e))  return emitBinary(*n);
    if (auto* n = llvm::dyn_cast<UnaryExpr>(&e))   return emitUnary(*n);
    if (auto* n = llvm::dyn_cast<CallExpr>(&e))    return emitCallExpr(*n);
    if (auto* n = llvm::dyn_cast<IndexExpr>(&e))   return emitIndexLoad(*n);
    if (auto* n = llvm::dyn_cast<FieldExpr>(&e))   return emitFieldLoad(*n);
    if (auto* n = llvm::dyn_cast<DerefExpr>(&e))   return emitDerefLoad(*n);
    if (auto* n = llvm::dyn_cast<SetLiteralExpr>(&e)) {
        // Empty set → 0.
        const int64_t base = setBaseOf(*n);
        llvm::Value* result = llvm::ConstantInt::get(setTy(), 0);
        for (const auto& elem : n->Elements) {
            llvm::Value* bits = nullptr;
            if (auto* rng = llvm::dyn_cast<SetRangeExpr>(elem.get()))
                bits = emitSetRange(emitExpr(*rng->Low), emitExpr(*rng->High), base);
            else
                bits = emitSetSingleton(emitExpr(*elem), base);
            result = builder.CreateOr(result, bits, "set");
        }
        return result;
    }
    if (auto* n = llvm::dyn_cast<SubstringExpr>(&e)) {
        // s[i..j] as an rvalue: produce a new string(cap) containing the substring.
        auto* strAddr = emitLValue(*n->Str);
        if (!strAddr) codegenICE("substring applied to a non-addressable operand");
        int64_t cap = 255;
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            for (auto& [nm, ve] : *it)
                if (ve.ptr == strAddr)
                    if (auto* st = llvm::dyn_cast<llvm::StructType>(ve.type))
                        if (st->getNumElements() == 2)
                            if (auto* arr = llvm::dyn_cast<llvm::ArrayType>(st->getElementType(1)))
                                cap = (int64_t)arr->getNumElements();
        auto* resPtr = createEntryAlloca(strStructType(cap), "substr.res");
        auto* low    = toI64(emitExpr(*n->Low));
        auto* high   = toI64(emitExpr(*n->High));
        // s[i..j] names its bounds, the runtime helper takes a count.
        auto* len    = builder.CreateAdd(
            builder.CreateSub(high, low, "substr.span"),
            llvm::ConstantInt::get(i64Ty, 1), "substr.len");
        auto* fn     = getStrFn("plang_str_substr",
            llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, ptrTy, i64Ty, i64Ty, i64Ty});
        builder.CreateCall(fn, {resPtr, llvm::ConstantInt::get(i64Ty, cap),
            strAddr, llvm::ConstantInt::get(i64Ty, cap), low, len});
        return resPtr;
    }
    if (auto* n = llvm::dyn_cast<WriteParam>(&e)) {
        // WriteParam in expression context: just emit the value.
        return emitExpr(*n->Value);
    }

    if (auto* n = llvm::dyn_cast<StructuredValueExpr>(&e))
        return emitStructuredValue(*n);

    codegenICE("unhandled expression node in emitExpr");
}

// Returns the POINTER to the storage for an lvalue expression.
llvm::Value* Codegen::Impl::emitLValue(const ExprNode& e) {
    if (auto* n = llvm::dyn_cast<IdentExpr>(&e)) {
        if (curRetAlloca && toLower(n->Name) == toLower(curFuncName))
            return curRetAlloca;
        auto* ve = findVar(n->Name);
        if (ve) return ve->ptr;
        // A string constant already lives in memory, as the { length, bytes }
        // struct a string value is read through, so it can answer for its own
        // address.  It is the one kind of constant that can: the others are
        // values in registers, and Sema has ruled out writing to any of them.
        if (auto cit = consts.find(toLower(n->Name)); cit != consts.end())
            if (llvm::isa<llvm::GlobalVariable>(cit->second))
                return cit->second;
        // ISO §6.8.2.2: a parameterless function-identifier in an expression
        // is a call.  Reading a component of what it returns needs the same
        // temporary a written-out call's result needs; without this the name
        // is taken for a variable and the link fails on a global that never
        // was one.
        if (mod->getFunction(findMangledProc(n->Name))
                || isImportedCallable(n->Name))
            return spillToTemporary(e);
        // Not declared here, so it is a variable imported from a module.
        if (const auto* iv = resolveImportedVar(n->Name, e.ResolvedType.get()))
            return iv->ptr;
        return nullptr;
    }
    if (auto* n = llvm::dyn_cast<IndexExpr>(&e))  return emitIndexGEP(*n);
    if (auto* n = llvm::dyn_cast<FieldExpr>(&e))  return emitFieldGEP(*n);
    if (auto* n = llvm::dyn_cast<DerefExpr>(&e)) {
        // ISO §6.5.5: f^ is the file's buffer variable, which lives beside the
        // stream rather than at an address the program holds, so it is asked
        // for by name.  Loading the file variable would hand back the handle
        // itself, which is what used to reach the store below.
        if (isFileVar(*n->Pointer)) return fileBufferPtr(*n->Pointer);
        // p^ — load the pointer value; that IS the target address.
        auto* p = emitExpr(*n->Pointer);
        if (p && p->getType()->isPointerTy()) emitNilCheck(p);
        return p;
    }
    if (auto* n = llvm::dyn_cast<SubstringExpr>(&e)) {
        // Substring lvalue s[i..j] — the address of the string variable itself.
        return emitLValue(*n->Str);
    }
    if (llvm::isa<CallExpr>(&e)) return spillToTemporary(e);
    return nullptr;
}

llvm::Value* Codegen::Impl::spillToTemporary(const ExprNode& e) {
    // ISO §6.7.2: a function result is a value and has no place of its own,
    // but reading a component of one — binding(f).bound — needs an address,
    // so it is lent a temporary.  Sema does not allow assignment through it,
    // so the temporary is only ever read.
    llvm::Value* v = emitExpr(e);
    if (!v) return nullptr;
    // A string result is spilled where it is called and hands back the
    // address of that spill, so it already has somewhere to live.
    if (v->getType()->isPointerTy()) return v;
    auto* tmp = createEntryAlloca(v->getType(), "call.result");
    builder.CreateStore(v, tmp);
    return tmp;
}

// ---- Binary expressions ----

llvm::Value* Codegen::Impl::emitBinary(const BinaryExpr& e) {
    // Short-circuit boolean operators.
    if (e.Op == TokenKind::And || e.Op == TokenKind::Or) {
        auto* l = ensureI1(emitExpr(*e.Left));
        auto* r = ensureI1(emitExpr(*e.Right));
        return (e.Op == TokenKind::And)
            ? builder.CreateAnd(l, r, "and")
            : builder.CreateOr(l, r, "or");
    }

    // EP short-circuit: and_then / or_else
    if (e.Op == TokenKind::AndThen || e.Op == TokenKind::OrElse) {
        bool isAnd = e.Op == TokenKind::AndThen;
        auto* lv     = ensureI1(emitExpr(*e.Left));
        auto* rhsBB  = llvm::BasicBlock::Create(ctx, isAnd ? "andthen.rhs" : "orelse.rhs",  curFunc);
        auto* endBB  = llvm::BasicBlock::Create(ctx, isAnd ? "andthen.end" : "orelse.end",  curFunc);
        auto* shortBB= llvm::BasicBlock::Create(ctx, isAnd ? "andthen.skip": "orelse.skip", curFunc);
        // and_then: if left is false, skip right; or_else: if left is true, skip right.
        builder.CreateCondBr(lv, isAnd ? rhsBB : shortBB,
                                 isAnd ? shortBB : rhsBB);
        builder.SetInsertPoint(rhsBB);
        auto* rv = ensureI1(emitExpr(*e.Right));
        builder.CreateBr(endBB);
        auto* fromRhs = builder.GetInsertBlock();
        builder.SetInsertPoint(shortBB);
        builder.CreateBr(endBB);
        builder.SetInsertPoint(endBB);
        auto* phi = builder.CreatePHI(i1Ty, 2, isAnd ? "andthen" : "orelse");
        // and_then shortcut value: false; or_else shortcut value: true
        phi->addIncoming(llvm::ConstantInt::get(i1Ty, isAnd ? 0 : 1), shortBB);
        phi->addIncoming(rv, fromRhs);
        return phi;
    }

    // EP exponentiation: ** and pow  → std::pow (result is real or complex)
    if (e.Op == TokenKind::StarStar || e.Op == TokenKind::Pow) {
        // Check for complex operands via Sema-annotated types.
        bool lCplx = e.Left->ResolvedType
                     && e.Left->ResolvedType->Kind == TypeKind::Complex;
        bool rCplx = e.Right->ResolvedType
                     && e.Right->ResolvedType->Kind == TypeKind::Complex;
        if (lCplx || rCplx) {
            auto* lv = emitExpr(*e.Left);
            auto* rv = emitExpr(*e.Right);
            return emitComplexPow(coerceToComplex(lv), coerceToComplex(rv));
        }
        // EP §6.8.3.2: an integer base with pow keeps an integer result, so it
        // must not make the round trip through double that '**' does.
        if (e.ResolvedType && e.ResolvedType->Kind == TypeKind::Integer) {
            auto* fn = getExternFnN("plang_ipow", i64Ty, {i64Ty, i64Ty});
            return builder.CreateCall(
                fn, {toI64(emitExpr(*e.Left)), toI64(emitExpr(*e.Right))}, "ipow");
        }
        auto* powFn = getExternFnN("pow", dblTy, {dblTy, dblTy});
        auto* base  = toDouble(emitExpr(*e.Left));
        auto* exp   = toDouble(emitExpr(*e.Right));
        return builder.CreateCall(powFn, {base, exp}, "pow");
    }

    // EP §6.8.3.6: string concatenation  a + b
    // Use Sema-annotated type to detect operands and read capacities.
    if (e.Op == TokenKind::Plus && exprIsVarStr(e)) {
        // EP §6.8.3.2 makes a char operand string-compatible, so either side of
        // the concatenation may be one.  The runtime concatenates onto a string,
        // so a char on the left has to become a one-character string first.
        auto strOperand = [&](const ExprNode& x) -> std::pair<llvm::Value*, int64_t> {
            if (exprIsVarStr(x)) return {emitStrAddr(x), exprStrCap(x)};
            auto* v   = emitExpr(x);
            auto* tmp = createEntryAlloca(strStructType(1), "str.chr");
            if (v && v->getType()->isIntegerTy(8)) emitStrFromChar(tmp, 1, v);
            else if (v)                            emitStrFromCStr(tmp, 1, v);
            return {tmp, 1};
        };
        auto [lv, capL] = strOperand(*e.Left);
        int64_t capR   = exprIsVarStr(*e.Right) ? exprStrCap(*e.Right) : 1;
        int64_t capRes = capL + capR;
        auto*   resPtr = createEntryAlloca(strStructType(capRes), "str.concat");
        auto*   rv     = exprIsVarStr(*e.Right) ? emitStrAddr(*e.Right)
                                                : emitExpr(*e.Right);
        if (exprIsVarStr(*e.Right)) {
            auto* fn = getStrFn("plang_str_concat", llvm::Type::getVoidTy(ctx),
                {ptrTy, i64Ty, ptrTy, i64Ty, ptrTy, i64Ty});
            builder.CreateCall(fn, {resPtr, llvm::ConstantInt::get(i64Ty, capRes),
                lv, llvm::ConstantInt::get(i64Ty, capL),
                rv, llvm::ConstantInt::get(i64Ty, capR)});
        } else if (rv && rv->getType()->isIntegerTy(8)) {
            auto* fn = getStrFn("plang_str_concat_char", llvm::Type::getVoidTy(ctx),
                {ptrTy, i64Ty, ptrTy, i64Ty, i8Ty});
            builder.CreateCall(fn, {resPtr, llvm::ConstantInt::get(i64Ty, capRes),
                lv, llvm::ConstantInt::get(i64Ty, capL), rv});
        } else {
            auto* fn = getStrFn("plang_str_concat_cstr", llvm::Type::getVoidTy(ctx),
                {ptrTy, i64Ty, ptrTy, i64Ty, ptrTy});
            builder.CreateCall(fn, {resPtr, llvm::ConstantInt::get(i64Ty, capRes),
                lv, llvm::ConstantInt::get(i64Ty, capL), rv});
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
            auto toStrPtr = [&](const ExprNode& expr) -> std::pair<llvm::Value*, int64_t> {
                if (exprIsVarStr(expr))
                    return {emitStrAddr(expr), exprStrCap(expr)};
                if (exprIsCharStr(expr))
                    return {emitCharStrAsStr(expr), exprCharStrLen(expr)};
                // String literal or char — wrap in a temporary VarString.
                int64_t cap = 1;
                if (auto* sl = llvm::dyn_cast<StringLitExpr>(&expr))
                    cap = (int64_t)sl->Value.size();
                auto* val = emitExpr(expr);
                auto* tmp = createEntryAlloca(strStructType(cap), "str.cmp.tmp");
                if (val && val->getType()->isIntegerTy(8))
                    emitStrFromChar(tmp, cap, val);
                else if (val)
                    emitStrFromCStr(tmp, cap, val);
                return {tmp, cap};
            };
            auto [la, capL] = toStrPtr(*e.Left);
            auto [ra, capR] = toStrPtr(*e.Right);
            auto* fn  = getStrFn(fnName, i8Ty, {ptrTy, i64Ty, ptrTy, i64Ty});
            auto* raw = builder.CreateCall(fn, {la,
                llvm::ConstantInt::get(i64Ty, capL), ra,
                llvm::ConstantInt::get(i64Ty, capR)}, "str.cmp");
            return ensureI1(raw);
        }
    }

    // ISO §6.7.2.4/§6.7.2.5 and EP symmetric difference: intercept before the
    // generic path, whose integer instructions would treat the two bitmasks as
    // numbers.
    if (exprIsSet(*e.Left) && exprIsSet(*e.Right)) {
        auto* a = emitExpr(*e.Left);
        auto* b = emitExpr(*e.Right);
        if (!a || !b) codegenICE("set operator with an unlowerable operand");
        // Both operands are brought into one window before their bits meet.
        // A set-valued result is built in the window of the type Sema gave it,
        // since that is the window whoever receives it will read it in; a
        // comparison has no set type of its own, and the lower of the two
        // origins is chosen there because widening a window never drops a bit.
        const bool ResultIsSet =
            e.ResolvedType && e.ResolvedType->Kind == TypeKind::Set;
        const int64_t Base =
            ResultIsSet ? setBaseOf(e)
                        : std::min(setBaseOf(*e.Left), setBaseOf(*e.Right));
        a = alignSet(a, setBaseOf(*e.Left),  Base);
        b = alignSet(b, setBaseOf(*e.Right), Base);
        if (auto* r = emitSetBinary(e.Op, a, b)) return r;
        codegenICE("unhandled set operator '" + std::string(opSpelling(e.Op)) + "'");
    }

    // ISO §6.7.2.5: membership takes an ordinal on the left, so exprIsSet is
    // only true of the right operand.
    if (e.Op == TokenKind::In)
        return emitSetMember(emitExpr(*e.Left), emitExpr(*e.Right),
                             setBaseOf(*e.Right));

    auto* lv = emitExpr(*e.Left);
    auto* rv = emitExpr(*e.Right);
    if (!lv || !rv) codegenICE("binary operator with an unlowerable operand");

    // EP §6.8.3.2: complex arithmetic — intercept before the scalar path.
    if (lv->getType() == complexTy() || rv->getType() == complexTy()) {
        auto* lc = coerceToComplex(lv);
        auto* rc = coerceToComplex(rv);
        switch (e.Op) {
            case TokenKind::Plus:   return emitComplexAdd(lc, rc);
            case TokenKind::Minus:  return emitComplexSub(lc, rc);
            case TokenKind::Times:  return emitComplexMul(lc, rc);
            case TokenKind::Divide: return emitComplexDiv(lc, rc);
            case TokenKind::Equal: {
                // Component-wise equality.
                auto* req = builder.CreateFCmpOEQ(
                    builder.CreateExtractValue(lc, 0, "l.re"),
                    builder.CreateExtractValue(rc, 0, "r.re"), "re.eq");
                auto* ieq = builder.CreateFCmpOEQ(
                    builder.CreateExtractValue(lc, 1, "l.im"),
                    builder.CreateExtractValue(rc, 1, "r.im"), "im.eq");
                return builder.CreateAnd(req, ieq, "cplx.eq");
            }
            case TokenKind::NotEqual: {
                auto* req = builder.CreateFCmpOEQ(
                    builder.CreateExtractValue(lc, 0, "l.re"),
                    builder.CreateExtractValue(rc, 0, "r.re"), "re.eq");
                auto* ieq = builder.CreateFCmpOEQ(
                    builder.CreateExtractValue(lc, 1, "l.im"),
                    builder.CreateExtractValue(rc, 1, "r.im"), "im.eq");
                auto* both = builder.CreateAnd(req, ieq, "cplx.eq");
                return builder.CreateNot(both, "cplx.ne");
            }
            default:
                codegenICE("unsupported operator on complex operands: "
                           + std::string(kindName(e.Op)));
        }
    }

    // Integer-to-real promotion.
    bool needFP = lv->getType()->isDoubleTy() || rv->getType()->isDoubleTy();
    if (needFP) {
        lv = toDouble(lv);
        rv = toDouble(rv);
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
        lv = coerceToType(lv, wide);
        rv = coerceToType(rv, wide);
    }

    // Either side answers this, the two being compatible by now; the right one
    // is the fallback for an untyped left literal.
    bool uns = ordinalIsUnsigned(e.Left->ResolvedType.get())
               || ordinalIsUnsigned(e.Right->ResolvedType.get());

    switch (e.Op) {
        case TokenKind::Plus:
            return needFP ? builder.CreateFAdd(lv, rv, "fadd")
                          : builder.CreateAdd(lv, rv, "add");
        case TokenKind::Minus:
            return needFP ? builder.CreateFSub(lv, rv, "fsub")
                          : builder.CreateSub(lv, rv, "sub");
        case TokenKind::Times:
            return needFP ? builder.CreateFMul(lv, rv, "fmul")
                          : builder.CreateMul(lv, rv, "mul");
        case TokenKind::Divide:
            return builder.CreateFDiv(toDouble(lv), toDouble(rv), "fdiv");
        case TokenKind::Div: {
            auto* d = toI64(rv);
            emitDivZeroCheck(d, "div");
            return builder.CreateSDiv(toI64(lv), d, "sdiv");
        }
        case TokenKind::Mod: {
            auto* d = toI64(rv);
            emitModDivisorCheck(d);
            // ISO §6.7.2.2 wants 0 <= i mod j < j, but srem takes its sign from
            // the dividend, so (-17) mod 5 comes back as -2 instead of 3.
            auto* r   = builder.CreateSRem(toI64(lv), d, "srem");
            auto* neg = builder.CreateICmpSLT(r, llvm::ConstantInt::get(i64Ty, 0),
                                              "mod.neg");
            return builder.CreateSelect(neg, builder.CreateAdd(r, d, "mod.adj"),
                                        r, "mod");
        }
        case TokenKind::Equal:
            return needFP ? builder.CreateFCmpOEQ(lv, rv, "feq")
                          : builder.CreateICmpEQ(lv, rv, "eq");
        case TokenKind::NotEqual:
            return needFP ? builder.CreateFCmpONE(lv, rv, "fne")
                          : builder.CreateICmpNE(lv, rv, "ne");
        case TokenKind::LessThan:
            return needFP ? builder.CreateFCmpOLT(lv, rv, "flt")
                   : uns   ? builder.CreateICmpULT(lv, rv, "ult")
                           : builder.CreateICmpSLT(lv, rv, "slt");
        case TokenKind::LessThanOrEqual:
            return needFP ? builder.CreateFCmpOLE(lv, rv, "fle")
                   : uns   ? builder.CreateICmpULE(lv, rv, "ule")
                           : builder.CreateICmpSLE(lv, rv, "sle");
        case TokenKind::GreaterThan:
            return needFP ? builder.CreateFCmpOGT(lv, rv, "fgt")
                   : uns   ? builder.CreateICmpUGT(lv, rv, "ugt")
                           : builder.CreateICmpSGT(lv, rv, "sgt");
        case TokenKind::GreaterThanOrEqual:
            return needFP ? builder.CreateFCmpOGE(lv, rv, "fge")
                   : uns   ? builder.CreateICmpUGE(lv, rv, "uge")
                           : builder.CreateICmpSGE(lv, rv, "sge");
        default:
            codegenICE("unhandled binary operator '"
                       + std::string(kindName(e.Op)) + "'");
    }
}

// ====================================================================
// EP §6.4.2.2 / §6.8.3.2: complex arithmetic helpers
// ====================================================================

llvm::Value* Codegen::Impl::emitComplexAdd(llvm::Value* a, llvm::Value* b) {
    auto* ar = builder.CreateExtractValue(a, 0, "a.re");
    auto* ai = builder.CreateExtractValue(a, 1, "a.im");
    auto* br = builder.CreateExtractValue(b, 0, "b.re");
    auto* bi = builder.CreateExtractValue(b, 1, "b.im");
    return makeComplex(builder.CreateFAdd(ar, br, "c.re"),
                       builder.CreateFAdd(ai, bi, "c.im"));
}

llvm::Value* Codegen::Impl::emitComplexSub(llvm::Value* a, llvm::Value* b) {
    auto* ar = builder.CreateExtractValue(a, 0, "a.re");
    auto* ai = builder.CreateExtractValue(a, 1, "a.im");
    auto* br = builder.CreateExtractValue(b, 0, "b.re");
    auto* bi = builder.CreateExtractValue(b, 1, "b.im");
    return makeComplex(builder.CreateFSub(ar, br, "c.re"),
                       builder.CreateFSub(ai, bi, "c.im"));
}

llvm::Value* Codegen::Impl::emitComplexMul(llvm::Value* a, llvm::Value* b) {
    // (ar+ai*i)(br+bi*i) = (ar*br - ai*bi) + (ar*bi + ai*br)*i
    auto* ar = builder.CreateExtractValue(a, 0, "a.re");
    auto* ai = builder.CreateExtractValue(a, 1, "a.im");
    auto* br = builder.CreateExtractValue(b, 0, "b.re");
    auto* bi = builder.CreateExtractValue(b, 1, "b.im");
    auto* rr = builder.CreateFMul(ar, br, "ar.br");
    auto* ii = builder.CreateFMul(ai, bi, "ai.bi");
    auto* ri = builder.CreateFMul(ar, bi, "ar.bi");
    auto* ir = builder.CreateFMul(ai, br, "ai.br");
    return makeComplex(builder.CreateFSub(rr, ii, "c.re"),
                       builder.CreateFAdd(ri, ir, "c.im"));
}

llvm::Value* Codegen::Impl::emitComplexDiv(llvm::Value* a, llvm::Value* b) {
    // (ar+ai*i)/(br+bi*i) = ((ar*br+ai*bi) + (ai*br-ar*bi)*i) / (br^2+bi^2)
    auto* ar = builder.CreateExtractValue(a, 0, "a.re");
    auto* ai = builder.CreateExtractValue(a, 1, "a.im");
    auto* br = builder.CreateExtractValue(b, 0, "b.re");
    auto* bi = builder.CreateExtractValue(b, 1, "b.im");
    auto* denom = builder.CreateFAdd(builder.CreateFMul(br, br, "br2"),
                                      builder.CreateFMul(bi, bi, "bi2"), "denom");
    auto* numRe = builder.CreateFAdd(builder.CreateFMul(ar, br, "ar.br"),
                                      builder.CreateFMul(ai, bi, "ai.bi"), "num.re");
    auto* numIm = builder.CreateFSub(builder.CreateFMul(ai, br, "ai.br"),
                                      builder.CreateFMul(ar, bi, "ar.bi"), "num.im");
    return makeComplex(builder.CreateFDiv(numRe, denom, "c.re"),
                       builder.CreateFDiv(numIm, denom, "c.im"));
}

llvm::Value* Codegen::Impl::callComplexUnary(const std::string& name, llvm::Value* z) {
    // Convention: plang_cXXX_out(double* re_out, double* im_out, double re, double im)
    auto* re_out = createEntryAlloca(dblTy, name + ".re");
    auto* im_out = createEntryAlloca(dblTy, name + ".im");
    auto* re_in  = builder.CreateExtractValue(z, 0, "z.re");
    auto* im_in  = builder.CreateExtractValue(z, 1, "z.im");
    auto* fn = getExternFnN(name, llvm::Type::getVoidTy(ctx),
                             {ptrTy, ptrTy, dblTy, dblTy});
    builder.CreateCall(fn, {re_out, im_out, re_in, im_in});
    auto* re = builder.CreateLoad(dblTy, re_out, "re");
    auto* im = builder.CreateLoad(dblTy, im_out, "im");
    return makeComplex(re, im);
}

llvm::Value* Codegen::Impl::emitComplexPow(llvm::Value* a, llvm::Value* b) {
    // plang_cpow_out(re_out, im_out, are, aim, bre, bim)
    auto* re_out = createEntryAlloca(dblTy, "cpow.re");
    auto* im_out = createEntryAlloca(dblTy, "cpow.im");
    auto* ar = builder.CreateExtractValue(a, 0, "a.re");
    auto* ai = builder.CreateExtractValue(a, 1, "a.im");
    auto* br = builder.CreateExtractValue(b, 0, "b.re");
    auto* bi = builder.CreateExtractValue(b, 1, "b.im");
    auto* fn = getExternFnN("plang_cpow_out", llvm::Type::getVoidTy(ctx),
                             {ptrTy, ptrTy, dblTy, dblTy, dblTy, dblTy});
    builder.CreateCall(fn, {re_out, im_out, ar, ai, br, bi});
    auto* re = builder.CreateLoad(dblTy, re_out, "re");
    auto* im = builder.CreateLoad(dblTy, im_out, "im");
    return makeComplex(re, im);
}

llvm::Value* Codegen::Impl::emitUnary(const UnaryExpr& e) {
    auto* v = emitExpr(*e.Operand);
    if (!v) codegenICE("unary operator with an unlowerable operand");
    switch (e.Op) {
        case TokenKind::Minus:
            // EP §6.4.2.2: unary minus on complex negates both components.
            if (v->getType() == complexTy()) {
                auto* re = builder.CreateExtractValue(v, 0, "neg.re");
                auto* im = builder.CreateExtractValue(v, 1, "neg.im");
                return makeComplex(builder.CreateFNeg(re, "neg.re"),
                                   builder.CreateFNeg(im, "neg.im"));
            }
            if (v->getType()->isDoubleTy())
                return builder.CreateFNeg(v, "fneg");
            return builder.CreateNeg(v, "neg");
        case TokenKind::Not: {
            auto* b = ensureI1(v);
            return builder.CreateNot(b, "not");
        }
        // Unary plus is the identity; anything else passing the operand
        // through unchanged would be the operator quietly going missing.
        case TokenKind::Plus: return v;
        default:
            codegenICE("unhandled unary operator '"
                       + std::string(opSpelling(e.Op)) + "'");
    }
}

llvm::Value* Codegen::Impl::emitCallExpr(const CallExpr& e) {
    std::string lo = toLower(e.Name);

    // ISO §6.2.2.10: a required function identifier may be redeclared, and
    // then it denotes what the program declared and not the required one.  The
    // chain below dispatches on spelling alone, so without this a program that
    // declares its own `abs` calls the required one and never reaches the
    // declared body.  Sema resolved the name in the scope it was written in
    // and is the only thing that knows which won.  This also settles a
    // functional parameter named after a required function, which the check at
    // the head of emitUserFuncCall would otherwise not be reached to make.
    if (e.ResolvedBuiltin == BuiltinID::None) return emitUserFuncCall(e);

    // ---- Math built-ins routed through plang_math.c ----
    if (lo == "sqrt" || lo == "sin" || lo == "cos" || lo == "exp"
        || lo == "ln"  || lo == "arctan") {
        auto* arg = emitExpr(*e.Args[0]);
        // EP §6.7.6.2: dispatch to complex variant when argument is complex.
        if (arg->getType() == complexTy()) {
            // e.g. "sqrt" → "plang_csqrt_out"
            std::string cname = "plang_c" + lo + "_out";
            return callComplexUnary(cname, arg);
        }
        auto* darg = toDouble(arg);
        return builder.CreateCall(getRTMathRR("plang_" + lo), {darg}, lo);
    }
    if (lo == "abs") {
        auto* v = emitExpr(*e.Args[0]);
        // EP §6.7.6.2: abs(complex) → real = sqrt(re² + im²)
        if (v->getType() == complexTy()) {
            auto* re = builder.CreateExtractValue(v, 0, "z.re");
            auto* im = builder.CreateExtractValue(v, 1, "z.im");
            // plang_abs_cplx(re, im) → double
            auto* fn = getExternFnN("plang_abs_cplx", dblTy, {dblTy, dblTy});
            return builder.CreateCall(fn, {re, im}, "abs_cplx");
        }
        if (v->getType()->isDoubleTy())
            return builder.CreateCall(getRTMathRR("plang_abs_real"), {v}, "abs");
        return builder.CreateCall(getRTMathII("plang_abs_int"), {toI64(v)}, "abs");
    }
    if (lo == "sqr") {
        auto* v = emitExpr(*e.Args[0]);
        // EP §6.7.6.2: sqr(complex) → complex = z * z
        if (v->getType() == complexTy())
            return emitComplexMul(v, v);
        if (v->getType()->isDoubleTy())
            return builder.CreateCall(getRTMathRR("plang_sqr_real"), {v}, "sqr");
        return builder.CreateCall(getRTMathII("plang_sqr_int"), {toI64(v)}, "sqr");
    }
    // EP §6.7.6.3: cmplx(x, y) constructor
    if (lo == "cmplx") {
        auto* re = toDouble(emitExpr(*e.Args[0]));
        auto* im = toDouble(emitExpr(*e.Args[1]));
        return makeComplex(re, im);
    }
    // EP §6.7.6.3: polar(r, t) = r*cos(t) + i*r*sin(t)
    if (lo == "polar") {
        auto* r = toDouble(emitExpr(*e.Args[0]));
        auto* t = toDouble(emitExpr(*e.Args[1]));
        auto* cosTh = builder.CreateCall(getRTMathRR("plang_cos"), {t}, "cos_t");
        auto* sinTh = builder.CreateCall(getRTMathRR("plang_sin"), {t}, "sin_t");
        auto* re = builder.CreateFMul(r, cosTh, "polar_re");
        auto* im = builder.CreateFMul(r, sinTh, "polar_im");
        return makeComplex(re, im);
    }
    // EP §6.7.6.2: re(z), im(z), arg(z)
    if (lo == "re") {
        auto* z = emitExpr(*e.Args[0]);
        if (z->getType() == complexTy())
            return builder.CreateExtractValue(z, 0, "re");
        return toDouble(z);
    }
    if (lo == "im") {
        auto* z = emitExpr(*e.Args[0]);
        if (z->getType() == complexTy())
            return builder.CreateExtractValue(z, 1, "im");
        return llvm::ConstantFP::get(dblTy, 0.0);
    }
    if (lo == "arg") {
        auto* z = emitExpr(*e.Args[0]);
        if (z->getType() == complexTy()) {
            auto* re = builder.CreateExtractValue(z, 0, "z.re");
            auto* im = builder.CreateExtractValue(z, 1, "z.im");
            auto* fn = getExternFnN("plang_arg", dblTy, {dblTy, dblTy});
            return builder.CreateCall(fn, {re, im}, "arg");
        }
        // Real/int: arg is 0 for positive, π for negative
        auto* fn = getExternFnN("plang_arg", dblTy, {dblTy, dblTy});
        return builder.CreateCall(fn, {toDouble(z),
            llvm::ConstantFP::get(dblTy, 0.0)}, "arg");
    }
    if (lo == "trunc") {
        auto* arg = toDouble(emitExpr(*e.Args[0]));
        return builder.CreateCall(getRTMathRI("plang_trunc"), {arg}, "trunc");
    }
    if (lo == "round") {
        auto* arg = toDouble(emitExpr(*e.Args[0]));
        return builder.CreateCall(getRTMathRI("plang_round"), {arg}, "round");
    }
    // ---- Boolean file-status built-ins ----
    if (lo == "eof") {
        if (!e.Args.empty() && isFileVar(*e.Args[0])) {
            auto* fp  = fileVarPtr(*e.Args[0]);
            auto* raw = builder.CreateCall(
                getExternFnN("plang_eof_file", i8Ty, {ptrTy}), {fp}, "eof.raw");
            return ensureI1(raw);
        }
        auto* raw = builder.CreateCall(
            getRuntimeBoolFn("plang_eof_stdin"), {}, "eof.raw");
        return ensureI1(raw);
    }
    if (lo == "eoln") {
        if (!e.Args.empty() && isFileVar(*e.Args[0])) {
            auto* fp  = fileVarPtr(*e.Args[0]);
            auto* raw = builder.CreateCall(
                getExternFnN("plang_eoln_file", i8Ty, {ptrTy}), {fp}, "eoln.raw");
            return ensureI1(raw);
        }
        auto* raw = builder.CreateCall(
            getRuntimeBoolFn("plang_eoln_stdin"), {}, "eoln.raw");
        return ensureI1(raw);
    }
    // EP §6.7.6.8: binding(f) → BindingType record
    if (lo == "binding" && !e.Args.empty()) {
        auto* fp  = fileVarPtr(*e.Args[0]);
        auto* out = createEntryAlloca(bindingStructType(), "binding.out");
        builder.CreateStore(llvm::Constant::getNullValue(bindingStructType()), out);
        auto* fn  = getExternFnN("plang_binding",
                                  llvm::Type::getVoidTy(ctx), {ptrTy, ptrTy});
        builder.CreateCall(fn, {fp, out});
        return builder.CreateLoad(bindingStructType(), out, "binding");
    }

    // EP §6.7.6.9: date(t) / time(t) — format TimeStamp to string
    if ((lo == "date" || lo == "time") && !e.Args.empty()) {
        auto* tPtr = emitLValue(*e.Args[0]);
        // 'time' maps to plang_time_ts to avoid clashing with the POSIX time() symbol.
        std::string fnName = (lo == "time") ? "plang_time_ts" : "plang_date";
        auto* fn = getExternFnN(fnName, ptrTy, {ptrTy});
        return builder.CreateCall(fn, {tPtr}, lo);
    }

    // EP §6.7.6.6: position(f) / lastposition(f)
    if ((lo == "position" || lo == "lastposition") && !e.Args.empty()) {
        auto* fp  = fileVarPtr(*e.Args[0]);
        int64_t esz = getFileElemSize(*e.Args[0]);
        auto* fn  = getExternFnN("plang_" + lo, i64Ty, {ptrTy, i64Ty});
        return builder.CreateCall(fn, {fp, llvm::ConstantInt::get(i64Ty, esz)}, lo);
    }
    // EP §6.7.6.5: empty(f)
    if (lo == "empty" && !e.Args.empty()) {
        auto* fp  = fileVarPtr(*e.Args[0]);
        int64_t esz = getFileElemSize(*e.Args[0]);
        auto* fn  = getExternFnN("plang_empty", i8Ty, {ptrTy, i64Ty});
        auto* raw = builder.CreateCall(fn, {fp, llvm::ConstantInt::get(i64Ty, esz)}, "empty.raw");
        return builder.CreateICmpNE(raw, llvm::ConstantInt::get(i8Ty, 0), "empty");
    }

    // ---- Ordinal built-ins — simple enough to keep inline ----
    if (lo == "ord") {
        auto* v = emitExpr(*e.Args[0]);
        if (v->getType()->isIntegerTy(64)) return v;
        return builder.CreateZExt(v, i64Ty, "ord");
    }
    if (lo == "chr") {
        return builder.CreateTrunc(toI64(emitExpr(*e.Args[0])), i8Ty, "chr");
    }
    if (lo == "odd") {
        auto* v   = toI64(emitExpr(*e.Args[0]));
        auto* bit = builder.CreateAnd(v, llvm::ConstantInt::get(i64Ty, 1), "odd.bit");
        return builder.CreateICmpNE(bit, llvm::ConstantInt::get(i64Ty, 0), "odd");
    }
    if (lo == "succ" || lo == "pred") {
        auto* arg = emitExpr(*e.Args[0]);
        auto* v = toI64(arg);
        auto* k = e.Args.size() > 1
            ? toI64(emitExpr(*e.Args[1]))
            : llvm::ConstantInt::get(i64Ty, 1);
        auto* r = lo == "succ" ? builder.CreateAdd(v, k, "succ")
                               : builder.CreateSub(v, k, "pred");
        // ISO §6.6.6.4: the result is of the argument's type.  The arithmetic
        // is done wide, so it has to come back in the width that type is held
        // in, or a boolean result is an i64 that write puts out as 1 and 0.
        return arg && arg->getType() != i64Ty
            ? builder.CreateZExtOrTrunc(r, arg->getType(), lo)
            : r;
    }
    if (lo == "card") {
        // Cardinality of a set (bit population count).
        auto* v   = toSetWidth(emitExpr(*e.Args[0]));
        auto* fn  = llvm::Intrinsic::getOrInsertDeclaration(
                        mod.get(), llvm::Intrinsic::ctpop, {setTy()});
        auto* n   = builder.CreateCall(fn, {v}, "card");
        return builder.CreateZExtOrTrunc(n, i64Ty, "card.i64");
    }

    // ---- EP string functions (§6.7.6.7) ----
    // Return (ptr, cap) for a string argument using Sema-annotated type.
    auto getStrArgPtr = [&](int idx) -> std::pair<llvm::Value*, int64_t> {
        if (e.Args.size() <= (size_t)idx) return {nullptr, 0};
        const auto& arg = *e.Args[idx];
        if (exprIsVarStr(arg))
            return {emitStrAddr(arg), exprStrCap(arg)};
        // String literal — create a temp VarString.
        if (auto* sl = llvm::dyn_cast<StringLitExpr>(&arg)) {
            int64_t cap = (int64_t)sl->Value.size();
            auto* tmp = createEntryAlloca(strStructType(cap), "str.arg");
            emitStrFromCStr(tmp, cap, internStrPtr(sl->Value));
            return {tmp, cap};
        }
        return {nullptr, 0};
    };
    if (lo == "length") {
        auto [ptr, cap] = getStrArgPtr(0);
        if (ptr) {
            auto* fn = getStrFn("plang_str_length", i64Ty, {ptrTy, i64Ty});
            return builder.CreateCall(fn,
                {ptr, llvm::ConstantInt::get(i64Ty, cap)}, "length");
        }
        // Fallback: strlen on a char*
        auto* s  = emitExpr(*e.Args[0]);
        auto* fn = getExternFnN("strlen", i64Ty, {ptrTy});
        return builder.CreateCall(fn, {s}, "length");
    }
    if (lo == "index") {
        auto [sp, sc] = getStrArgPtr(0);
        auto [pp, pc] = getStrArgPtr(1);
        if (sp && pp) {
            auto* fn = getStrFn("plang_str_index", i64Ty,
                {ptrTy, i64Ty, ptrTy, i64Ty});
            return builder.CreateCall(fn,
                {sp, llvm::ConstantInt::get(i64Ty, sc),
                 pp, llvm::ConstantInt::get(i64Ty, pc)}, "index");
        }
    }
    if (lo == "substr") {
        auto [sp, sc] = getStrArgPtr(0);
        if (sp) {
            // EP §6.7.5.4: the third argument is how many characters to take,
            // not where to stop.  Omitting it means the rest of the string.
            auto* i = toI64(emitExpr(*e.Args[1]));
            auto* n = e.Args.size() > 2
                ? toI64(emitExpr(*e.Args[2]))
                : builder.CreateAdd(
                      builder.CreateSub(strLoadLen(sp), i, "substr.rest"),
                      llvm::ConstantInt::get(i64Ty, 1), "substr.len");
            auto* resPtr = createEntryAlloca(strStructType(sc), "substr.res");
            auto* fn     = getStrFn("plang_str_substr",
                llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, ptrTy, i64Ty, i64Ty, i64Ty});
            builder.CreateCall(fn,
                {resPtr, llvm::ConstantInt::get(i64Ty, sc),
                 sp,     llvm::ConstantInt::get(i64Ty, sc), i, n});
            return resPtr;
        }
    }
    if (lo == "trim") {
        auto [sp, sc] = getStrArgPtr(0);
        if (sp) {
            auto* resPtr = createEntryAlloca(strStructType(sc), "trim.res");
            auto* fn     = getStrFn("plang_str_trim",
                llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, ptrTy, i64Ty});
            builder.CreateCall(fn,
                {resPtr, llvm::ConstantInt::get(i64Ty, sc),
                 sp,     llvm::ConstantInt::get(i64Ty, sc)});
            return resPtr;
        }
    }
    // EP string comparison functions: EQ NE LT GT LE GE
    {
        static const std::unordered_map<std::string,const char*> strCmpFns = {
            {"eq","plang_str_eq"},{"ne","plang_str_ne"},{"lt","plang_str_lt"},
            {"gt","plang_str_gt"},{"le","plang_str_le"},{"ge","plang_str_ge"},
        };
        auto it = strCmpFns.find(lo);
        if (it != strCmpFns.end() && e.Args.size() >= 2) {
            auto [lp, lc] = getStrArgPtr(0);
            auto [rp, rc] = getStrArgPtr(1);
            if (lp && rp) {
                auto* fn  = getStrFn(it->second, i8Ty, {ptrTy, i64Ty, ptrTy, i64Ty});
                auto* raw = builder.CreateCall(fn,
                    {lp, llvm::ConstantInt::get(i64Ty, lc),
                     rp, llvm::ConstantInt::get(i64Ty, rc)}, lo);
                return ensureI1(raw);
            }
        }
    }

    return emitUserFuncCall(e);
}

llvm::Value* Codegen::Impl::emitUserFuncCall(const CallExpr& e) {
    // ISO §6.6.3.1: a functional parameter is called through the pair it
    // arrived as, so there is no name to resolve.
    if (auto* pve = findVar(e.Name); pve && pve->isProcParam)
        return emitProcParamCall(*pve, e.Args);

    // User-defined function — walk the nesting hierarchy.
    std::string mangledName = findMangledProc(e.Name);
    auto* callee = mod->getFunction(mangledName);
    if (!callee) {
        // The function is not defined in this compilation unit; it must come
        // from a separately compiled module.  Create an external declaration
        // using the LLVM types derived from the Sema-resolved call-site types.
        llvm::Type* retLLVMTy = llvm::Type::getVoidTy(ctx);
        if (e.ResolvedType && !e.ResolvedType->isError()) {
            retLLVMTy = llvmTypeOfSemaType(*e.ResolvedType);
        }
        std::vector<llvm::Type*> paramTys;
        for (const auto& Arg : e.Args) {
            if (Arg && Arg->ResolvedType && !Arg->ResolvedType->isError())
                paramTys.push_back(llvmTypeOfSemaType(*Arg->ResolvedType));
            else
                paramTys.push_back(i64Ty); // safe fallback
        }
        auto* fnTy = llvm::FunctionType::get(retLLVMTy, paramTys, false);
        callee = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage,
                                        mangledName, mod.get());
    }

    std::vector<llvm::Value*> args;

    // Nested function call: build the callee's static-link frame from its
    // recorded outer variable list, so the slot order matches the definition.
    if (auto* frame = buildStaticLinkFrame(mangledName)) args.push_back(frame);

    // EP §6.7.3.7: look up conformant param dimensions for this callee.
    const std::vector<std::vector<std::pair<std::string,std::string>>>* cDims = nullptr;
    {
        auto cit = conformantParamDims_.find(mangledName);
        if (cit != conformantParamDims_.end())
            cDims = &cit->second;
    }

    size_t pi = args.size();
    for (size_t astArgIdx = 0; astArgIdx < e.Args.size(); ++astArgIdx) {
        const auto& arg = e.Args[astArgIdx];

        // ISO §6.6.3.1: procedural param — entry point plus its frame.
        if (const auto* pt = procParamArg(mangledName, astArgIdx)) {
            pushProcParamArgs(args, *arg, *pt);
            pi = args.size();
            continue;
        }

        // EP §6.4.7: schema param — body pointer plus its discriminants.
        if (unsigned nd = schemaArgDiscs(mangledName, astArgIdx); nd > 0) {
            pushSchemaArgs(args, *arg, nd);
            pi = args.size();
            continue;
        }

        bool isConformant = cDims && astArgIdx < cDims->size()
                            && !(*cDims)[astArgIdx].empty();

        if (isConformant) {
            const size_t dims = (*cDims)[astArgIdx].size();
            pushConformantArgs(args, *arg, dims);
            pi += 1 + 2 * dims;
        } else {
            args.push_back(alignSetArg(
                emitCallArg(*arg,
                    pi < callee->arg_size()
                        ? callee->getFunctionType()->getParamType(pi) : nullptr,
                    paramIsByRef(mangledName, astArgIdx)),
                *arg, mangledName, astArgIdx));
            ++pi;
        }
    }
    auto* ret = builder.CreateCall(callee, args, "call");
    // A string result comes back as the whole { length, bytes } struct, but
    // every consumer of a string expression expects its address.  Spill the
    // returned value so the result of f reads like any other string.
    if (exprIsVarStr(e) && ret->getType()->isStructTy()) {
        auto* tmp = createEntryAlloca(ret->getType(), "str.ret");
        builder.CreateStore(ret, tmp);
        return tmp;
    }
    return ret;
}

// ---- Array / record / pointer lvalue helpers ----

llvm::Value* Codegen::Impl::emitConformantElemPtr(const IndexExpr& e) {
    // Walk down to the name being subscripted, collecting the subscripts on
    // the way so that they come back outermost first.
    std::vector<const ExprNode*> subs{e.Index.get()};
    const ExprNode* base = e.Array.get();
    while (auto* inner = llvm::dyn_cast<IndexExpr>(base)) {
        subs.push_back(inner->Index.get());
        base = inner->Array.get();
    }
    auto* id = llvm::dyn_cast<IdentExpr>(base);
    if (!id) return nullptr;
    const VarEntry* ve = findVar(id->Name);
    if (!ve || !ve->isConformantArray) return nullptr;
    std::reverse(subs.begin(), subs.end());

    // The bounds are ordinary integer variables in this activation, put there
    // by the prologue from the hidden arguments.
    auto boundOf = [&](const std::string& name) -> llvm::Value* {
        auto* bv = findVar(name);
        return bv ? builder.CreateLoad(i64Ty, bv->ptr, "conf.bound") : nullptr;
    };
    auto extentOf = [&](size_t d) -> llvm::Value* {
        if (d >= ve->conformantDims.size()) return nullptr;
        auto* lo = boundOf(ve->conformantDims[d].first);
        auto* hi = boundOf(ve->conformantDims[d].second);
        if (!lo || !hi) return nullptr;
        return builder.CreateAdd(builder.CreateSub(hi, lo, "conf.span"),
                                 llvm::ConstantInt::get(i64Ty, 1), "conf.ext");
    };

    // The array is one flat block, so the subscripts fold together the way a
    // row-major layout reads: each one scales what came before it by the width
    // of its own dimension.
    llvm::Value* flat = llvm::ConstantInt::get(i64Ty, 0);
    const size_t dims = ve->conformantDims.empty() ? 1 : ve->conformantDims.size();
    for (size_t d = 0; d < subs.size(); ++d) {
        auto* idx = toI64(emitExpr(*subs[d]));
        if (d < ve->conformantDims.size())
            if (auto* lo = boundOf(ve->conformantDims[d].first))
                idx = builder.CreateSub(idx, lo, "idx.adj.conf");
        if (d > 0)
            if (auto* ext = extentOf(d))
                flat = builder.CreateMul(flat, ext, "conf.row");
        flat = builder.CreateAdd(flat, idx, "conf.off");
    }
    // A subscript short of the last dimension names a row rather than an
    // element, and a row is as wide as the dimensions still to come.
    for (size_t d = subs.size(); d < dims; ++d)
        if (auto* ext = extentOf(d))
            flat = builder.CreateMul(flat, ext, "conf.row");

    llvm::Type* elemTy = ve->conformantElemTy ? ve->conformantElemTy : i64Ty;
    return builder.CreateGEP(elemTy, ve->ptr, {flat}, "elem.ptr");
}

llvm::Value* Codegen::Impl::emitIndexGEP(const IndexExpr& e) {
    // EP §6.4.7: an undiscriminated schema recomputes its bounds from the
    // discriminants it carries, then indexes like a conformant array.
    if (auto ref = schemaRefOf(*e.Array)) {
        auto [lo, hi] = schemaArrayBounds(*ref);
        auto* elemTy  = schemaStorageType(*ref);
        auto* idx     = toI64(emitExpr(*e.Index));
        emitRangeCheckDyn(idx, lo, hi, /*isIndex=*/true, e.Loc);
        idx = builder.CreateSub(idx, lo, "idx.adj.sch");
        return builder.CreateGEP(elemTy, ref->data, {idx}, "elem.ptr");
    }

    // EP §6.5.3.2: s[i] selects the i'th character, counting from 1 and running
    // to the string's current length rather than to its capacity.
    if (exprIsVarStr(*e.Array)) {
        auto* strPtr = emitStrAddr(*e.Array);
        auto* idx    = toI64(emitExpr(*e.Index));
        if (rangeChecksAt(e.Loc)) {
            auto* len   = strLoadLen(strPtr);
            auto* one   = llvm::ConstantInt::get(i64Ty, 1);
            auto* bad   = builder.CreateOr(
                builder.CreateICmpSLT(idx, one,  "str.rng.lo"),
                builder.CreateICmpSGT(idx, len,  "str.rng.hi"), "str.rng.bad");
            emitGuard(bad, "strbounds", [&] {
                builder.CreateCall(
                    getExternFnN("plang_err_str_index",
                                 llvm::Type::getVoidTy(ctx), {i64Ty, i64Ty}),
                    {idx, len});
            });
        }
        auto* zeroBased = builder.CreateSub(idx,
            llvm::ConstantInt::get(i64Ty, 1), "str.idx");
        return builder.CreateGEP(i8Ty, strDataPtr(strPtr), {zeroBased},
                                 "str.elem.ptr");
    }

    // EP §6.7.3.7: conformant array.  Every dimension's bounds only exist at
    // run time, so the whole subscript chain is flattened together here rather
    // than one subscript at a time.
    if (auto* conf = emitConformantElemPtr(e)) return conf;

    auto* ve = [&]() -> const VarEntry* {
        if (auto* id = llvm::dyn_cast<IdentExpr>(e.Array.get()))
            return findVar(id->Name);
        return nullptr;
    }();

    auto* idx = toI64(emitExpr(*e.Index));
    auto* arrPtr = ve ? ve->ptr : emitLValue(*e.Array);

    llvm::Type* arrTy  = nullptr;
    llvm::Type* elemTy = i64Ty;
    // Try to get array type info from the typenode embedded in the ident.
    if (auto* id = llvm::dyn_cast<IdentExpr>(e.Array.get())) {
        auto* ve2 = findVar(id->Name);
        if (ve2) {
            if (llvm::isa<llvm::ArrayType>(ve2->type)) {
                arrTy  = ve2->type;
                elemTy = llvm::cast<llvm::ArrayType>(arrTy)->getElementType();
            } else {
                elemTy = ve2->type;
            }
        }
    }
    // Subtract the lower bound so Pascal array [lo..hi] maps to LLVM [0..hi-lo].
    int64_t Low = 0;
    // The declaration is the better source of the bounds, but a name with no
    // variable behind it — a parameterless function returning an array — has
    // none, and then the Sema type is all there is.  Falling through was what
    // this used to do only for an operand that was not a name at all, so
    // `ramp[3]` indexed with no element type and no bounds at all.
    const VarEntry* declVe = nullptr;
    if (auto* id2 = llvm::dyn_cast<IdentExpr>(e.Array.get()))
        declVe = findVar(id2->Name);
    if (declVe) {
        if (auto* atn = llvm::dyn_cast_or_null<ArrayTypeNode>(declVe->typeNode)) {
            Low = arrayIndexLow(*atn);
        } else if (auto* stn = llvm::dyn_cast_or_null<SchemaTypeNode>(declVe->typeNode)) {
            // EP §6.4.7: schema instance — read lower bound from resolved body type.
            if (stn->ResolvedBody && stn->ResolvedBody->SchemaBody
                    && stn->ResolvedBody->SchemaBody->Kind == TypeKind::Array
                    && stn->ResolvedBody->SchemaBody->IndexType)
                Low = stn->ResolvedBody->SchemaBody->IndexType->SubLo;
        } else if (auto* ntn = llvm::dyn_cast_or_null<NamedTypeNode>(declVe->typeNode)) {
            // Named type alias (e.g. var r: Row where Row = array[1..5] of ...).
            // Look through typeAliases to find the underlying ArrayTypeNode.
            auto it = typeAliases.find(toLower(ntn->Name));
            if (it != typeAliases.end())
                if (auto* atn2 = llvm::dyn_cast<ArrayTypeNode>(it->second))
                    Low = arrayIndexLow(*atn2);
        }
    } else if (e.Array->ResolvedType) {
        // Nested indexing A[1][2], or anything else with no declaration to
        // read: use the Sema type for the element type and the lower bound.
        const Type* T = e.Array->ResolvedType.get();
        if (T->Kind == TypeKind::SchemaInstance && T->SchemaBody)
            T = T->SchemaBody.get();
        if (T->Kind == TypeKind::Array) {
            if (T->ElemType && !T->ElemType->isError())
                elemTy = llvmTypeOfSemaType(*T->ElemType);
            if (T->IndexType)
                Low = T->IndexType->SubLo;
            // The extent has to be recovered here as well, or the bounds check
            // below has nothing to test against.  Every dimension after the
            // first indexes an expression rather than a name, so leaving this
            // null let a[1][i] run off the end of the inner array unchecked —
            // and with a[i, j] abbreviating exactly that, it is the ordinary
            // way to reach a multi-dimensional array.
            arrTy = llvmTypeOfSemaType(*T);
        }
    }
    // Check before the lower-bound adjustment so the reported value and range
    // are the ones the source actually wrote.
    if (auto* at = llvm::dyn_cast_or_null<llvm::ArrayType>(arrTy)) {
        auto n = static_cast<int64_t>(at->getNumElements());
        if (n > 0) emitRangeCheck(idx, Low, Low + n - 1, /*isIndex=*/true, e.Loc);
    }
    if (Low != 0)
        idx = builder.CreateSub(idx, llvm::ConstantInt::get(i64Ty, Low), "idx.adj");

    if (!arrTy) {
        auto* ep = builder.CreateGEP(elemTy, arrPtr, {idx}, "elem.ptr");
        return ep;
    }

    auto* ep = builder.CreateGEP(arrTy, arrPtr,
                   {llvm::ConstantInt::get(i64Ty, 0), idx}, "elem.ptr");
    return ep;
}

llvm::Value* Codegen::Impl::emitIndexLoad(const IndexExpr& e) {
    auto* ptr = emitIndexGEP(e);
    // EP §6.5.3.2: a string component is a char.
    if (exprIsVarStr(*e.Array))
        return builder.CreateLoad(i8Ty, ptr, "str.elem");
    llvm::Type* elemTy = i64Ty;
    // EP §6.4.7: the element type of a schematic array comes from Sema; the
    // variable entry holds only the untyped body pointer.
    if (e.Array->ResolvedType && e.Array->ResolvedType->Kind == TypeKind::Schema) {
        const plang::Type* T = e.ResolvedType.get();
        if (!T || T->isError())
            codegenICE("indexing a schematic variable produced no element type");
        return builder.CreateLoad(llvmTypeOfSemaType(*T), ptr, "elem");
    }
    if (auto* id = llvm::dyn_cast<IdentExpr>(e.Array.get())) {
        auto* ve = findVar(id->Name);
        if (ve) {
            // EP §6.7.3.7: for conformant arrays, use conformantElemTy.
            if (ve->isConformantArray && ve->conformantElemTy)
                elemTy = ve->conformantElemTy;
            else if (llvm::isa<llvm::ArrayType>(ve->type))
                elemTy = llvm::cast<llvm::ArrayType>(ve->type)->getElementType();
            else
                elemTy = ve->type;
        }
    } else if (e.ResolvedType) {
        // Non-IdentExpr array (e.g. nested A[1][2]): use Sema-annotated element type.
        const Type* T = e.ResolvedType.get();
        if (T->Kind == TypeKind::SchemaInstance && T->SchemaBody)
            T = T->SchemaBody.get();
        if (!T->isError())
            elemTy = llvmTypeOfSemaType(*T);
    }
    return builder.CreateLoad(elemTy, ptr, "elem");
}

/// The record type a field expression selects from, looking through a schema
/// to the body that actually has the fields (EP §6.4.7).
static const Type* recordTypeOf(const ExprNode& recExpr) {
    const Type* T = recExpr.ResolvedType.get();
    if (!T) return nullptr;
    if ((T->Kind == TypeKind::SchemaInstance || T->Kind == TypeKind::Schema)
            && T->SchemaBody)
        T = T->SchemaBody.get();
    return T->Kind == TypeKind::Record ? T : nullptr;
}

/// Resolve the LLVM struct element index for a named field, using the
/// Sema-annotated record type on the expression.  Returns nothing when the
/// field cannot be matched; callers must not fall back to index 0, which
/// would read or write the wrong field.
///
/// This is the fallback for a record type with no declaration to lay out from,
/// where the struct was built by walking the same flattened field list.
static std::optional<unsigned> fieldStructIndex(const ExprNode& recExpr,
                                                 const std::string& fieldName,
                                                 llvm::StructType* st) {
    const Type* RecTy = recordTypeOf(recExpr);
    if (!RecTy || !st) return std::nullopt;

    unsigned Idx = 0;
    for (const auto& F : RecTy->RecordFields) {
        if (Idx >= st->getNumElements()) break;
        if (eqCI(F.Name, fieldName)) return Idx;
        ++Idx;
    }
    return std::nullopt;
}

/// Resolve the LLVM struct type for the record in a field expression.
/// Handles both  r.field  (r is a record variable)  and
///              p^.field  (p is a pointer to a record).
llvm::StructType* Codegen::Impl::resolveRecordStructType(const FieldExpr& e) {
    // EP §6.4.7: a schematic record body has a fixed layout (Sema rejects the
    // varying non-array case), so the struct comes straight from the body type.
    if (e.Record->ResolvedType
            && e.Record->ResolvedType->Kind == TypeKind::Schema
            && e.Record->ResolvedType->SchemaBody)
        return llvm::dyn_cast<llvm::StructType>(
                   llvmTypeOfSemaType(*e.Record->ResolvedType->SchemaBody));

    // Case 1: r.field — r is a direct record variable.
    if (auto* id = llvm::dyn_cast<IdentExpr>(e.Record.get()))
        if (auto* ve = findVar(id->Name))
            if (auto* st = llvm::dyn_cast<llvm::StructType>(ve->type))
                return st;

    // Case 2: p^.field — p is a pointer variable; get the pointee struct type.
    // The pointer typeNode may be PointerTypeNode directly, or a NamedTypeNode
    // that is a type alias resolving to a PointerTypeNode (e.g. recptr = ^rec).
    if (auto* deref = llvm::dyn_cast<DerefExpr>(e.Record.get())) {
        if (auto* innerID = llvm::dyn_cast<IdentExpr>(deref->Pointer.get())) {
            if (auto* ve = findVar(innerID->Name)) {
                // Direct: var p: ^Rec
                if (auto* ptn = llvm::dyn_cast_or_null<PointerTypeNode>(ve->typeNode))
                    return llvm::dyn_cast<llvm::StructType>(llvmTypeOfNode(*ptn->Base));
                // Via alias: type PtrRec = ^Rec;  var p: PtrRec
                if (auto* ntn = llvm::dyn_cast_or_null<NamedTypeNode>(ve->typeNode)) {
                    auto it = typeAliases.find(toLower(ntn->Name));
                    if (it != typeAliases.end())
                        if (auto* ptn2 = llvm::dyn_cast<PointerTypeNode>(it->second))
                            return llvm::dyn_cast<llvm::StructType>(
                                llvmTypeOfNode(*ptn2->Base));
                }
            }
        }
    }

    // Case 3: Sema-annotated record type → look up via typeAliases.
    if (e.Record->ResolvedType
            && e.Record->ResolvedType->Kind == TypeKind::Record) {
        auto it = typeAliases.find(toLower(e.Record->ResolvedType->Name));
        if (it != typeAliases.end())
            if (auto* rtn = llvm::dyn_cast<RecordTypeNode>(it->second))
                return structTypeFor(*rtn);
        // Case 4: records reached through an index or a call have no variable
        // entry and may be anonymous, so build the struct from the Sema type.
        // The layout matches, which is all a GEP needs.
        return llvm::dyn_cast<llvm::StructType>(
                   llvmTypeOfSemaType(*e.Record->ResolvedType));
    }
    return nullptr;
}

llvm::Type* Codegen::Impl::fieldLlvmType(const FieldExpr& e) {
    // A field of a variant is one of several sharing a single struct element,
    // so the element's type is the storage they share and not the field's own.
    if (const Type* RecTy = recordTypeOf(*e.Record)) {
        if (const auto* L = layoutOfRecord(*RecTy)) {
            auto It = L->Fields.find(toLower(e.Field));
            if (It != L->Fields.end() && It->second.Ty) return It->second.Ty;
        }
    }
    if (auto* st = resolveRecordStructType(e)) {
        auto Idx = fieldStructIndex(*e.Record, e.Field, st);
        if (Idx && *Idx < st->getNumElements()) return st->getElementType(*Idx);
    }
    // The address of this field was worked out by the same two routes, so
    // reaching here means the load is about to read storage of a shape nobody
    // could name.  Reading it as an integer would take the first eight bytes of
    // whatever is there and call the answer a number.
    codegenICE("field '" + e.Field + "' has no type that either its record's "
               "declaration or Sema can give");
}

llvm::Value* Codegen::Impl::emitFieldGEP(const FieldExpr& e) {
    // EP §6.4.7: for p^ the body starts past the discriminant header, so the
    // record pointer has to come from the schematic view rather than emitLValue.
    llvm::Value* recPtr = nullptr;
    if (e.Record->ResolvedType
            && e.Record->ResolvedType->Kind == TypeKind::Schema) {
        if (auto ref = schemaRefOf(*e.Record)) recPtr = ref->data;
    }
    if (!recPtr) recPtr = emitLValue(*e.Record);
    if (!recPtr) return nullptr;

    // Returning recPtr unchanged here would silently alias the whole record,
    // so both failures below are hard errors.
    auto* st = resolveRecordStructType(e);
    if (!st) codegenICE("cannot resolve the record type of field '" + e.Field + "'");
    auto* zero = llvm::ConstantInt::get(i32Ty, 0);

    // The declaration knows which fields share storage; the flattened field
    // list below does not, so it is only the fallback for a record that has no
    // declaration to consult.
    if (const Type* RecTy = recordTypeOf(*e.Record)) {
        if (const auto* L = layoutOfRecord(*RecTy)) {
            auto It = L->Fields.find(toLower(e.Field));
            if (It == L->Fields.end())
                codegenICE("record has no field named '" + e.Field + "'");
            const auto& P = It->second;
            auto* ep = builder.CreateGEP(
                st, recPtr, {zero, llvm::ConstantInt::get(i32Ty, P.Index)},
                P.InVariant ? "variant.ptr" : "field.ptr");
            if (!P.InVariant || P.Offset == 0) return ep;
            return builder.CreateConstGEP1_64(i8Ty, ep, P.Offset, "field.ptr");
        }
    }

    auto FieldIdx = fieldStructIndex(*e.Record, e.Field, st);
    if (!FieldIdx) codegenICE("record has no field named '" + e.Field + "'");
    auto* fidx = llvm::ConstantInt::get(i32Ty, *FieldIdx);
    return builder.CreateGEP(st, recPtr, {zero, fidx}, "field.ptr");
}

llvm::Value* Codegen::Impl::emitFieldLoad(const FieldExpr& e) {
    // EP §6.8.4: a schema-discriminant, constant for a discriminated instance
    // and carried with the value for an undiscriminated one.
    if (e.Record->ResolvedType
            && e.Record->ResolvedType->Kind == TypeKind::SchemaInstance) {
        for (const auto& D : e.Record->ResolvedType->SchemaDiscs) {
            if (eqCI(D.Name, e.Field))
                return llvm::ConstantInt::get(i64Ty,
                           static_cast<uint64_t>(D.Value), /*isSigned=*/true);
        }
        // Not a discriminant — fall through to normal field access on the body.
    }
    if (e.Record->ResolvedType
            && e.Record->ResolvedType->Kind == TypeKind::Schema) {
        if (auto ref = schemaRefOf(*e.Record)) {
            const auto& discs = ref->semaTy->SchemaDiscs;
            for (size_t i = 0; i < discs.size() && i < ref->discs.size(); ++i) {
                if (!eqCI(discs[i].Name, e.Field)) continue;
                // Discriminants travel as i64; narrow to the declared ordinal
                // type so that char and enum discriminants print correctly.
                llvm::Type* want = discs[i].Ty && !discs[i].Ty->isError()
                                       ? llvmTypeOfSemaType(*discs[i].Ty) : i64Ty;
                return want->isIntegerTy() && want != i64Ty
                           ? builder.CreateTrunc(ref->discs[i], want, "sch.disc.n")
                           : ref->discs[i];
            }
        }
    }

    auto* ptr = emitFieldGEP(e);
    if (!ptr) codegenICE("field access '" + e.Field + "' on a non-record operand");

    return builder.CreateLoad(fieldLlvmType(e), ptr, "field");
}

llvm::Value* Codegen::Impl::emitDerefLoad(const DerefExpr& e) {
    // ISO §6.5.5: reading f^ reads the file's buffer variable, which the
    // runtime fills from the component at the current position.
    llvm::Value* ptrVal = isFileVar(*e.Pointer) ? fileBufferPtr(*e.Pointer)
                                                : emitExpr(*e.Pointer);
    if (!ptrVal) codegenICE("dereference of an unlowerable pointer expression");
    if (ptrVal->getType()->isPointerTy() && !isFileVar(*e.Pointer))
        emitNilCheck(ptrVal);

    // Determine the pointee type from the Sema annotation on the deref
    // expression.  A record written through a name has a struct already built
    // from its declaration, and reusing it keeps p^.f and q.f agreeing on
    // field order; everything else follows from the type alone.
    llvm::Type* loadTy = i64Ty;
    if (e.ResolvedType) {
        if (e.ResolvedType->Kind == TypeKind::Record) {
            auto it = typeAliases.find(toLower(e.ResolvedType->Name));
            if (it != typeAliases.end())
                if (auto* rtn = llvm::dyn_cast<RecordTypeNode>(it->second))
                    loadTy = structTypeFor(*rtn);
                else
                    loadTy = llvmTypeOfSemaType(*e.ResolvedType);
            else
                loadTy = llvmTypeOfSemaType(*e.ResolvedType);
        } else {
            loadTy = llvmTypeOfSemaType(*e.ResolvedType);
        }
    }
    return builder.CreateLoad(loadTy, ptrVal, "deref");
}

// ====================================================================
// Type coercion helpers
// ====================================================================

llvm::Value* Codegen::Impl::ensureI1(llvm::Value* v) {
    if (!v) codegenICE("boolean conversion of an unlowerable expression");
    if (v->getType()->isIntegerTy(1)) return v;
    // Truncate any integer to i1.
    return builder.CreateTrunc(v, i1Ty, "to.i1");
}

llvm::Value* Codegen::Impl::toDouble(llvm::Value* v) {
    if (!v) codegenICE("real conversion of an unlowerable expression");
    if (v->getType()->isDoubleTy()) return v;
    return builder.CreateSIToFP(v, dblTy, "to.dbl");
}

llvm::Value* Codegen::Impl::toI64(llvm::Value* v) {
    if (!v) codegenICE("integer conversion of an unlowerable expression");
    if (v->getType()->isIntegerTy(64)) return v;
    if (v->getType()->isDoubleTy())
        return builder.CreateFPToSI(v, i64Ty, "to.i64");
    return builder.CreateZExt(v, i64Ty, "to.i64");
}

llvm::Value* Codegen::Impl::coerceToType(llvm::Value* v, llvm::Type* dst) {
    if (!v || v->getType() == dst) return v;
    if (dst->isDoubleTy() && v->getType()->isIntegerTy())
        return builder.CreateSIToFP(v, dblTy, "widen");
    if (dst->isIntegerTy() && v->getType()->isDoubleTy())
        return builder.CreateFPToSI(v, dst, "narrow");
    // Ordinals of different widths meet whenever a char or boolean is stored
    // where an integer was computed, or the reverse.  Zero-extension is the
    // right widening: the narrow ordinals all have non-negative values.
    if (dst->isIntegerTy() && v->getType()->isIntegerTy())
        return builder.CreateZExtOrTrunc(v, dst, "conv");
    return v;
}

// ====================================================================
// EP §6.8.7: Structured value constructor emission
// ====================================================================

// EP §6.4.1: the denoter a value is a value of, with any names it is written
// through followed to the declaration that gives its shape.
const TypeNode* Codegen::Impl::denoterOf(const TypeNode* tn) const {
    for (int hops = 0; tn && hops < 32; ++hops) {
        auto* named = llvm::dyn_cast<NamedTypeNode>(tn);
        if (!named) return tn;
        auto it = typeAliases.find(toLower(named->Name));
        if (it == typeAliases.end() || it->second == tn) return tn;
        tn = it->second;
    }
    return tn;
}

// The denoter written for a named field, looked for among the fixed fields
// and then through the variants, which declare fields of their own.
const TypeNode* Codegen::Impl::fieldDenoter(const RecordTypeNode& rtn,
                                            std::string_view name) {
    auto inSections = [&](const std::vector<FieldDecl>& fields) -> const TypeNode* {
        for (const auto& fd : fields)
            for (const auto& n : fd.Names)
                if (toLower(n) == toLower(std::string(name))) return fd.Type.get();
        return nullptr;
    };
    if (auto* t = inSections(rtn.Fields)) return t;
    for (const VariantPart* vp = rtn.Variant.get(); vp; ) {
        const VariantPart* next = nullptr;
        for (const auto& c : vp->Cases) {
            if (auto* t = inSections(c.Fields)) return t;
            if (!next) next = c.NestedVariant.get();
        }
        vp = next;
    }
    return nullptr;
}

llvm::Value* Codegen::Impl::emitStructuredValue(const StructuredValueExpr& e,
                                                 const TypeNode* denoter) {
    if (!e.ResolvedType) return llvm::ConstantInt::get(i64Ty, 0);

    // EP §6.8.7.1: written with a type name, that name says which declaration
    // gives the bounds and the fields; written without one — as a
    // component-value is — the denoter it stands for was handed in.
    const TypeNode* shape = denoterOf(denoter);
    if (!e.TypeName.empty())
        if (auto it = typeAliases.find(toLower(e.TypeName)); it != typeAliases.end())
            shape = denoterOf(it->second);

    // ---- Set constructor with type prefix: emit as bitmask ----
    if (e.ResolvedType->Kind == TypeKind::Set) {
        const int64_t base = setBaseOf(e);
        llvm::Value* result = llvm::ConstantInt::get(setTy(), 0);
        for (const auto& arm : e.Arms) {
            for (const auto& lbl : arm.Labels) {
                llvm::Value* bits = nullptr;
                if (auto* rng = llvm::dyn_cast<SetRangeExpr>(lbl.get()))
                    bits = emitSetRange(emitExpr(*rng->Low), emitExpr(*rng->High),
                                        base);
                else
                    bits = emitSetSingleton(emitExpr(*lbl), base);
                if (bits) result = builder.CreateOr(result, bits, "set");
            }
            // Arms with Values in a set constructor are unusual but tolerated.
            if (arm.Value) (void)emitExpr(*arm.Value);
        }
        return result;
    }

    // ---- Array constructor ----
    if (e.ResolvedType->Kind == TypeKind::Array) {
        // Handing back an integer for an array used to be the answer here, and
        // an array of no elements the answer below: between them every value
        // written in the constructor was dropped and the result was a
        // zero-filled aggregate of the wrong shape.
        auto* atn = llvm::dyn_cast_or_null<ArrayTypeNode>(shape);
        if (!atn)
            codegenICE("array constructor has no array declaration to take its "
                       "bounds and element type from");

        auto range = arrayIndexRange(*atn);
        if (!range)
            codegenICE("array constructor has bounds that neither folded nor "
                       "Sema can give");
        const int64_t lo    = range->first;
        const int64_t hi    = range->second;
        const int64_t count = (hi >= lo) ? (hi - lo + 1) : 0;

        auto* arrTy = llvm::ArrayType::get(llvmTypeOfNode(*atn->Element),
                                            static_cast<uint64_t>(count));
        auto* elemTy = arrTy->getElementType();
        auto* alloca = createEntryAlloca(arrTy, "arr.ctor");
        builder.CreateStore(llvm::Constant::getNullValue(arrTy), alloca);

        // Helper: store val at element index idx (0-based after adjusting by lo).
        auto storeAt = [&](int64_t idx, llvm::Value* val) {
            if (idx < lo || idx > hi) return;
            auto* gep = builder.CreateGEP(arrTy, alloca,
                {llvm::ConstantInt::get(i64Ty, 0),
                 llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(idx - lo))},
                "arr.ctor.e");
            // An element that is itself a structure arrives as the address of
            // one, there being no register that holds it.
            if (elemTy->isAggregateType() && val->getType()->isPointerTy()) {
                builder.CreateMemCpy(gep, llvm::MaybeAlign(),
                                     val, llvm::MaybeAlign(),
                                     mod->getDataLayout().getTypeAllocSize(elemTy));
                return;
            }
            builder.CreateStore(coerceToType(val, elemTy), gep);
        };

        // EP §6.8.7.2: 'otherwise' fills all unspecified indices.
        // Process 'otherwise' arms first (default values), then explicit arms
        // override them.  This matches the spec even when 'otherwise' appears
        // before explicit arms in the source.
        // A component-value of an element names no type either, so the
        // element's denoter goes with it.
        auto emitArmValue = [&](const ExprNode& v) {
            if (auto* sv = llvm::dyn_cast<StructuredValueExpr>(&v);
                    sv && sv->TypeName.empty())
                return emitStructuredValue(*sv, atn->Element.get());
            return emitExpr(v);
        };

        for (const auto& arm : e.Arms) {
            if (!arm.IsOtherwise) continue;
            auto* val = emitArmValue(*arm.Value);
            for (int64_t i = lo; i <= hi; ++i) storeAt(i, val);
        }
        for (const auto& arm : e.Arms) {
            if (arm.IsOtherwise) continue;
            auto* val = emitArmValue(*arm.Value);
            // EP §6.8.7.2: an index in a constructor is a constant, so one that
            // will not fold is not an index this can place.  Standing in a
            // bound for it put the value at the end of the array, or outside it
            // where storeAt drops it and the element keeps the zero it started
            // with — either way somewhere the source never said.
            auto labelIndex = [&](const ExprNode& lbl) {
                auto v = tryEvalConstInt(lbl, &consts);
                if (!v) codegenICE("array constructor has an index that is not "
                                   "a constant this can work out");
                return *v;
            };
            for (const auto& lbl : arm.Labels) {
                if (auto* rng = llvm::dyn_cast<SetRangeExpr>(lbl.get())) {
                    const int64_t rlo = labelIndex(*rng->Low);
                    const int64_t rhi = labelIndex(*rng->High);
                    for (int64_t i = rlo; i <= rhi; ++i) storeAt(i, val);
                } else {
                    storeAt(labelIndex(*lbl), val);
                }
            }
        }
        return alloca; // caller uses memcpy or memcpy-like assign
    }

    // ---- Record constructor ----
    if (e.ResolvedType->Kind == TypeKind::Record) {
        auto* rtn = llvm::dyn_cast_or_null<RecordTypeNode>(shape);
        if (!rtn)
            codegenICE("record constructor has no record declaration to take "
                       "its fields from");

        // The layout, rather than a map built here from the fixed fields: it
        // covers the tag and the variants too, which were silently dropped.
        const auto& L      = layoutOf(*rtn);
        auto*       st     = L.Ty;
        auto*       alloca = createEntryAlloca(st, "rec.ctor");
        builder.CreateStore(llvm::Constant::getNullValue(st), alloca);

        for (const auto& arm : e.Arms) {
            for (const auto& lbl : arm.Labels) {
                auto* id = llvm::dyn_cast<IdentExpr>(lbl.get());
                if (!id) continue;
                auto fit = L.Fields.find(toLower(id->Name));
                if (fit == L.Fields.end()) continue;
                const auto& P = fit->second;
                if (P.Index >= st->getNumElements()) continue;
                // A field's own value is written bare as well, so the field's
                // denoter is what says what shape it has.
                llvm::Value* val = nullptr;
                if (auto* sv = llvm::dyn_cast<StructuredValueExpr>(arm.Value.get());
                        sv && sv->TypeName.empty())
                    val = emitStructuredValue(*sv, fieldDenoter(*rtn, id->Name));
                else
                    val = emitExpr(*arm.Value);
                llvm::Value* gep = builder.CreateGEP(st, alloca,
                    {llvm::ConstantInt::get(i32Ty, 0),
                     llvm::ConstantInt::get(i32Ty, P.Index)},
                    "rec.ctor.f");
                if (P.InVariant && P.Offset != 0)
                    gep = builder.CreateConstGEP1_64(i8Ty, gep, P.Offset,
                                                      "rec.ctor.f");
                if (P.Ty->isAggregateType() && val->getType()->isPointerTy()) {
                    builder.CreateMemCpy(gep, llvm::MaybeAlign(),
                                         val, llvm::MaybeAlign(),
                                         mod->getDataLayout().getTypeAllocSize(P.Ty));
                    continue;
                }
                builder.CreateStore(coerceToType(val, P.Ty), gep);
            }
        }
        return alloca;
    }

    return llvm::ConstantInt::get(i64Ty, 0); // fallback
}
