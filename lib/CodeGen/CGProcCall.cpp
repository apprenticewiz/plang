#include "CGProcCall.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"

using namespace plang;

void CGProcCall::emitCallStmt(const CallStmt& s) {
    std::string lo = toLower(s.Name);

    // ISO §6.6.3.1: a procedural parameter is called through the pair it
    // arrived as.  Checked before the required procedures, so a parameter
    // named `page` or `get` is still the parameter.
    if (auto* pve = SymTab.findVar(s.Name); pve && pve->isProcParam) {
        (void)ClosureAbi.emitProcParamCall(*pve, s.Args);
        return;
    }

    // Turbo procedural VALUES: see CGFuncCall::emitUserFuncCall's identical
    // arm -- an indirect call through whatever routine s.Name's variable
    // currently holds.
    if (auto* pve = SymTab.findVar(s.Name); pve && pve->isProcVar) {
        (void)ClosureAbi.emitProcVarCall(*pve, s.Args);
        return;
    }

    // ISO §6.2.2.10: a required procedure identifier may be redeclared, and
    // then it denotes what the program declared and not the required one.  The
    // chain below dispatches on spelling alone, so without this a program that
    // declares its own `close` reaches a required procedure that takes
    // different arguments — which it then emitted a call to with none of them.
    // Sema resolved the name in the scope it was written in and is the only
    // thing that knows which won.
    if (s.ResolvedBuiltin == BuiltinID::None) {
        emitUserProcCall(s);
        return;
    }

    // TP-only: Assert(cond[, msg]).  Gated on Switch::Assertions at the
    // CALL's own location, decided before anything else about the call is
    // emitted -- Turbo's `{$C-}` makes the whole call compile to nothing,
    // not even evaluating cond (confirmed against `fpc -Mtp`: a side-
    // effecting cond never runs its side effect with assertions off), which
    // is why this is checked here, first, rather than inside a guard the
    // way every other runtime check below is -- every one of those always
    // evaluates its own operands and only branches around the failure.
    if (lo == "assert" && !s.Args.empty()) {
        if (!RangeGuards.assertionsAt(s.Loc)) return;
        auto* cond = EnsureI1(EmitExpr(*s.Args[0]));
        auto* msg  = s.Args.size() > 1
            ? StrCall.emitCStrArg(*s.Args[1])
            : llvm::ConstantPointerNull::get(PtrTy);
        RangeGuards.emitGuard(B.CreateNot(cond), "assert", [&] {
            B.CreateCall(
                RtFns.getExternFnN("plang_err_assert_failed",
                                    llvm::Type::getVoidTy(Ctx), {PtrTy}),
                {msg});
        });
        return;
    }

    if (lo == "write" || lo == "writeln") {
        Builtins.emitBuiltinWrite(s.Args, lo == "writeln");
        return;
    }
    if (lo == "read") {
        Builtins.emitBuiltinRead(s.Args);
        return;
    }
    if (lo == "readln") {
        Builtins.emitBuiltinReadln(s.Args);
        return;
    }
    // EP §6.7.5.5: both require a destination/source plus at least one value.
    if (lo == "writestr" && s.Args.size() >= 2) {
        Builtins.emitBuiltinWriteStr(s.Args);
        return;
    }
    if (lo == "readstr" && s.Args.size() >= 2) {
        Builtins.emitBuiltinReadStr(s.Args);
        return;
    }
    if (lo == "page") {
        if (!s.Args.empty() && FileVars.isFileVar(*s.Args[0])) {
            auto* fp = FileVars.fileVarPtr(*s.Args[0]);
            B.CreateCall(RtFns.getExternFnN("plang_page_file",
                llvm::Type::getVoidTy(Ctx), {PtrTy}), {fp});
        } else {
            B.CreateCall(RtFns.getRuntimeFn("plang_page", nullptr), {});
        }
        return;
    }
    if ((lo == "reset" || lo == "rewrite") && !s.Args.empty()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        // A string(n) filename has no NUL terminator of its own -- only the
        // { length, bytes } struct every EP string carries -- and
        // plang_reset/plang_rewrite take `const char *`, so it has to be
        // marshalled into one the same way a var-string reaches any other
        // C-string-shaped runtime entry point.
        auto* nm = s.Args.size() > 1
            ? StrCall.emitCStrArg(*s.Args[1])
            : llvm::ConstantPointerNull::get(PtrTy);
        // §6.4.3.5 makes a text file a sequence of lines, each ended by a line
        // marker.  Turning one round to read it has to finish the line the
        // writing left open, and whether there is a line to finish is a
        // question only about a text file.
        auto* fn = RtFns.getExternFnN("plang_" + lo,
            llvm::Type::getVoidTy(Ctx), {PtrTy, PtrTy, I8Ty});
        B.CreateCall(fn, {fp, nm,
            llvm::ConstantInt::get(I8Ty,
                FileVars.isTypedBinaryFileVar(*s.Args[0]) ? 0 : 1)});
        return;
    }
    if ((lo == "get" || lo == "put") && !s.Args.empty()) {
        // ISO §6.5.5: both move one component, so both need its width.
        auto* fp  = FileVars.fileVarPtr(*s.Args[0]);
        auto* esz = llvm::ConstantInt::get(I64Ty, FileVars.getFileElemSize(*s.Args[0]));
        auto* fn  = RtFns.getExternFnN("plang_" + lo + "_file",
            llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty});
        B.CreateCall(fn, {fp, esz});
        return;
    }
    if (lo == "close" && !s.Args.empty()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        // §6.4.3.5, same as reset/rewrite just above: closing a file being
        // written has to finish whatever line write left open, and whether
        // there is a line to finish is a question only about a text file
        // (issue #234).
        auto* fn = RtFns.getExternFnN("plang_close",
            llvm::Type::getVoidTy(Ctx), {PtrTy, I8Ty});
        B.CreateCall(fn, {fp,
            llvm::ConstantInt::get(I8Ty,
                FileVars.isTypedBinaryFileVar(*s.Args[0]) ? 0 : 1)});
        return;
    }
    // EP §6.7.5.2: extend / update
    if ((lo == "extend" || lo == "update") && !s.Args.empty()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        // Same filename marshalling reset/rewrite need just above -- a
        // string(n) actual is a { length, bytes } struct, and plang_extend/
        // plang_update take `const char *`.
        auto* nm = s.Args.size() > 1
            ? StrCall.emitCStrArg(*s.Args[1])
            : llvm::ConstantPointerNull::get(PtrTy);
        // Same text-file line-finishing question as close, just above
        // (issue #234): extend in particular reopens (or reuses) F's stream
        // for appending, and an unfinished line left there would otherwise
        // glue straight onto whatever gets appended next.
        auto* fn = RtFns.getExternFnN("plang_" + lo,
            llvm::Type::getVoidTy(Ctx), {PtrTy, PtrTy, I8Ty});
        B.CreateCall(fn, {fp, nm,
            llvm::ConstantInt::get(I8Ty,
                FileVars.isTypedBinaryFileVar(*s.Args[0]) ? 0 : 1)});
        return;
    }
    // EP §6.7.5.2: SeekRead / SeekWrite / SeekUpdate.  n is a value of the
    // file's declared INDEX TYPE (ISO §6.7.5.2's own pre-assertion measures
    // "ord(n)-ord(a)"), not a byte offset and not a 0-based component count
    // -- so `file[5..10] of integer; SeekWrite(f, 5)` must land on the FIRST
    // component, not five components in.
    if ((lo == "seekread" || lo == "seekwrite" || lo == "seekupdate")
        && s.Args.size() >= 2) {
        auto* fp      = FileVars.fileVarPtr(*s.Args[0]);
        auto* idx     = ToI64(EmitExpr(*s.Args[1]));
        int64_t esz   = FileVars.getFileElemSize(*s.Args[0]);
        int64_t ilo   = FileVars.getFileIndexLow(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_" + lo,
            llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty, I64Ty, I64Ty});
        B.CreateCall(fn, {fp, idx, llvm::ConstantInt::get(I64Ty, esz),
                                llvm::ConstantInt::get(I64Ty, ilo)});
        return;
    }
    // EP §6.7.5.6: bind(f, b) / unbind(f) — associate/dissociate file binding
    if (lo == "bind" && s.Args.size() >= 2) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        auto* bp = EmitLValue(*s.Args[1]);
        auto* fn = RtFns.getExternFnN("plang_bind",
                                llvm::Type::getVoidTy(Ctx), {PtrTy, PtrTy});
        B.CreateCall(fn, {fp, bp});
        return;
    }
    if (lo == "unbind" && !s.Args.empty()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_unbind",
                                llvm::Type::getVoidTy(Ctx), {PtrTy});
        B.CreateCall(fn, {fp});
        return;
    }

    // EP §6.7.5.8: GetTimeStamp(t) — fill t with current date/time
    if (lo == "gettimestamp" && !s.Args.empty()) {
        auto* tPtr = EmitLValue(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_gettimestamp",
                                llvm::Type::getVoidTy(Ctx), {PtrTy});
        B.CreateCall(fn, {tPtr});
        return;
    }

    // ISO §6.7.5.4 transfer procedures.
    if ((lo == "pack" || lo == "unpack") && s.Args.size() == 3) {
        PackUnpack.emitPackUnpack(s, /*isPack=*/lo == "pack");
        return;
    }

    if (lo == "halt") {
        // EP §6.7.5.7 halt takes no argument; halt(n) is the widespread extension
        // that sets the exit status.
        auto* status = s.Args.empty() ? llvm::ConstantInt::get(I64Ty, 0)
                                      : ToI64(EmitExpr(*s.Args[0]));
        B.CreateCall(RtFns.getRuntimeHaltFn(), {status});
        B.CreateUnreachable();
        return;
    }

    // TP-only: Break/Continue leave/skip the innermost loop.  Sema's
    // LoopDepth_ (Sema.h) has already refused either one reaching here with
    // no enclosing loop, including from a procedure nested inside a loop's
    // body -- a nested procedure does not inherit its caller's loop context
    // -- so CurrentContinueTarget/CurrentBreakTarget's own CGFunction::
    // LoopStack is guaranteed non-empty.  The branch alone is the whole of
    // it: it terminates the current block exactly like a goto or halt does,
    // and emitCompound's own IsTerminated()/resumeAfterTerminator() (called
    // for every following statement in this same body, generically, not
    // specially for these two) already opens a fresh block for whatever
    // follows.
    if (lo == "break") {
        B.CreateBr(CurrentBreakTarget());
        return;
    }
    if (lo == "continue") {
        B.CreateBr(CurrentContinueTarget());
        return;
    }

    // TP-only: Exit([value]) leaves the current procedure or function
    // immediately.  Sema's checkCallStmt Exit arm has already refused a
    // value argument outside function context and checked one given against
    // CurrentRetType, so this only has to perform the store -- exactly the
    // store an ordinary `FuncName := value` would make (CGAssign::
    // emitAssignValue, reused rather than reimplemented: see its own doc
    // comment for why the target is a freshly-synthesized IdentExpr rather
    // than a real AssignStmt node).
    if (lo == "exit") {
        if (!s.Args.empty()) {
            IdentExpr target;
            target.Loc          = s.Loc;
            target.Name         = CurFuncName();
            target.ResolvedType = CurRetSemaType();
            Assign.emitAssignValue(target, *s.Args[0], s.Loc);
        }
        B.CreateBr(ExitBlock());
        return;
    }

    // TP-only: RunError([errorcode: Integer]).  Builtins.def gates this to
    // -std=turbo (Dialects = TP), so unlike RangeCheckGuards' own guards --
    // which serve every dialect from the same call site and so have to ask
    // Opts at run time -- reaching this arm at all already means the active
    // dialect is Turbo.  Routes through the same plang_tp_runerror(code)
    // every numbered check (div-zero, range, ...) reports through, so
    // RunError(200) and an actual division by zero are indistinguishable to
    // whatever reads the process's exit status, exactly as on real
    // Turbo/FPC.  fpc -Mtp confirms the no-argument form's default: 0.
    if (lo == "runerror") {
        auto* code = s.Args.empty() ? llvm::ConstantInt::get(I64Ty, 0)
                                    : ToI64(EmitExpr(*s.Args[0]));
        B.CreateCall(
            RtFns.getExternFnN("plang_tp_runerror", llvm::Type::getVoidTy(Ctx), {I64Ty}),
            {code});
        B.CreateUnreachable();
        return;
    }

    // TP-only: Include(s, x) / Exclude(s, x) -- `s := s + [x]` / `s := s -
    // [x]` by another name, reusing the same set primitives (SetOps) a
    // written-out set literal and `+`/`-` already lower through: build x's
    // singleton bitmask (range-checked against s's own declared base type,
    // same as a literal member would be), OR or AND-NOT it into s's current
    // value, and store the result back.
    if ((lo == "include" || lo == "exclude") && s.Args.size() == 2) {
        auto* addr = EmitLValue(*s.Args[0]);
        auto* cur  = B.CreateLoad(Sets.setTy(), addr, "set.cur");
        auto* bit  = Sets.emitSetSingleton(EmitExpr(*s.Args[1]),
                         Sets.setBaseOf(*s.Args[0]), Sets.declaredRangeOf(*s.Args[0]), s.Loc);
        auto* next = Sets.emitSetBinary(
            lo == "include" ? TokenKind::Plus : TokenKind::Minus, cur, bit);
        B.CreateStore(next, addr);
        return;
    }

    // TP-only: Inc(x[, n]) / Dec(x[, n]).  Mirrors CGFuncCall's succ/pred
    // lowering (widen, add/sub, range-check, narrow back) but STORES the
    // result into x instead of returning it -- Inc/Dec are statements,
    // succ/pred are functions, and that is the only difference between the
    // two.  x's address is computed exactly ONCE (EmitLValue) and read back
    // through a plain load from that same address, rather than also calling
    // EmitExpr(*s.Args[0]) separately -- x may be `arr[F()]` or similar,
    // and evaluating it a second time would run F() (and recompute the GEP)
    // twice for what has to be a single read-modify-write, the same
    // single-address discipline read()'s own target already follows
    // (BuiltinIO::emitReadArg). A PChar-like typed pointer
    // (isCharPointerType, Type.h) takes the GEP-scaled-by-pointee-size path
    // CGBinaryOps' own `p + n`/`p - n` already uses -- Sema's checkCallStmt
    // Inc/Dec arm refuses any OTHER pointer type, so nothing else reaches
    // this branch.
    if ((lo == "inc" || lo == "dec") && !s.Args.empty()) {
        const auto& ty = s.Args[0]->ResolvedType;
        auto* addr = EmitLValue(*s.Args[0]);
        if (ty && ty->Kind == TypeKind::Pointer) {
            auto* cur  = B.CreateLoad(PtrTy, addr, "incdec.cur");
            auto* step = s.Args.size() > 1 ? ToI64(EmitExpr(*s.Args[1]))
                                            : llvm::ConstantInt::get(I64Ty, 1);
            if (lo == "dec") step = B.CreateNeg(step, "dec.step");
            llvm::Type* elemLLVMTy = Types.llvmTypeOfSemaType(*ty->PointeeType);
            auto* np = B.CreateGEP(elemLLVMTy, cur, {step}, "incdec.ptr");
            B.CreateStore(np, addr);
            return;
        }
        auto* llTy = ty ? Types.llvmTypeOfSemaType(*ty) : I64Ty;
        auto* cur  = B.CreateLoad(llTy, addr, "incdec.cur");
        auto* v = ToI64(cur);
        auto* k = s.Args.size() > 1 ? ToI64(EmitExpr(*s.Args[1]))
                                     : llvm::ConstantInt::get(I64Ty, 1);
        auto* r = (lo == "inc") ? B.CreateAdd(v, k, "inc") : B.CreateSub(v, k, "dec");
        if (ty && !ty->isError())
            if (auto range = ordinalRange(*ty))
                RangeGuards.emitRangeCheck(r, range->first, range->second,
                                            /*isIndex=*/false, s.Loc);
        auto* narrowed = (cur && cur->getType() != I64Ty)
            ? B.CreateZExtOrTrunc(r, cur->getType(), lo) : r;
        B.CreateStore(narrowed, addr);
        return;
    }

    // TP-only: FillChar(var X; Count: Integer; Value).  X is "untyped" --
    // any variable at all, addressed directly and filled byte-for-byte with
    // Value's own low byte, regardless of X's declared type.
    if (lo == "fillchar" && s.Args.size() == 3) {
        auto* dst   = EmitLValue(*s.Args[0]);
        auto* count = ToI64(EmitExpr(*s.Args[1]));
        auto* val   = B.CreateTrunc(ToI64(EmitExpr(*s.Args[2])), I8Ty, "fillchar.val");
        B.CreateMemSet(dst, val, count, llvm::MaybeAlign());
        return;
    }
    // TP-only: Move(const Source; var Dest; Count: Integer).  Turbo's own
    // parameter order is (Source, Dest, Count) -- the REVERSE of
    // llvm.memmove's (and C memmove's) own (Dest, Src, Len) -- so `src`
    // below is deliberately read from s.Args[0] and `dst` from s.Args[1],
    // not in textual left-to-right order, to land each in the position
    // CreateMemMove actually wants. Lowered to memmove specifically, not
    // memcpy: Source and Dest may legally overlap (Turbo's own Move is
    // defined to handle that correctly), and memcpy's behavior on
    // overlapping ranges is undefined.
    if (lo == "move" && s.Args.size() == 3) {
        auto* src   = EmitLValue(*s.Args[0]);
        auto* dst   = EmitLValue(*s.Args[1]);
        auto* count = ToI64(EmitExpr(*s.Args[2]));
        B.CreateMemMove(dst, llvm::MaybeAlign(), src, llvm::MaybeAlign(), count);
        return;
    }

    // ---- Turbo System-unit ShortString routines that mutate a var
    // parameter, plus Str/Val -- gated TP in Builtins.def, so reaching any
    // of these arms already means -std=turbo.  sstrArgPtr mirrors
    // CGFuncCall.cpp's own identical local lambda (its own doc comment
    // explains why each file keeps a copy rather than sharing one): a
    // ShortString expression's address and static capacity directly, or a
    // fresh capacity-sized temporary for a Char/literal operand.
    auto sstrArgPtr = [&](const ExprNode& x) -> std::pair<llvm::Value*, llvm::Value*> {
        if (ExprIsShortStr(x))
            return {StrCall.emitStrAddr(x), llvm::ConstantInt::get(I64Ty, ExprShortStrCap(x), true)};
        // A literal's OWN length is the byte count to copy -- NOT the
        // capacity floor below, which exists only so a 0-length literal ('',
        // legal under Turbo too) still gets a real (1-byte-minimum) alloca
        // to point at.  See CGBinaryOps.cpp's sstrOperand for the identical
        // fix and the bug this avoids repeating (an empty-literal argument
        // reading one stray byte off its own zero-length interned data).
        int64_t litLen = 0;
        bool isLit = false;
        if (auto* sl = llvm::dyn_cast<StringLitExpr>(&x)) {
            isLit  = true;
            litLen = static_cast<int64_t>(sl->Value.size());
        }
        const int64_t cap = isLit ? std::max<int64_t>(1, litLen) : 1;
        auto* val = EmitExpr(x);
        auto* tmp = CreateEntryAlloca(Types.sstrStructType(cap), "sstr.arg");
        if (val && val->getType()->isIntegerTy(8))
            Strings.emitSstrFromChar(tmp, llvm::ConstantInt::get(I64Ty, cap), val);
        else if (isLit)
            Strings.emitSstrFromBytes(tmp, llvm::ConstantInt::get(I64Ty, cap), val,
                                       llvm::ConstantInt::get(I64Ty, litLen));
        else if (val)
            Strings.emitSstrFromBytes(tmp, llvm::ConstantInt::get(I64Ty, cap), val,
                                       llvm::ConstantInt::get(I64Ty, cap));
        return {tmp, llvm::ConstantInt::get(I64Ty, cap)};
    };
    // Delete(var s, index, count) -- mutates s in place; Builtins.def's own
    // comment on the out-of-range-index NO-OP rule (not a clamp).
    if (lo == "delete" && s.Args.size() == 3) {
        auto* sp  = StrCall.emitStrAddr(*s.Args[0]);
        auto* idx = ToI64(EmitExpr(*s.Args[1]));
        auto* cnt = ToI64(EmitExpr(*s.Args[2]));
        auto* fn  = RtFns.getExternFnN("plang_sstr_delete", llvm::Type::getVoidTy(Ctx),
            {PtrTy, I64Ty, I64Ty, I64Ty});
        B.CreateCall(fn, {sp, llvm::ConstantInt::get(I64Ty, ExprShortStrCap(*s.Args[0])), idx, cnt});
        return;
    }
    // Insert(source, var s, index) -- mutates s in place, clamped at s's own
    // declared capacity; Builtins.def's own comment on the out-of-range-index
    // CLAMP rule (the opposite of Delete's no-op).
    if (lo == "insert" && s.Args.size() == 3) {
        auto [srcp, srcc] = sstrArgPtr(*s.Args[0]);
        auto* sp  = StrCall.emitStrAddr(*s.Args[1]);
        auto* idx = ToI64(EmitExpr(*s.Args[2]));
        auto* fn  = RtFns.getExternFnN("plang_sstr_insert", llvm::Type::getVoidTy(Ctx),
            {PtrTy, I64Ty, PtrTy, I64Ty, I64Ty});
        B.CreateCall(fn, {sp, llvm::ConstantInt::get(I64Ty, ExprShortStrCap(*s.Args[1])),
                          srcp, srcc, idx});
        return;
    }
    // SetLength(var s, newLength) -- sets s's own length byte, clamped to
    // s's declared capacity; Builtins.def's own comment on the deliberate
    // divergence from fpc's own unclamped (buffer-overrunning) behavior.
    if (lo == "setlength" && s.Args.size() == 2) {
        auto* sp     = StrCall.emitStrAddr(*s.Args[0]);
        auto* newLen = ToI64(EmitExpr(*s.Args[1]));
        auto* fn = RtFns.getExternFnN("plang_sstr_setlength", llvm::Type::getVoidTy(Ctx),
            {PtrTy, I64Ty, I64Ty});
        B.CreateCall(fn, {sp, llvm::ConstantInt::get(I64Ty, ExprShortStrCap(*s.Args[0])), newLen});
        return;
    }
    // Str(x [: width [: decimals]], var s) -- reuses the writestr capture
    // machinery (BuiltinIO.cpp), which owns the full argument-order/
    // ShortString-header handling; nothing further to do here.
    if (lo == "str" && s.Args.size() == 2) {
        Builtins.emitBuiltinStr(s.Args);
        return;
    }
    // Val(s, var v, var code) -- parses s (any turbo-string-like value, not
    // necessarily an lvalue) into v (Integer- or Real-kind, any width;
    // Sema's own Val arm already refused anything else) and sets code to 0
    // on success or the 1-based bad-character index on failure.  NEVER
    // aborts the process -- the whole reason plang_val_parse_int/_real
    // (runtime/plang_val.cpp) exist as a genuinely new, non-fatal primitive
    // rather than a reuse of plang_read_i64/_f64's [[noreturn]] machinery.
    if (lo == "val" && s.Args.size() == 3) {
        auto [sp, scap] = sstrArgPtr(*s.Args[0]);
        (void)scap;
        auto* dataPtr = Strings.sstrDataPtr(sp);
        auto* lenRaw  = Strings.sstrLoadLen(sp);
        auto* lenV    = B.CreateZExt(lenRaw, I64Ty, "val.srclen");

        auto* vAddr = EmitLValue(*s.Args[1]);
        auto* cAddr = EmitLValue(*s.Args[2]);
        const auto& vTy = s.Args[1]->ResolvedType;
        const plang::Type* vBase = vTy.get();
        while (vBase && vBase->Kind == TypeKind::Subrange && vBase->SubBase)
            vBase = vBase->SubBase.get();

        auto* codeTmp = CreateEntryAlloca(I64Ty, "val.code");
        auto* voidTy  = llvm::Type::getVoidTy(Ctx);
        if (vBase && vBase->Kind == TypeKind::Real) {
            auto* valTmp = CreateEntryAlloca(DblTy, "val.real");
            auto* fn = RtFns.getExternFnN("plang_val_parse_real", voidTy,
                {PtrTy, I64Ty, PtrTy, PtrTy});
            B.CreateCall(fn, {dataPtr, lenV, valTmp, codeTmp});
            auto* loaded = B.CreateLoad(DblTy, valTmp, "val.real.loaded");
            llvm::Type* vLLTy = Types.llvmTypeOfSemaType(*vTy);
            B.CreateStore(CoerceToType(loaded, vLLTy), vAddr);
        } else {
            const bool isUnsigned = vBase && !vBase->IsSigned;
            auto* valTmp = CreateEntryAlloca(I64Ty, "val.int");
            auto* fn = RtFns.getExternFnN("plang_val_parse_int", voidTy,
                {PtrTy, I64Ty, I8Ty, PtrTy, PtrTy});
            B.CreateCall(fn, {dataPtr, lenV,
                llvm::ConstantInt::get(I8Ty, isUnsigned ? 1 : 0), valTmp, codeTmp});
            auto* loaded = B.CreateLoad(I64Ty, valTmp, "val.int.loaded");
            llvm::Type* vLLTy = Types.llvmTypeOfSemaType(*vTy);
            B.CreateStore(CoerceToType(loaded, vLLTy), vAddr);
        }
        auto* codeLoaded = B.CreateLoad(I64Ty, codeTmp, "val.code.loaded");
        llvm::Type* cLLTy = Types.llvmTypeOfSemaType(*s.Args[2]->ResolvedType);
        B.CreateStore(CoerceToType(codeLoaded, cLLTy), cAddr);
        return;
    }

    if (lo == "new" && !s.Args.empty()) {
        // EP §6.7.5.3: new(p, d1..ds) when p's domain-type is a schema-name.
        if (const auto& pt = s.Args[0]->ResolvedType;
                pt && pt->Kind == TypeKind::Pointer && pt->PointeeType
                && pt->PointeeType->Kind == TypeKind::Schema) {
            auto ref = Schema.emitNewSchema(*s.Args[0], *pt->PointeeType,
                          std::span(s.Args).subspan(1));
            // EP §6.6 with §6.7.5.3: the instance new() creates begins in the
            // same initial state a declared one would (see the plain-pointer
            // case below) -- plang_new's calloc zeroed it instead, which is
            // not the same thing wherever the body has a `value` clause.
            EmitSchemaInitialState(ref.data, *pt->PointeeType, ref.discs);
            return;
        }
        auto* addr = EmitLValue(*s.Args[0]);
        // How much to allocate is a question about the pointer's type, not
        // about how the declaration was written.  A pointer reached through a
        // type name has no PointerTypeNode to read, and the old fallback of
        // one pointer's worth silently under-allocated for anything larger.
        // R1, reopened by review 5.  Sema's answer was already here -- as the
        // FALLBACK, reached only when the denoter route returned 0.  The
        // denoter route walks `typeAliases` by SPELLING at the use site, so a
        // pointer declared `var g: pt` in a program where a procedure declares
        // its own `pt` allocated the INNER pt's domain: 16 bytes for a
        // ten-element array, and glibc aborted on the corrupted heap.  Plain
        // ISO 7185, and the shape the 0.1.5/0.1.6 corruptions had.
        //
        // The R1 rule went into llvmTypeOfNode's NamedTypeNode branch, which is
        // why this survived it: the name is re-bound HERE, before any TypeNode
        // is lowered, so llvmTypeOfNode is handed the inner declaration's base
        // and answers correctly for the wrong type.  A site that resolves a
        // name before reaching the rule is not covered by the rule.
        int64_t            Bytes   = 0;
        const TypeNode*    domain  = nullptr;
        const plang::Type* pointee = nullptr;
        if (const auto& pt = s.Args[0]->ResolvedType;
                pt && pt->Kind == TypeKind::Pointer && pt->PointeeType)
            pointee = pt->PointeeType.get();
        if (pointee)
            Bytes = (int64_t)Mod.getDataLayout().getTypeAllocSize(
                Types.llvmTypeOfSemaType(*pointee));

        // The domain DENOTER is still wanted, for the initial state below --
        // Sema's Type records a RecordDecl and nothing more general, so a
        // `value` clause on a non-record domain is only reachable through the
        // node.  It is accepted only when it agrees with the size Sema gave:
        // the same spelling walk that mis-sized the allocation also picked the
        // wrong type's `value` clause, memcpying 400 bytes of one type's
        // initial value into another's 4-byte allocation.  A disagreement means
        // the denoter was re-resolved somewhere else, so it is not this
        // variable's domain and its initial state is not this variable's.
        //
        // The size-agreement check is not enough on its own: `ve->typeNode` is
        // `g`'s OWN declaration, written wherever `g` was declared -- module
        // scope, say -- and not in the procedure calling `new(g)`.  denoterOf
        // walked `typeAliases` for that FOREIGN node's name, so a procedure
        // that merely shadows the pointer's own type name with an unrelated,
        // SAME-SIZE one slipped straight through the check: `new(g)` inside a
        // procedure with its own local `type pt = ^inner_dom` (one field,
        // like the real domain) applied inner_dom's `value` clause to g's
        // real, unrelated allocation.  initialStateShapeOf is the fix already
        // used for exactly this pattern elsewhere: it follows
        // NamedTypeNode::Denotes, which Sema recorded in the scope `pt` was
        // actually written in.
        if (auto* id = llvm::dyn_cast<IdentExpr>(s.Args[0].get()))
            if (auto* ve = SymTab.findVar(id->Name))
                if (auto* ptn = llvm::dyn_cast_or_null<PointerTypeNode>(
                        InitialStateShapeOf(ve->typeNode))) {
                    const TypeNode* d = ptn->Base.get();
                    const auto dsz = (int64_t)Mod.getDataLayout()
                        .getTypeAllocSize(Types.llvmTypeOfNode(*d));
                    if (Bytes == 0 || dsz == Bytes) {
                        domain = d;
                        if (Bytes == 0) Bytes = dsz;
                    }
                }
        // The domain type, for the initial state below.  Only the identifier
        // route set it, so `new(h.p)` and `new(a[1])` applied no initial state
        // at all: the size already fell back to Sema's type and this did not.
        // A record is what carries field `value` clauses, and Sema's type
        // knows the declaration it came from.
        if (!domain && pointee) domain = pointee->RecordDecl;
        if (Bytes == 0)
            codegenICE("new() cannot determine the size of what '"
                       + std::string(s.Args[0]->ResolvedType
                                     ? s.Args[0]->ResolvedType->Name : "?")
                       + "' points to");
        auto* ptr = B.CreateCall(RtFns.getRuntimeNewFn(),
                                       {llvm::ConstantInt::get(I64Ty, Bytes)});
        B.CreateStore(ptr, addr);
        // EP §6.6: the variable new creates begins in whatever state its type
        // says a variable of it begins in, as a declared one would.
        if (domain && HasInitialState(domain))
            EmitInitialState(ptr, Types.llvmTypeOfNode(*domain), domain);
        return;
    }
    if (lo == "dispose" && !s.Args.empty()) {
        auto* val = EmitExpr(*s.Args[0]);
        B.CreateCall(RtFns.getRuntimeDisposeFn(), {val});
        return;
    }

    // s.ResolvedBuiltin != BuiltinID::None (the top of this function already
    // returned otherwise) and lo matched none of the required-PROCEDURE
    // names above -- every Proc-kind row in Builtins.def has its own arm
    // there.  The only builtin left is a required FUNCTION, which Sema
    // resolves to this same statement shape only under Turbo `{$X+}`
    // (Sema::checkCallStmt's err_func_as_statement arm).  Its result is
    // simply not wanted, exactly like an ordinary user-defined function
    // called as a statement just below.
    if (auto* v = EmitBuiltinFuncCall(s.Name, s.Args, s.Loc)) { (void)v; return; }
    codegenICE("'" + s.Name + "' resolved to a builtin function but matched "
               "no builtin-function dispatch arm");
}

