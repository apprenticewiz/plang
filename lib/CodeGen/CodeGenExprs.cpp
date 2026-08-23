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
