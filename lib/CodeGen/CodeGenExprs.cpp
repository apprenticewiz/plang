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

// ====================================================================
// EP §6.4.2.2 / §6.8.3.2: complex arithmetic helpers
// ====================================================================
//
// emitComplexAdd/Sub/Div/Pow moved to CGBinaryOps (their only caller was
// emitBinary); emitComplexMul and callComplexUnary stay here as
// forwarders since emitCallExpr (still in this file) also calls them.

llvm::Value* Codegen::Impl::emitComplexMul(llvm::Value* a, llvm::Value* b) {
    return complexOps_->emitComplexMul(a, b);
}

llvm::Value* Codegen::Impl::callComplexUnary(const std::string& name, llvm::Value* z) {
    return complexOps_->callComplexUnary(name, z);
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
