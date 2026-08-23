#include "CodeGenImpl.h"
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
            // ISO §6.2.2.10: a program that declares the name means its own.
            // Sema settled that in the scope the name was written in and says
            // so on the node; codegen used to guess by asking which of its own
            // tables held the spelling, and a user-declared parameterless
            // FUNCTION called eof was in none of them.  The builtin won, and
            // because the builtin reads standard input a program whose own eof
            // never touches a file HUNG on a terminal.
            if ((lo == "eof" || lo == "eoln") && !n->UserDeclared) {
                auto* r = builder.CreateCall(
                    getRuntimeBoolFn(lo == "eof" ? "plang_eof_stdin"
                                                 : "plang_eoln_stdin"), {}, lo);
                return ensureI1(r);
            }
        }
        // Function result pseudo-variable (Pascal: assign to function name).
        if (curRetAlloca && toLower(n->Name) == toLower(curFuncName)
                && !boundInsideFunction(n->Name))
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
    // EP §6.4.3.3: a string(n) is carried by its ADDRESS -- every caller that
    // takes one expects a pointer to the { length, bytes } struct, which is
    // what the IdentExpr branch above hands back.  That contract held for an
    // identifier and nothing else, so a string reached as a field, an element
    // or a dereference was loaded by VALUE here instead: passing r.s to a
    // `string(25)` parameter loaded a { i64, [20 x i8] } and failed IR
    // verification, and every other caller of the contract had the same hole.
    if (exprIsVarStr(e)
            && (llvm::isa<IndexExpr>(&e) || llvm::isa<FieldExpr>(&e)
                || llvm::isa<DerefExpr>(&e)))
        if (auto* p = emitLValue(e)) return p;

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
        // The capacity is a property of the operand's type, and Sema has it.
        // This used to hunt for it by scanning every scope for a variable whose
        // address was object-identical to the one just emitted, defaulting to
        // 255 -- which is an identifier lookup by another route.  A field, an
        // element or a dereference is a GEP that matches no entry, so its
        // substring was silently truncated to 255 characters.
        //
        // The scan was not sound even when it did match.  A record whose first
        // field is a string has the same address as the record, so the scan
        // found the RECORD and read a capacity off whatever its second element
        // happened to be: in `record s: string(20); t: array[1..5] of char end`,
        // r.s[1..10] came back five characters long, its capacity taken from t.
        //
        // The assignment path a few lines away in CodeGenStmts already asks
        // exprStrCap.  Only the rvalue did this.
        // Two different capacities, and conflating them cut a
        // discriminant-sized string's substring to one character: the result
        // TEMPORARY has to be sized by a constant, while what the runtime is
        // told about the SOURCE is the capacity that source really has.
        int64_t cap = exprStrCapStatic(*n->Str);
        if (cap <= 0) cap = PlangMaxStringCapacity;
        // The source capacity falls back to the same widest-capacity answer
        // when the operand is not typed as a string(n); exprStrCapV reports 0
        // there, and telling the runtime the source holds nothing put every
        // substring of one outside its own bounds.
        auto* srcCap = exprIsVarStr(*n->Str) ? exprStrCapV(*n->Str) : i64c(cap);
        auto* resPtr = createEntryAlloca(strStructType(cap), "substr.res");
        auto* low    = toI64(emitExpr(*n->Low));
        auto* high   = toI64(emitExpr(*n->High));
        // s[i..j] names its bounds, the runtime helper takes a count.
        auto* len    = builder.CreateAdd(
            builder.CreateSub(high, low, "substr.span"),
            llvm::ConstantInt::get(i64Ty, 1), "substr.len");
        auto* fn     = getStrFn("plang_str_substr",
            llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, ptrTy, i64Ty, i64Ty, i64Ty});
        builder.CreateCall(fn, {resPtr, i64c(cap), strAddr, srcCap, low, len});
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
        if (curRetAlloca && toLower(n->Name) == toLower(curFuncName)
                && !boundInsideFunction(n->Name))
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
        // EP §6.7.5.3: new(p, d..) writes the discriminants into a header in
        // FRONT of the body, so what p holds is not the address of p^.  Two
        // places answer "where is p^'s storage" -- this one and schemaRefOf --
        // and they differed by the header size, so `q^ := 'first'` for a
        // ^string wrote the length field over the capacity discriminant and
        // the NEXT assignment was checked against the previous string's
        // length.  Asked of the pointer, not of the dereference, because for a
        // string body p^ reads as the string and no longer says "schema".
        if (const auto& PT = n->Pointer->ResolvedType;
                PT && PT->Kind == TypeKind::Pointer && PT->PointeeType
                && PT->PointeeType->Kind == TypeKind::Schema)
            if (auto ref = schemaRefOf(*n)) return ref->data;
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
        // EP §6.8.3.2: "a factor of the form x**y shall be an error if x is
        // zero and y is less than or equal to zero" -- shared with `pow`,
        // whose integer path checks it above (plang_ipow) but whose OWN
        // real-base path did not; and, for `**` only (a real or integer, so
        // non-complex, base), "an error if x is negative" -- x**y is
        // exp(y*ln(x)), and ln has no real value at or below zero.  Neither
        // was checked, so std::pow's own C99 conventions answered instead:
        // 0.0**0.0 is 1 by its any**0 rule, (-2.0)**2.0 by its extension for
        // an integral exponent.
        auto* zero    = llvm::ConstantFP::get(dblTy, 0.0);
        auto* baseLT0 = builder.CreateFCmpOLT(base, zero, "pow.base.lt0");
        auto* expLE0  = builder.CreateFCmpOLE(exp,  zero, "pow.exp.le0");
        auto* zeroZero = builder.CreateAnd(
            builder.CreateFCmpOEQ(base, zero, "pow.base.eq0"), expLE0, "pow.00");
        // `**`: base < 0 (any exponent) OR base = 0 with exponent <= 0.
        // `pow`'s real-base path (a real or complex-mixed base -- the
        // integer*integer case took the ipow branch above): base = 0 with
        // exponent <= 0 only, since pow's recursive definition is well-formed
        // for a negative base.
        auto* bad = e.Op == TokenKind::StarStar
            ? builder.CreateOr(baseLT0, zeroZero, "pow.bad")
            : zeroZero;
        emitGuard(bad, "pow.domain", [&] {
            builder.CreateCall(
                getExternFnN("plang_err_pow_domain", llvm::Type::getVoidTy(ctx),
                             {dblTy, dblTy}),
                {base, exp});
        });
        return builder.CreateCall(powFn, {base, exp}, "pow");
    }

    // EP §6.8.3.6: string concatenation  a + b
    // Use Sema-annotated type to detect operands and read capacities.
    if (e.Op == TokenKind::Plus && exprIsVarStr(e)) {
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
            if (exprIsVarStr(x)) return strAddrAndCap(x);
            if (exprIsCharStr(x))
                return {emitCharStrAsStr(x), i64c(exprCharStrLen(x))};
            auto* v   = emitExpr(x);
            auto* tmp = createEntryAlloca(strStructType(1), "str.chr");
            if (v && v->getType()->isIntegerTy(8)) emitStrFromChar(tmp, 1, v);
            else if (v)                            emitStrFromCStr(tmp, 1, v);
            return {tmp, i64c(1)};
        };
        auto [lv, capL] = strOperand(*e.Left);
        auto* capR = exprIsVarStr(*e.Right) ? exprStrCapV(*e.Right)
                   : exprIsCharStr(*e.Right) ? i64c(exprCharStrLen(*e.Right))
                   : i64c(1);
        // A non-string operand is one character, as it was before: returning
        // zero for it sized the result temporary at 1 and cut 'x' + 'y' to "x".
        auto staticCap = [&](const ExprNode& x) -> int64_t {
            if (exprIsVarStr(x)) return exprStrCapStatic(x);
            if (exprIsCharStr(x)) return exprCharStrLen(x);
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
        auto varies = [](const ExprNode& x) {
            return exprIsVarStr(x) && x.ResolvedType->ExtentVaries;
        };
        const bool capVaries = varies(*e.Left) || varies(*e.Right);
        const int64_t capRes = staticCap(*e.Left) + staticCap(*e.Right);
        llvm::Value* capResV = capVaries
            ? builder.CreateAdd(capL, capR, "concat.cap") : i64c(capRes);
        llvm::Value* resPtr  = capVaries
            ? createDynStrAlloca(capResV, "str.concat")
            : static_cast<llvm::Value*>(
                  createEntryAlloca(strStructType(capRes), "str.concat"));
        auto*   rv     = exprIsVarStr(*e.Right)  ? emitStrAddr(*e.Right)
                        : exprIsCharStr(*e.Right) ? emitCharStrAsStr(*e.Right)
                                                : emitExpr(*e.Right);
        if (exprIsVarStr(*e.Right) || exprIsCharStr(*e.Right)) {
            auto* fn = getStrFn("plang_str_concat", llvm::Type::getVoidTy(ctx),
                {ptrTy, i64Ty, ptrTy, i64Ty, ptrTy, i64Ty});
            builder.CreateCall(fn, {resPtr, capResV, lv, capL, rv, capR});
        } else if (rv && rv->getType()->isIntegerTy(8)) {
            auto* fn = getStrFn("plang_str_concat_char", llvm::Type::getVoidTy(ctx),
                {ptrTy, i64Ty, ptrTy, i64Ty, i8Ty});
            builder.CreateCall(fn, {resPtr, capResV, lv, capL, rv});
        } else {
            auto* fn = getStrFn("plang_str_concat_cstr", llvm::Type::getVoidTy(ctx),
                {ptrTy, i64Ty, ptrTy, i64Ty, ptrTy});
            builder.CreateCall(fn, {resPtr, capResV, lv, capL, rv});
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
                if (exprIsVarStr(expr)) return strAddrAndCap(expr);
                if (exprIsCharStr(expr))
                    return {emitCharStrAsStr(expr), i64c(exprCharStrLen(expr))};
                // String literal or char — wrap in a temporary VarString.
                int64_t cap = 1;
                if (auto* sl = llvm::dyn_cast<StringLitExpr>(&expr))
                    cap = (int64_t)sl->Value.size();
                auto* val = emitExpr(expr);
                auto* tmp = createEntryAlloca(strStructType(cap), "str.cmp.tmp");
                if (val && val->getType()->isIntegerTy(8))
                    emitStrFromChar(tmp, cap, val);
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
                    emitStrFromCStr(tmp, cap, val);
                }
                return {tmp, i64c(cap)};
            };
            auto [la, capL] = toStrPtr(*e.Left);
            auto [ra, capR] = toStrPtr(*e.Right);
            auto* fn  = getStrFn(fnName, i8Ty, {ptrTy, i64Ty, ptrTy, i64Ty});
            auto* raw = builder.CreateCall(fn, {la, capL, ra, capR}, "str.cmp");
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
    return complexOps_->emitComplexAdd(a, b);
}

