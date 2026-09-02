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
    // See MaxExprDepth/StackBaseline_ in CGExprCore.h (issue #556). Checked
    // before the RAII bump: a caller already sitting at the ceiling must
    // abort without recursing again. codegenICE never returns, so -- unlike
    // Parser::parsePower's or Sema::checkExpr's own guards -- there is no
    // "already reported" latch here for DepthGuard's construction order to
    // matter for; either ceiling firing ends the process on this statement.
    if (ExprDepth_ >= MaxExprDepth || stackNearlyExhausted(StackBaseline_))
        codegenICE("expression nesting exceeds CodeGen's depth ceiling, or "
                    "the real C++ stack is nearly exhausted; this is a hard "
                    "recursion limit of this build's expression emitter, "
                    "not a diagnostic on the input program");
    ExprDepthScope DepthGuard(ExprDepth_);

    if (auto* n = llvm::dyn_cast<IntLitExpr>(&e))
        // Deliberately ALWAYS i64 here, not n->ResolvedType->Width: unlike
        // every other integral expression kind, an IntLitExpr's own
        // Sema-resolved type is not a genuine width for ITS value -- Sema
        // types EVERY integer literal as the dialect's plain default
        // Integer (16 bits under Turbo) regardless of the literal's actual
        // magnitude (SemaExpr.cpp's IntLitExpr arm), so a literal like
        // LongInt's own MinInt (-2147483648, i.e. unary-minus of the
        // literal 2147483648) would be truncated to garbage at the literal
        // itself, well before the real target width (LongInt, from the
        // enclosing assignment/context) is ever consulted, if this emitted
        // at ResolvedType's width instead. i64 safely holds every literal
        // this front end accepts; issue #577's actual fix (making a
        // literal's contribution to CGBinaryOps' width-unification match
        // an equal-valued variable's) lives in CGBinaryOps.cpp instead,
        // where the width used for promotion is already a purpose-built
        // "what should this operand's width behave as" computation, not a
        // stand-in for "what LLVM type must this value survive being
        // stored in without loss" the way this call site's is.
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
                // -std=turbo only: 'input' is now a real, addressable
                // PascalFile (Sema::registerBuiltins' Input Symbol) that
                // Assign/Reset may have redirected away from stdin -- see
                // CGFuncCall.cpp's identical eof/eoln arm for the full
                // reasoning, which this bare (no-parentheses) spelling
                // mirrors exactly, dispatching to the same plang_eof_
                // file_turbo/plang_eoln_file_turbo entry points rather than
                // reading the real stdin regardless of any redirection.
                if (RangeGuards.isTurbo()) {
                    if (auto* ve = SymTab.findVar("Input")) {
                        auto* r = B.CreateCall(
                            RtFns.getExternFnN(lo == "eof" ? "plang_eof_file_turbo"
                                                            : "plang_eoln_file_turbo",
                                                I8Ty, {PtrTy}), {ve->ptr}, lo);
                        return EnsureI1(r);
                    }
                }
                auto* r = B.CreateCall(
                    RtFns.getRuntimeBoolFn(lo == "eof" ? "plang_eof_stdin"
                                                 : "plang_eoln_stdin"), {}, lo);
                return EnsureI1(r);
            }
            // -std=turbo only: Random, called bare with no parens
            // (`x := Random;`), is TP's own idiomatic zero-argument spelling
            // -- the same "no parentheses" shape eof/eoln get just above.
            // checkIdent's generic SymbolKind::Builtin case (Sema.cpp) has
            // nothing to say about arity (only checkCallExpr's
            // checkBuiltinArity does, and that only ever runs for the
            // parenthesized CallExpr form), so a bare use type-checks fine
            // with nothing here to route it to a call: without this case, it
            // fell through to the ordinary variable lookup below, found no
            // VarEntry for a name that was never one, and hit the "Sema
            // missed an undefined-identifier error" ICE.  A user's own
            // parameterless function named Random (UserDeclared) is excluded
            // the same way eof/eoln's is, so it is still resolved as an
            // ordinary call further down instead.
            if (lo == "random" && !n->UserDeclared) {
                auto* fn = RtFns.getExternFnN("plang_tp_random_real", DblTy, {});
                return B.CreateCall(fn, {}, "random");
            }
            // TP-only: ParamCount, like eof/eoln/Random just above, is real
            // Turbo Pascal's own idiom used bare, with no parentheses at
            // all -- Sema::checkIdent's generic SymbolKind::Builtin case
            // already types this correctly (it answers Sym->ReturnType for
            // any zero-argument bare builtin function read), but nothing
            // before this call routed the READ itself anywhere but the
            // ordinary variable table, so an undeclared 'ParamCount' fell
            // through to ResolveImportedVar and linked against a global
            // that was never one.  CGFuncCall::emitBuiltinCall's own
            // "paramcount" arm handles the WITH-parentheses call shape;
            // this is the same runtime call, reached the other way.
            if (lo == "paramcount" && !n->UserDeclared) {
                return B.CreateCall(
                    RtFns.getExternFnN("plang_tp_paramcount", I64Ty, {}), {}, lo);
            }
            // TP-only: IOResult, like ParamCount/Random just above, is real
            // Turbo Pascal's own idiom used bare, with no parentheses --
            // Sema::checkIdent's generic SymbolKind::Builtin case types this
            // correctly already (same checkEPOnly gating ParamCount/Random
            // get), but nothing before this call routed the read itself
            // anywhere but the ordinary variable table.
            // CGFuncCall::emitBuiltinCall's own "ioresult" arm handles the
            // WITH-parentheses call shape; this is the same runtime call,
            // reached the other way -- see plang_tp_ioresult's own comment
            // (runtime/plang_sys.cpp) for why the read-and-clear happens only
            // in the runtime, not duplicated at either of these two call sites.
            if (lo == "ioresult" && !n->UserDeclared) {
                return B.CreateCall(
                    RtFns.getExternFnN("plang_tp_ioresult", I64Ty, {}), {}, lo);
            }
            // Turbo Tier 4, Cluster C item 5: KeyPressed/ReadKey, like
            // ParamCount/IOResult just above, are almost always written bare
            // in real TP code ("if KeyPressed then ..."/"c := ReadKey").
            // CGFuncCall::emitBuiltinCall's own "keypressed"/"readkey" arms
            // handle the WITH-parentheses shape; this is the same runtime
            // call, reached the other way.
            if (lo == "keypressed" && !n->UserDeclared) {
                auto* raw = B.CreateCall(
                    RtFns.getExternFnN("plang_crt_keypressed", I8Ty, {}), {}, lo);
                return EnsureI1(raw);
            }
            if (lo == "readkey" && !n->UserDeclared) {
                return B.CreateCall(
                    RtFns.getExternFnN("plang_crt_readkey", I8Ty, {}), {}, lo);
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
        // Issue #773: a bare (no-parens) reference to a parameterless
        // FUNCTION method of the currently active implicit receiver --
        // Sema::checkImplicitMethodIdent already decided which one and
        // recorded it on this node.  Reuses CGFuncCall::emitCallExpr's own
        // CallExpr::ImplicitMethodReceiverType branch (the SAME one the
        // parenthesized 'Area()' spelling already goes through) via an
        // empty-Args synthetic CallExpr, exactly the way the RecursiveCall
        // branch just above reuses that same function for a plain
        // recursive call -- not a second implementation of the bound-method
        // call sequence.
        if (n->Resolution == IdentExpr::IdentResolution::ImplicitMethodCall) {
            if (!n->ImplicitMethodReceiverType)
                codegenICE("implicit method identifier '" + n->Name + "' has "
                           "no receiver type -- Sema should have refused "
                           "this already");
            CallExpr Call;
            Call.Name                     = n->Name;
            Call.Loc                      = n->Loc;
            Call.ResolvedType             = e.ResolvedType;
            Call.ImplicitMethodReceiverType = n->ImplicitMethodReceiverType;
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
        // Turbo procedural VALUES (issue #649): mirrors the procedural-
        // PARAMETER case just above -- a bare read of a FUNCTION-typed
        // procedural VARIABLE is an implicit zero-argument call, unless
        // Sema stamped this exact occurrence ProcVarRawValue (checkIdent's
        // own comment, SemaExpr.cpp): the "f2 := f1" / '@f1' / Assigned(f1)
        // idioms, which want f1's own stored value read, not called
        // through. Gated on ve->procType->IsFunction (not just isProcVar)
        // rather than an ICE on a Procedure-kind mismatch the way the
        // procParam case above does: Sema's own gate is the identical
        // IsFunction check, so a Procedure-kind procVar never reaches this
        // branch with Resolution left at Ordinary in the first place (it
        // never auto-calls), and falling through to the ordinary load below
        // is the same value that arm has always produced.
        if (ve && ve->isProcVar && ve->procType && ve->procType->IsFunction
                && n->Resolution != IdentExpr::IdentResolution::ProcVarRawValue) {
            return ClosureAbi.emitProcVarCall(*ve, {});
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
        // Turbo string[N]: the identical "carried by address" contract as
        // VarString just above, and for the identical reason -- every
        // caller of a string-shaped expression expects an ADDRESS to pass to
        // the string runtime, not the struct loaded by value.  A separate
        // branch rather than widening the VarString check with an `||`:
        // ShortString is never VarString (ExprIsVarStr is false for it by
        // construction), and the two runtimes must never be interchangeable
        // just because this one piece of plumbing happens to be identical.
        // Before this branch existed, falling through to the generic
        // B.CreateLoad below loaded the WHOLE packed <{i8,[N]}> struct by
        // value instead -- wrong in kind for every consumer downstream (a
        // concatenation, comparison, assignment, or call argument all
        // expect a pointer).
        if (ExprIsShortStr(e)) return ve->ptr;
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
    // Issue #786: 'S.Area' (or 'p^.Name') with no parentheses, from OUTSIDE
    // the object's own methods -- Sema::checkField's own IsImplicitMethodCall
    // flag says E.Field named no actual field, only a parameterless FUNCTION
    // method -- has to be checked and routed to emitImplicitMethodFieldCall
    // BEFORE the ExprIsVarStr/ExprIsShortStr FieldExpr branches just below.
    // A method returning string/ShortString has e.ResolvedType == that
    // string type same as a genuine string FIELD would, so if those checks
    // ran first they would take this FieldExpr for a real field and call
    // FieldAccess.emitFieldGEP on a field that does not exist -- 'object has
    // no field named ...' from the IR verifier.  See this same guard's twin
    // further below (kept, for a non-string-returning method) and emitLValue's
    // own FieldExpr case for the identical ordering requirement there.
    if (auto* n = llvm::dyn_cast<FieldExpr>(&e); n && n->IsImplicitMethodCall) {
        llvm::Value* selfPtr = emitLValue(*n->Record);
        if (!selfPtr) codegenICE("implicit method field access has no receiver address");
        return FuncCall.emitImplicitMethodFieldCall(*n, selfPtr);
    }
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
    // Turbo string[N]: the identical fix, for the identical reason, as the
    // VarString case just above -- a ShortString reached as a field, an
    // element or a dereference needs its ADDRESS here too, not a load of the
    // struct by value.  Kept as its own condition rather than an `||` onto
    // the VarString one: see this function's IdentExpr case for why the two
    // dialects' string-shaped values must stay two separate branches
    // throughout, never a single widened check.
    if (ExprIsShortStr(e)
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
                                          declaredRange, rng->Loc,
                                          exprIsSigned(*rng->Low), exprIsSigned(*rng->High));
            else
                bits = Sets.emitSetSingleton(emitExpr(*elem), base, declaredRange, elem->Loc,
                                              exprIsSigned(*elem));
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
        // exprStrCapDeclared.  Only the rvalue did this.
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
        // Sema places no type restriction on a substring bound beyond being
        // an expression (SubstringExpr's own SemaExpr.cpp arm just
        // checkExpr's Low/High), so either can be a signed narrow or
        // unsigned wide Turbo ordinal (issue #177's sibling audit).
        auto* low    = ToI64(emitExpr(*n->Low),  exprIsSigned(*n->Low));
        auto* high   = ToI64(emitExpr(*n->High), exprIsSigned(*n->High));
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

    if (auto* n = llvm::dyn_cast<TypeCastExpr>(&e)) return emitTypeCastValue(*n);

    // Turbo Tier 5, Cluster A item 4: 'Obj.Method(args)' / 'P^.Method(args)'
    // used as a value -- a static/direct call to Method's own mangled
    // symbol (CGFuncCall::emitMethodCallExpr's own comment for the whole
    // design; VMT dispatch is item 5's job, not this one's).
    if (auto* n = llvm::dyn_cast<MethodCallExpr>(&e)) return FuncCall.emitMethodCallExpr(*n);

    // Turbo Tier 5, issue #509: 'inherited [Method[(args)]]' used as a value
    // -- a static/direct call to the resolved ancestor's own mangled symbol,
    // never through the VMT; see CGFuncCall::emitInheritedCallExpr's own
    // comment for the whole design.
    if (auto* n = llvm::dyn_cast<InheritedCallExpr>(&e)) return FuncCall.emitInheritedCallExpr(*n);

    // Turbo procedural VALUES (issue #648): 'a[i](args)' / 'p^(args)' used
    // as a value -- see CGFuncCall::emitIndirectCallExpr's own comment.
    if (auto* n = llvm::dyn_cast<IndirectCallExpr>(&e)) return FuncCall.emitIndirectCallExpr(*n);

    codegenICE("unhandled expression node in emitExpr");
}

// Turbo VALUE typecast, read as a value (as opposed to used as an lvalue --
// see emitLValue's TypeCastExpr case for that).  Sema::checkTypeCast has
// already ruled out any pair for which neither strategy below applies.
llvm::Value* CGExprCore::emitTypeCastValue(const TypeCastExpr& n) {
    llvm::Type*  dstLlvmTy = Types.llvmTypeOfSemaType(*n.ResolvedType);
    const auto&  DstTy     = n.ResolvedType;
    const auto&  SrcTy     = n.Operand->ResolvedType;
    // Issue #645: Sema::checkTypeCast's untyped-var-parameter special case
    // aliases n.Operand->ResolvedType TO n.ResolvedType (see
    // IdentExpr::IsUntypedParamCastOperand's own comment), so SrcTy above is
    // never actually the operand's own type for this shape -- it is
    // DstTy's, making the bothScalar test just below spuriously true for
    // ANY ordinal/real target and routing a plain `v := Integer(x)` through
    // "value-evaluate x" (a load through x's OWN storage, which for an
    // untyped var param is a bare `ptr` slot holding the CALLER's address --
    // not x's referent at all) instead of "load x's referent at the
    // target's width".  Checked first, unconditionally: this is always a
    // reinterpretation of the caller's storage, never a value conversion,
    // regardless of what TargetTy happens to be.
    if (auto* id = llvm::dyn_cast<IdentExpr>(n.Operand.get());
            id && id->IsUntypedParamCastOperand) {
        llvm::Value* ptr = emitLValue(*n.Operand);
        if (!ptr) codegenICE("untyped var-parameter typecast operand has no storage");
        return B.CreateLoad(dstLlvmTy, ptr, "typecast.untyped");
    }
    const bool bothScalar = DstTy && SrcTy
        && (DstTy->isOrdinal() || DstTy->Kind == plang::TypeKind::Real)
        && (SrcTy->isOrdinal() || SrcTy->Kind == plang::TypeKind::Real);
    if (bothScalar) {
        // A genuine value CONVERSION -- Integer(SomeReal) truncates like
        // Trunc would, Integer(SomeChar) reinterprets the ordinal value --
        // reusing the exact same generic widen/narrow/int<->double helper
        // every other numeric coercion in codegen already goes through.
        // For a same-size ordinal pair (SmallInt(SomeInteger)) this is a
        // no-op once the LLVM types agree, which is bit-for-bit identical to
        // reinterpreting; for a genuinely different representation (Real's
        // bits are not its integer value's bits) only this path is correct.
        // n.Operand's own signedness: a Turbo VALUE typecast widening a
        // signed narrow (ShortInt) or unsigned wide (Word/Cardinal)
        // operand into a wider destination otherwise guessed sign- vs.
        // zero-extension from the operand's own LLVM width (issue #177's
        // sibling audit).
        llvm::Value* srcVal = emitExpr(*n.Operand);
        // Boolean(x) is a special case of the above: a strict Boolean
        // destination lowers to i1 (CGTypes::llvmTypeOfSemaTypeImpl), and
        // CoerceToType's ordinal path below is a plain *ExtOrTrunc, which
        // for an i1 destination keeps only the LOW BIT of an ordinal source
        // -- so `Boolean(2)`/`Boolean(4)` truncated to False instead of
        // True.  Real TP7/fpc field practice (confirmed against `fpc
        // -Mtp`) treats ANY nonzero value as True on a Boolean value cast,
        // exactly like Turbo's loose ByteBool/WordBool/LongBool family
        // already does (issue #633) -- so an ordinal source routes through
        // the same "nonzero" test every other Boolean-producing value
        // already goes through, rather than through CoerceToType's
        // truncation.  Guarded to an ORDINAL source only: a Real source
        // (`Boolean(SomeReal)`, also BothScalar-legal) keeps its existing
        // FPToSI behavior below, unchanged, since EnsureI1's own nonzero
        // test assumes an integer operand.
        if (dstLlvmTy->isIntegerTy(1) && srcVal->getType()->isIntegerTy())
            return EnsureI1(srcVal);
        // Issue #642: a loose ByteBool/WordBool/LongBool operand
        // (Type::IsLooseBool) widening into a wider integer destination --
        // e.g. `LongInt(WordBool(40000))` -- has to SIGN-extend its raw
        // storage bits, matching real `fpc -Mtp` (confirmed empirically:
        // `LongInt(WordBool(40000))` prints -25536, WordBool's stored 0x9C40
        // read as a signed i16), even though SrcTy->IsSigned is deliberately
        // false for this family (TypeContext::getLooseBoolean's own comment
        // -- required so `<`/`>` between two loose Booleans compares their
        // UNSIGNED magnitude, also matching fpc). exprIsSigned(*n.Operand)
        // alone would zero-extend here, so a loose-bool source overrides it.
        const bool srcIsLooseBool = SrcTy && SrcTy->IsLooseBool;
        return CoerceToType(srcVal, dstLlvmTy, srcIsLooseBool || exprIsSigned(*n.Operand));
    }
    // Not both ordinal-or-real: Sema only accepts this when the two types
    // are exactly the same size, so it is a VARIABLE-style reinterpretation
    // read here as a value (e.g. TByteRec(SomeWord) on the right of an
    // assignment). Loading through a pointer whose STATIC type is the
    // target, from storage the operand actually has, is what reinterprets
    // the bytes -- opaque pointers carry no pointee type of their own, only
    // the load does. An operand with no address of its own (a call result,
    // say) is spilled to one first, the same fallback spillToTemporary
    // itself is for.
    llvm::Value* ptr = emitLValue(*n.Operand);
    if (!ptr) ptr = spillToTemporary(*n.Operand);
    if (!ptr) codegenICE("type cast operand has neither storage nor a spillable value");
    return B.CreateLoad(dstLlvmTy, ptr, "typecast");
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
        // Issue #773: same reasoning as RecursiveCall just above -- Sema's
        // own isLValue already refuses this resolution as an assignment
        // TARGET (Symtab.lookup finds no Var/VarParam Symbol for a method
        // name), but a component of its result (e.g. an implicit method
        // returning a record, read as 'Area.Field') needs an address to GEP
        // through, which only a spilled temporary can give a call result.
        if (n->Resolution == IdentExpr::IdentResolution::ImplicitMethodCall)
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
    // Issue #786: same ordering requirement as emitExpr's own FieldExpr
    // case above it -- an implicit-method-call FieldExpr (e.g. 'p^.Name'
    // resolving to a parameterless FUNCTION, not a real field) names no
    // actual field, so emitFieldGEP below must never see it.  A caller that
    // needs this FieldExpr's ADDRESS (a component read off the call's
    // result, or emitExpr's own VarStr/ShortStr FieldExpr branch) gets one
    // the same way any other call result does: emitExpr the call and spill
    // it to a temporary.
    if (auto* n = llvm::dyn_cast<FieldExpr>(&e); n && n->IsImplicitMethodCall)
        return spillToTemporary(e);
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
    if (auto* n = llvm::dyn_cast<TypeCastExpr>(&e)) {
        // Turbo VARIABLE typecast: Sema (isLValue) only ever accepts this as
        // an lvalue when the operand is itself one and the two types are the
        // same size, so this is the entire lowering -- hand back the
        // OPERAND's own pointer, unchanged, so a write through it mutates
        // the operand's own storage rather than a copy. Opaque pointers
        // carry no static pointee type to reconcile; every load/store/GEP
        // that goes through this pointer already carries its own type
        // (n->ResolvedType, via CGTypes::llvmTypeOfSemaType), which is what
        // makes the reinterpretation happen -- see emitTypeCastValue's twin
        // case for the read side of the same idea.
        return emitLValue(*n->Operand);
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
