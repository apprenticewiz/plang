#include "CGExprCore.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"

using namespace plang;

llvm::Value* CGExprCore::emitExpr(const ExprNode& e) {
    // See MaxExprDepth in CGExprCore.h. Checked before the RAII bump: a caller
    // already sitting at the ceiling must abort without recursing again.
    if (ExprDepth_ >= MaxExprDepth)
        codegenICE("expression nesting exceeds CodeGen's depth ceiling; "
                    "this is a hard recursion limit of this build's "
                    "expression emitter, not a diagnostic on the input "
                    "program");
    ExprDepthScope DepthGuard(ExprDepth_);

    if (auto* n = llvm::dyn_cast<IntLitExpr>(&e))
        return llvm::ConstantInt::get(I64Ty, static_cast<uint64_t>(n->Value), true);

    if (auto* n = llvm::dyn_cast<RealLitExpr>(&e))
        return llvm::ConstantFP::get(DblTy, n->Value);

    if (auto* n = llvm::dyn_cast<BoolLitExpr>(&e))
        return llvm::ConstantInt::getBool(Ctx, n->Value);

    if (llvm::dyn_cast<NilExpr>(&e))
        return llvm::ConstantPointerNull::get(PtrTy);

    if (auto* n = llvm::dyn_cast<StringLitExpr>(&e)) {
        // Single-character literal → char value (i8).
        if (n->Value.size() == 1)
            return llvm::ConstantInt::get(I8Ty,
                static_cast<unsigned char>(n->Value[0]));
        // EP: string literals carry VarString type — materialize as a struct so
        // that the invariant "exprIsVarStr → emitExpr returns ptr to struct" holds.
        if (ExprIsVarStr(e)) {
            int64_t cap  = (int64_t)n->Value.size();
            auto*   tmp  = CreateEntryAlloca(Types.strStructType(cap), "str.lit");
            Strings.emitStrFromBytes(tmp, i64c(cap), Strings.internStrPtr(n->Value),
                                     i64c(cap));
            return tmp;
        }
        return Strings.internStrPtr(n->Value);
    }

    if (auto* n = llvm::dyn_cast<IdentExpr>(&e)) {
        // Turbo procedural VALUES: Sema (checkRoutineValue) has already
        // decided this exact occurrence names a non-nested, non-parameter
        // routine used for ITS OWN VALUE -- the direct operand of `@`, or
        // the direct RHS of an assignment to a procedural variable -- not a
        // call.  Ahead of every other IdentExpr case in this function,
        // including the ordinary "bare function identifier is an implicit
        // call" ones below: those apply to how a name reads in an ordinary
        // expression, and this one is not that; it is the one syntactic
        // position where a bare routine name is a reference instead.  What
        // it produces is the routine's own LLVM Function*, which as a bare
        // Value* already has the flat pointer shape (`ptr` under opaque
        // pointers) a procedural variable's storage holds -- no closure
        // cell, no frame, matching CGTypes::llvmTypeOfSemaTypeImpl's
        // Procedure/Function case.
        if (n->Resolution == IdentExpr::IdentResolution::RoutineReference) {
            const std::string mangled = Linkage.findMangledProc(n->Name);
            if (auto* fn = Mod.getFunction(mangled)) return fn;
            codegenICE("routine '" + n->Name + "' resolved to a procedural "
                       "value but has no definition ('" + mangled
                       + "') in this module");
        }
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
                auto* r = B.CreateCall(
                    RtFns.getRuntimeBoolFn(lo == "eof" ? "plang_eof_stdin"
                                                 : "plang_eoln_stdin"), {}, lo);
                return EnsureI1(r);
            }
        }
        // Function result pseudo-variable (Pascal: assign to function name).
        // n->Resolution == ResultVariable alone is NOT enough: it only says
        // Sema decided this name denotes SOME enclosing function's result,
        // and ISO §6.8.2.2 lets a NESTED procedure's own assignment satisfy
        // an OUTER function -- 'outer := 37' written inside a nested
        // 'inner' still has Resolution == ResultVariable (checkIdent's own
        // comment), but CurRetAlloca/CurFuncName here are INNER's, not
        // outer's, while codegen is emitting inner's body.  The
        // toLower(Name)==toLower(CurFuncName) comparison is still what
        // narrows the fast path to "this function's own name" -- an outer
        // enclosing function's result is found the other way, through
        // SymTab.findVar below, which reaches it via the ordinary scope
        // chain (defVar(proc.Name, curRetAlloca, ...) in emitFunctionDef
        // registers each function's result alloca as an ordinary variable
        // of its own name, in the SCOPE that function's body opened).
        if (CurRetAlloca && n->Resolution == IdentExpr::IdentResolution::ResultVariable
                && toLower(n->Name) == toLower(CurFuncName)
                && !SymTab.boundInsideFunction(n->Name))
            return B.CreateLoad(CurRetType, CurRetAlloca, "retval");
        // -std=turbo only (see checkIdent's own comment): a bare read of the
        // enclosing function's own name that is NOT the assignment target is
        // a recursive, zero-argument call.  Ahead of the SymTab.findVar
        // lookup just below on purpose -- emitFunctionDef (CodeGenProcs.cpp)
        // ALSO registers the result alloca as an ordinary variable under the
        // function's own name (defVar(proc.Name, curRetAlloca, ...), so that
        // ResolvedVariable's own fast path above still works once a nested
        // procedure's own scope is pushed) -- so without this check first,
        // falling through would find that SAME VarEntry and silently read
        // the result cell again instead of calling.
        if (n->Resolution == IdentExpr::IdentResolution::RecursiveCall) {
            CallExpr Call;
            Call.Name         = n->Name;
            Call.Loc          = n->Loc;
            Call.ResolvedType = e.ResolvedType;
            return FuncCall.emitCallExpr(Call);
        }
        // Variable table.
        auto* ve = SymTab.findVar(n->Name);
        // Constant table.  A required constant stands only where the program
        // has not declared the name for something of its own: reading `pi`
        // gave 3.14159 in a program whose own `pi` had just been assigned to,
        // since the write went to the variable and the read never looked.
        // A program may also redeclare a required constant's name as a
        // FUNCTION (ISO §6.2.2.10) -- e.g. `function pi: real`.  That leaves
        // no VarEntry for `ve` to catch, so `writeln(pi)` returned the
        // builtin constant while `writeln(pi())` already correctly called the
        // user's function. UserDeclaredCallable is narrower than the plain
        // UserDeclared flag on purpose: an ordinary enum literal or named
        // constant is also UserDeclared, and excluding those from the
        // constant table here (as an earlier version of this fix did)
        // broke their normal resolution -- only a redeclaration as a
        // *procedure/function* should fall through to the call-resolution
        // path below instead of the constant table.
        auto cit = Consts.find(toLower(n->Name));
        if (cit != Consts.end()
                && !(ve && SymTab.isRequiredConst(toLower(n->Name)))
                && !n->UserDeclaredCallable)
            return cit->second;
        // ISO §6.6.3.1 with §6.8.2.2: a parameterless functional parameter
        // named in an expression is a call too.  It has a VarEntry, so it
        // would otherwise be loaded as if it were storage.
        if (ve && ve->isProcParam) {
            if (!ve->procType || !ve->procType->IsFunction)
                codegenICE("procedural parameter '" + n->Name
                           + "' used where a value is required");
            return ClosureAbi.emitProcParamCall(*ve, {});
        }
        if (!ve && (Mod.getFunction(Linkage.findMangledProc(n->Name))
                    || Linkage.isImportedCallable(n->Name))) {
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
            return FuncCall.emitCallExpr(Call);
        }
        // Not declared here, so it is a variable imported from a module.
        if (!ve) ve = ResolveImportedVar(n->Name, e.ResolvedType.get());
        if (!ve) {
            // Sema must have caught all undefined identifiers before codegen runs.
            std::string Msg = "plang codegen: identifier '" + n->Name
                            + "' not found in scope — Sema missed an undefined-identifier error";
            llvm::report_fatal_error(llvm::StringRef(Msg));
        }
        // VarString: return the struct address directly — callers use it as ptr.
        if (ExprIsVarStr(e)) return ve->ptr;
        auto* ld = B.CreateLoad(ve->type, ve->ptr, n->Name);
        // Issue #192: a with-bound field of a packed record is an ordinary
        // IdentExpr by the time it gets here, and IRBuilder's default ABI
        // alignment for it is a promise the byte-packed layout cannot keep;
        // see packedAccessAlign (CGFieldAccess.cpp).
        if (auto A = FieldAccess.packedAccessAlign(e)) ld->setAlignment(*A);
        return ld;
    }

    if (auto* n = llvm::dyn_cast<BinaryExpr>(&e))  return BinaryOps.emitBinary(*n);
    if (auto* n = llvm::dyn_cast<UnaryExpr>(&e))   return BinaryOps.emitUnary(*n);
    if (auto* n = llvm::dyn_cast<CallExpr>(&e))    return FuncCall.emitCallExpr(*n);
    // EP §6.4.3.3: a string(n) is carried by its ADDRESS -- every caller that
    // takes one expects a pointer to the { length, bytes } struct, which is
    // what the IdentExpr branch above hands back.  That contract held for an
    // identifier and nothing else, so a string reached as a field, an element
    // or a dereference was loaded by VALUE here instead: passing r.s to a
    // `string(25)` parameter loaded a { i64, [20 x i8] } and failed IR
    // verification, and every other caller of the contract had the same hole.
    if (ExprIsVarStr(e)
            && (llvm::isa<IndexExpr>(&e) || llvm::isa<FieldExpr>(&e)
                || llvm::isa<DerefExpr>(&e)))
        if (auto* p = emitLValue(e)) return p;

    if (auto* n = llvm::dyn_cast<IndexExpr>(&e))   return IndexAccess.emitIndexLoad(*n);
    if (auto* n = llvm::dyn_cast<FieldExpr>(&e))   return FieldAccess.emitFieldLoad(*n);
    if (auto* n = llvm::dyn_cast<DerefExpr>(&e))   return FieldAccess.emitDerefLoad(*n);
    if (auto* n = llvm::dyn_cast<SetLiteralExpr>(&e)) {
        // Empty set → 0.
        const int64_t base = Sets.setBaseOf(*n);
        const auto declaredRange = Sets.declaredRangeOf(*n);
        llvm::Value* result = llvm::ConstantInt::get(Sets.setTy(), 0);
        for (const auto& elem : n->Elements) {
            llvm::Value* bits = nullptr;
            if (auto* rng = llvm::dyn_cast<SetRangeExpr>(elem.get()))
                bits = Sets.emitSetRange(emitExpr(*rng->Low), emitExpr(*rng->High), base,
                                          declaredRange, rng->Loc);
            else
                bits = Sets.emitSetSingleton(emitExpr(*elem), base, declaredRange, elem->Loc);
            result = B.CreateOr(result, bits, "set");
        }
        return result;
    }
    if (auto* n = llvm::dyn_cast<SubstringExpr>(&e)) {
        // s[i..j] as an rvalue: produce a new string(cap) containing the substring.
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
        int64_t cap = ExprStrCapStatic(*n->Str);
        if (cap <= 0) cap = PlangMaxStringCapacity;
        // R6: address and capacity of a varying-capacity source from ONE walk
        // of its access path, the same strAddrAndCap the substring's own
        // ASSIGNMENT form (CodeGenStmts/CGAssign) already uses -- emitLValue
        // for the address and exprStrCapV for the source capacity (below)
        // each started a fresh walk from n->Str, so `q^.a[next].s[1..2]` as
        // a value (not an assignment target) ran `next` more than once, and
        // by a second route -- emitLValue's own fallback for a
        // varying-extent record field, CGFieldAccess::emitFieldGEP, tries
        // its own walk and then, when the record's extent varies, discards
        // it for a SchemaAccess one -- more than twice.
        //
        // The source capacity falls back to the same widest-capacity answer
        // when the operand is not typed as a string(n); exprStrCapV reports 0
        // there, and telling the runtime the source holds nothing put every
        // substring of one outside its own bounds.
        llvm::Value* strAddr;
        llvm::Value* srcCap;
        if (ExprIsVarStr(*n->Str)) {
            auto sp = Schema.strAddrAndCap(*n->Str);
            strAddr = sp.first; srcCap = sp.second;
        } else {
            strAddr = emitLValue(*n->Str);
            srcCap  = i64c(cap);
        }
        if (!strAddr) codegenICE("substring applied to a non-addressable operand");
        auto* resPtr = CreateEntryAlloca(Types.strStructType(cap), "substr.res");
        auto* low    = ToI64(emitExpr(*n->Low));
        auto* high   = ToI64(emitExpr(*n->High));
        // s[i..j] names its bounds, the runtime helper takes a count.
        auto* len    = B.CreateAdd(
            B.CreateSub(high, low, "substr.span"),
            llvm::ConstantInt::get(I64Ty, 1), "substr.len");
        auto* fn     = Strings.getStrFn("plang_str_substr",
            llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty, PtrTy, I64Ty, I64Ty, I64Ty});
        B.CreateCall(fn, {resPtr, i64c(cap), strAddr, srcCap, low, len});
        return resPtr;
    }
    if (auto* n = llvm::dyn_cast<WriteParam>(&e)) {
        // WriteParam in expression context: just emit the value.
        return emitExpr(*n->Value);
    }

    if (auto* n = llvm::dyn_cast<StructuredValueExpr>(&e))
        return StructuredValue.emitStructuredValue(*n);

    codegenICE("unhandled expression node in emitExpr");
}