llvm::Value* Codegen::Impl::emitComplexSub(llvm::Value* a, llvm::Value* b) {
    return complexOps_->emitComplexSub(a, b);
}

llvm::Value* Codegen::Impl::emitComplexMul(llvm::Value* a, llvm::Value* b) {
    return complexOps_->emitComplexMul(a, b);
}

llvm::Value* Codegen::Impl::emitComplexDiv(llvm::Value* a, llvm::Value* b) {
    return complexOps_->emitComplexDiv(a, b);
}

llvm::Value* Codegen::Impl::callComplexUnary(const std::string& name, llvm::Value* z) {
    return complexOps_->callComplexUnary(name, z);
}

llvm::Value* Codegen::Impl::emitComplexPow(llvm::Value* a, llvm::Value* b) {
    return complexOps_->emitComplexPow(a, b);
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

    // EP §6.7.6.6: position(f) / lastposition(f).  Both report a value of
    // the file's declared INDEX TYPE -- "position(f) = succ(a, ...)", a
    // being the index type's smallest value -- not a 0-based component
    // count, so a `file[1..5]` fully written reports 4, not 3.
    if ((lo == "position" || lo == "lastposition") && !e.Args.empty()) {
        auto* fp  = fileVarPtr(*e.Args[0]);
        int64_t esz = getFileElemSize(*e.Args[0]);
        int64_t ilo = getFileIndexLow(*e.Args[0]);
        auto* fn  = getExternFnN("plang_" + lo, i64Ty, {ptrTy, i64Ty, i64Ty});
        return builder.CreateCall(fn, {fp, llvm::ConstantInt::get(i64Ty, esz),
                                       llvm::ConstantInt::get(i64Ty, ilo)}, lo);
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
        // ISO §6.7.6.4: "The function shall yield a value whose ordinal
        // number is ord(x)+k, if such a value exists.  It shall be an error
        // if such a value does not exist" -- checked nowhere, so
        // succ(blue) one past the last value of a 3-member enum silently
        // wrapped into 3, an ordinal no value of the type has.  integer is
        // deliberately excluded (ordinalRange's own rule): it has no bounded
        // range to have walked off the end of.
        if (const auto& argTy = e.Args[0]->ResolvedType; argTy && !argTy->isError())
            if (auto range = ordinalRange(*argTy))
                emitRangeCheck(r, range->first, range->second, /*isIndex=*/false,
                              e.Loc);
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
    auto getStrArgPtr = [&](int idx) -> std::pair<llvm::Value*, llvm::Value*> {
        if (e.Args.size() <= (size_t)idx) return {nullptr, nullptr};
        const auto& arg = *e.Args[idx];
        if (exprIsVarStr(arg)) return strAddrAndCap(arg);
        // ISO §6.4.3.2's other string shape — a packed array[1..n] of char —
        // is the sibling comparison already widens for (exprIsStringLike,
        // above); length/substr/trim/index only asked exprIsVarStr and fell
        // through to a raw-strlen fallback that read the whole array as an
        // i64-sized value and passed it where a pointer was wanted, an LLVM
        // IR verifier failure, or (for substr/trim/index) to a runtime symbol
        // codegen never emits.
        if (exprIsCharStr(arg))
            return {emitCharStrAsStr(arg), i64c(exprCharStrLen(arg))};
        // String literal — create a temp VarString.
        if (auto* sl = llvm::dyn_cast<StringLitExpr>(&arg)) {
            int64_t cap = (int64_t)sl->Value.size();
            auto* tmp = createEntryAlloca(strStructType(cap), "str.arg");
            emitStrFromCStr(tmp, cap, internStrPtr(sl->Value));
            return {tmp, i64c(cap)};
        }
        // A bare char (a variable, not a literal): EP §6.7.6.7 accepts
        // char-type as readily as a string-type for EQ/LT/GT/NE/LE/GE, and
        // this had no case for it at all -- Sema's own check (SemaExpr.cpp)
        // accepts a char argument, and nothing here built the temporary
        // VarString it needs to be compared as, so the call reached nothing
        // and fell through to a link failure exactly like an outright
        // rejected argument would have.
        if (arg.ResolvedType && arg.ResolvedType->Kind == TypeKind::Char) {
            auto* v   = emitExpr(arg);
            auto* tmp = createEntryAlloca(strStructType(1), "str.arg.chr");
            if (v) emitStrFromChar(tmp, 1, v);
            return {tmp, i64c(1)};
        }
        return {nullptr, nullptr};
    };
    // For sizing a temporary, which needs a constant; see exprStrCapStatic.
    auto strArgCapStatic = [&](int idx) -> int64_t {
        if (e.Args.size() <= (size_t)idx) return 0;
        const auto& arg = *e.Args[idx];
        if (exprIsVarStr(arg)) return exprStrCapStatic(arg);
        if (exprIsCharStr(arg)) return exprCharStrLen(arg);
        if (auto* sl = llvm::dyn_cast<StringLitExpr>(&arg))
            return (int64_t)sl->Value.size();
        if (arg.ResolvedType && arg.ResolvedType->Kind == TypeKind::Char) return 1;
        return 0;
    };
    /// A result temporary as wide as the argument it is derived from.  Where a
    /// discriminant fixes that argument's capacity the width is not a constant,
    /// and exprStrCapStatic's 255 is the answer for a capacity nobody knows --
    /// so sizing by it cut substr and trim of a 400-capacity string to 255.
    auto strResultTemp = [&](int idx, llvm::Value* capV, const char* name)
            -> std::pair<llvm::Value*, llvm::Value*> {
        if (const auto& arg = *e.Args[idx];
                exprIsVarStr(arg) && arg.ResolvedType->ExtentVaries)
            return {createDynStrAlloca(capV, name), capV};
        const int64_t c = strArgCapStatic(idx);
        return {createEntryAlloca(strStructType(c), name), i64c(c)};
    };
    if (lo == "length") {
        auto [ptr, cap] = getStrArgPtr(0);
        if (ptr) {
            auto* fn = getStrFn("plang_str_length", i64Ty, {ptrTy, i64Ty});
            return builder.CreateCall(fn, {ptr, cap}, "length");
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
            return builder.CreateCall(fn, {sp, sc, pp, pc}, "index");
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
            auto [resPtr, resCapV] = strResultTemp(0, sc, "substr.res");
            auto* fn     = getStrFn("plang_str_substr",
                llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, ptrTy, i64Ty, i64Ty, i64Ty});
            builder.CreateCall(fn, {resPtr, resCapV, sp, sc, i, n});
            return resPtr;
        }
    }
    if (lo == "trim") {
        auto [sp, sc] = getStrArgPtr(0);
        if (sp) {
            auto [resPtr, resCapV] = strResultTemp(0, sc, "trim.res");
            auto* fn     = getStrFn("plang_str_trim",
                llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, ptrTy, i64Ty});
            builder.CreateCall(fn, {resPtr, resCapV, sp, sc});
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
                auto* raw = builder.CreateCall(fn, {lp, lc, rp, rc}, lo);
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
    const std::vector<ParamMeta>* pMeta = nullptr;
    {
        auto cit = paramMeta_.find(mangledName);
        if (cit != paramMeta_.end())
            pMeta = &cit->second;
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

        bool isConformant = pMeta && astArgIdx < pMeta->size()
                            && !(*pMeta)[astArgIdx].conformantDims.empty();

        if (isConformant) {
            const size_t dims = (*pMeta)[astArgIdx].conformantDims.size();
            pushConformantArgs(args, *arg, dims);
            pi += 1 + 2 * dims;
        } else {
            std::optional<int64_t> destSetBase;
            if (pMeta && astArgIdx < pMeta->size())
                destSetBase = (*pMeta)[astArgIdx].setBase;
            args.push_back(setOps_->alignSetArg(
                emitCallArg(*arg,
                    pi < callee->arg_size()
                        ? callee->getFunctionType()->getParamType(pi) : nullptr,
                    paramIsByRef(mangledName, astArgIdx)),
                *arg, destSetBase));
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