void CGProcCall::emitUserProcCall(const CallStmt& s) {
    // User-defined procedure — walk the nesting hierarchy to find the right
    // LLVM mangled name (plang_outer__inner, not just plang_inner).
    std::string mangledName = Linkage.findMangledProc(s.Name);
    auto* callee = Mod.getFunction(mangledName);
    if (!callee) {
        // The procedure is not defined in this compilation unit; it must come
        // from a separately compiled module.  Create an external declaration
        // using LLVM types derived from the Sema-resolved argument types.
        //
        // Turbo `{$X+}`: s.ResolvedType is non-null exactly when this CallStmt
        // is a FUNCTION called as a statement (Sema set it only on that path
        // -- see CallStmt::ResolvedType's own comment) and void otherwise, an
        // ordinary procedure having none to give.  Mirrors
        // CGFuncCall::emitUserFuncCall's identical fallback for a call in
        // expression position, which is what keeps the two from disagreeing
        // about the same external function's return type should it be
        // called both ways in this translation unit.
        llvm::Type* retTy = llvm::Type::getVoidTy(Ctx);
        if (s.ResolvedType && !s.ResolvedType->isError())
            retTy = Types.llvmTypeOfSemaType(*s.ResolvedType);
        std::vector<llvm::Type*> paramTys;
        for (const auto& Arg : s.Args) {
            if (Arg && Arg->ResolvedType && !Arg->ResolvedType->isError())
                paramTys.push_back(Types.llvmTypeOfSemaType(*Arg->ResolvedType));
            else
                paramTys.push_back(I64Ty); // safe fallback
        }
        auto* fnTy = llvm::FunctionType::get(retTy, paramTys, false);
        callee = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage,
                                        mangledName, &Mod);
    }

    std::vector<llvm::Value*> args;

    // If the callee is a nested procedure, build its static-link frame from
    // funcOuterVarNames_[callee] (recorded at definition) so the slot ordering
    // matches exactly, resolving each name through findVar() in the *current*
    // scope.  findVar() returns the local alloca for immediate outer vars and
    // the GEP-loaded pointer for deeper ones — composing correctly through any
    // number of nesting levels.
    if (auto* frame = BuildStaticLinkFrame(mangledName)) args.push_back(frame);

    // EP §6.7.3.7: look up conformant param dimensions for this callee.
    // ConformantDimsOf(mangledName, astArgIdx) is the dimension count for the
    // i-th AST argument position.  0 means the param is not conformant (emit
    // normally).
    size_t pi = args.size(); // LLVM arg index (after static link)
    for (size_t astArgIdx = 0; astArgIdx < s.Args.size(); ++astArgIdx) {
        const auto& arg = s.Args[astArgIdx];

        // ISO §6.6.3.1: procedural param — entry point plus its frame.
        if (const auto* pt = ProcParamArg(mangledName, astArgIdx)) {
            ClosureAbi.pushProcParamArgs(args, *arg, *pt);
            pi = args.size();
            continue;
        }

        // Check if this AST arg position is conformant.
        // EP §6.4.7: schema param — body pointer plus its discriminants.
        if (unsigned nd = Schema.schemaArgDiscs(mangledName, astArgIdx); nd > 0) {
            Schema.pushSchemaArgs(args, *arg, nd);
            pi = args.size();
            continue;
        }

        const size_t dims = ConformantDimsOf(mangledName, astArgIdx);
        if (dims > 0) {
            ClosureAbi.pushConformantArgs(args, *arg, dims);
            pi += 1 + 2 * dims;
        } else {
            // Regular param (var or value).
            std::optional<int64_t> destSetBase = ParamSetBaseOf(mangledName, astArgIdx);
            args.push_back(Sets.alignSetArg(
                StrCall.emitCallArg(*arg,
                    pi < callee->arg_size()
                        ? callee->getFunctionType()->getParamType(pi) : nullptr,
                    ParamIsByRef(mangledName, astArgIdx)),
                *arg, destSetBase));
            ++pi;
        }
    }
    B.CreateCall(callee, args);
}