// Returns the POINTER to the storage for an lvalue expression.
llvm::Value* CGExprCore::emitLValue(const ExprNode& e) {
    if (auto* n = llvm::dyn_cast<IdentExpr>(&e)) {
        // See emitExpr's identical IdentExpr case just above for why this
        // ALSO needs toLower(Name)==toLower(CurFuncName), not Resolution
        // alone: a nested procedure's own assignment to an OUTER enclosing
        // function's result (ISO §6.8.2.2) still has Resolution ==
        // ResultVariable while CurRetAlloca here is the NESTED procedure's
        // own, not the outer function's -- SymTab.findVar below is what
        // finds the outer one, through the ordinary scope chain.
        if (CurRetAlloca && n->Resolution == IdentExpr::IdentResolution::ResultVariable
                && toLower(n->Name) == toLower(CurFuncName)
                && !SymTab.boundInsideFunction(n->Name))
            return CurRetAlloca;
        // See emitExpr's identical check just above: the result alloca is
        // ALSO registered in SymTab under the function's own name, so this
        // has to come before the SymTab.findVar lookup just below, or a
        // recursive call's own address-needing use (@F, or F passed as a
        // var-parameter actual) would read raw, wrong-phase storage instead
        // of calling through and spilling the call's result.
        if (n->Resolution == IdentExpr::IdentResolution::RecursiveCall)
            return spillToTemporary(e);
        auto* ve = SymTab.findVar(n->Name);
        if (ve) return ve->ptr;
        // A string constant already lives in memory, as the { length, bytes }
        // struct a string value is read through, so it can answer for its own
        // address.  It is the one kind of constant that can: the others are
        // values in registers, and Sema has ruled out writing to any of them.
        if (auto cit = Consts.find(toLower(n->Name)); cit != Consts.end())
            if (llvm::isa<llvm::GlobalVariable>(cit->second))
                return cit->second;
        // ISO §6.8.2.2: a parameterless function-identifier in an expression
        // is a call.  Reading a component of what it returns needs the same
        // temporary a written-out call's result needs; without this the name
        // is taken for a variable and the link fails on a global that never
        // was one.
        if (Mod.getFunction(Linkage.findMangledProc(n->Name))
                || Linkage.isImportedCallable(n->Name))
            return spillToTemporary(e);
        // Not declared here, so it is a variable imported from a module.
        if (const auto* iv = ResolveImportedVar(n->Name, e.ResolvedType.get()))
            return iv->ptr;
        return nullptr;
    }
    if (auto* n = llvm::dyn_cast<IndexExpr>(&e))  return IndexAccess.emitIndexGEP(*n);
    if (auto* n = llvm::dyn_cast<FieldExpr>(&e))  return FieldAccess.emitFieldGEP(*n);
    if (auto* n = llvm::dyn_cast<DerefExpr>(&e)) {
        // ISO §6.5.5: f^ is the file's buffer variable, which lives beside the
        // stream rather than at an address the program holds, so it is asked
        // for by name.  Loading the file variable would hand back the handle
        // itself, which is what used to reach the store below.
        if (FileVars.isFileVar(*n->Pointer)) return FileVars.fileBufferPtr(*n->Pointer);
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
            if (auto ref = Schema.schemaRefOf(*n)) return ref->data;
        // p^ — load the pointer value; that IS the target address.
        auto* p = emitExpr(*n->Pointer);
        if (p && p->getType()->isPointerTy()) RangeGuards.emitNilCheck(p);
        return p;
    }
    if (auto* n = llvm::dyn_cast<SubstringExpr>(&e)) {
        // Substring lvalue s[i..j] — the address of the string variable itself.
        return emitLValue(*n->Str);
    }
    if (llvm::isa<CallExpr>(&e)) return spillToTemporary(e);
    return nullptr;
}

llvm::Value* CGExprCore::spillToTemporary(const ExprNode& e) {
    // ISO §6.7.2: a function result is a value and has no place of its own,
    // but reading a component of one — binding(f).bound — needs an address,
    // so it is lent a temporary.  Sema does not allow assignment through it,
    // so the temporary is only ever read.
    llvm::Value* v = emitExpr(e);
    if (!v) return nullptr;
    // A string result is spilled where it is called and hands back the
    // address of that spill, so it already has somewhere to live.
    if (v->getType()->isPointerTy()) return v;
    auto* tmp = CreateEntryAlloca(v->getType(), "call.result");
    B.CreateStore(v, tmp);
    return tmp;
}
