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

void CGProcCall::emitIoCheckIfNeeded(plang::SourceLocation Loc) {
    if (!RangeGuards.isTurbo() || !RangeGuards.ioChecksAt(Loc)) return;
    auto* fn = RtFns.getExternFnN("plang_iocheck", llvm::Type::getVoidTy(Ctx), {});
    B.CreateCall(fn, {});
}

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
        emitIoCheckIfNeeded(s.Loc);
        return;
    }
    if (lo == "read") {
        Builtins.emitBuiltinRead(s.Args);
        emitIoCheckIfNeeded(s.Loc);
        return;
    }
    if (lo == "readln") {
        Builtins.emitBuiltinReadln(s.Args);
        emitIoCheckIfNeeded(s.Loc);
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
    // -std=turbo only: Assign(f, name) -- TP's own file model (real
    // Borland/FPC's Assign), which the reset/rewrite/append/close arms just
    // below build on: Reset/Rewrite/Append under Turbo take no filename
    // argument of their own, they open whatever Assign bound f to last.
    // Gated only by Builtins.def's own TP-only dialect entry (like Assert/
    // RunError/... above), so this is never reached under ISO/EP.
    if (lo == "assign" && s.Args.size() >= 2) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        // Same filename marshalling reset/rewrite/extend/update already
        // need just below -- a string(n)/ShortString actual has no NUL
        // terminator of its own, and plang_tp_assign takes `const char *`.
        auto* nm = StrCall.emitCStrArg(*s.Args[1]);
        auto* fn = RtFns.getExternFnN("plang_tp_assign",
            llvm::Type::getVoidTy(Ctx), {PtrTy, PtrTy});
        B.CreateCall(fn, {fp, nm});
        return;
    }

    // -std=turbo only: Reset/Rewrite/Append's own binding comes from
    // whatever Assign(f, name) last bound f to -- an empty bound name
    // meaning "the console" (stdin for Reset, stdout for Rewrite/Append).
    //
    // Cluster A item 4 retired the PR #475/#478 "2-argument reset/rewrite is
    // an implicit Assign" convenience: real Turbo Pascal's second argument
    // is an INTEGER RecSize for an untyped file (confirmed against
    // `fpc -Mtp`), not a filename, and Sema now enforces exactly that (see
    // err_turbo_reset_rewrite_recsize_type, SemaStmt.cpp) -- so the only
    // shape reaching here for a 2-argument Reset/Rewrite is a RecSize int,
    // never a filename, and there is no implicit Assign to perform anymore.
    // Append is unaffected either way: real Turbo Pascal's Append is always
    // 1-arg (Builtins.def's own TP entry has MaxArgs 1), so it never reaches
    // the `s.Args.size() > 1` arm below.
    //
    // RecSize itself: a typed file's (`file of T`) is SizeOf(T), computed
    // here from the element type and NEVER from a user-supplied argument
    // (the roadmap's own rule) -- any explicit integer argument on a typed
    // file is still evaluated, for its side effects, but its value is
    // discarded. An untyped file's (`var f: file;`) is the given argument,
    // or TP's own documented default of 128 when none is given. A `text`
    // file has no RecSize concept at all (no such Reset/Rewrite overload in
    // real Turbo Pascal), so it skips the _sized wrapper entirely and calls
    // plang_tp_reset/plang_tp_rewrite directly, same as Append always does.
    // See plang_tp_reset_sized/plang_tp_rewrite_sized (runtime/
    // plang_file.cpp) for what a RecSize of 0 does (InOutRes 2, real
    // Borland/FPC field practice).  A genuinely separate runtime function
    // family, not the ISO ones below with a flag -- this project's P7 rule
    // that dialect selection happens at the CALL-SITE, since an ISO and a
    // Turbo object file can be linked into one program.
    if ((lo == "reset" || lo == "rewrite") && !s.Args.empty() && RangeGuards.isTurbo()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        if (FileVars.isTypedBinaryFileVar(*s.Args[0])) {
            if (s.Args.size() > 1) (void)EmitExpr(*s.Args[1]); // side effects only
            auto* recSize = llvm::ConstantInt::get(I64Ty,
                FileVars.getFileElemSize(*s.Args[0]));
            auto* fn = RtFns.getExternFnN("plang_tp_" + lo + "_sized",
                llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty});
            B.CreateCall(fn, {fp, recSize});
        } else if (FileVars.isUntypedFileVar(*s.Args[0])) {
            auto* recSize = s.Args.size() > 1
                ? ToI64(EmitExpr(*s.Args[1]), exprIsSigned(*s.Args[1]))
                : llvm::ConstantInt::get(I64Ty, 128); // TP's own default
            auto* fn = RtFns.getExternFnN("plang_tp_" + lo + "_sized",
                llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty});
            B.CreateCall(fn, {fp, recSize});
        } else {
            // A `text` file: no RecSize overload in real Turbo Pascal.  Any
            // argument given still has to be evaluated for its side effects.
            if (s.Args.size() > 1) (void)EmitExpr(*s.Args[1]);
            auto* fn = RtFns.getExternFnN("plang_tp_" + lo,
                llvm::Type::getVoidTy(Ctx), {PtrTy});
            B.CreateCall(fn, {fp});
        }
        emitIoCheckIfNeeded(s.Loc);
        return;
    }
    if (lo == "append" && !s.Args.empty() && RangeGuards.isTurbo()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_tp_append",
            llvm::Type::getVoidTy(Ctx), {PtrTy});
        B.CreateCall(fn, {fp});
        emitIoCheckIfNeeded(s.Loc);
        return;
    }
    // -std=turbo only: Seek(f, n) / Truncate(f).  n is in units of f's own
    // RecSize -- plang_tp_seek reads F->RecSize itself, so codegen passes n
    // through untouched (unlike EP's seekread/seekwrite/seekupdate above,
    // which pass ElemSize/IndexLow because those runtime calls have no
    // RecSize field to read yet).
    if (lo == "seek" && s.Args.size() == 2 && RangeGuards.isTurbo()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        auto* n  = ToI64(EmitExpr(*s.Args[1]), exprIsSigned(*s.Args[1]));
        auto* fn = RtFns.getExternFnN("plang_tp_seek",
            llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty});
        B.CreateCall(fn, {fp, n});
        emitIoCheckIfNeeded(s.Loc);
        return;
    }
    if (lo == "truncate" && !s.Args.empty() && RangeGuards.isTurbo()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_tp_truncate",
            llvm::Type::getVoidTy(Ctx), {PtrTy});
        B.CreateCall(fn, {fp});
        emitIoCheckIfNeeded(s.Loc);
        return;
    }
    // -std=turbo only: BlockRead(f, var buf; count[; var result]) /
    // BlockWrite(f, var buf; count[; var result]).  buf is untyped -- an
    // EmitLValue straight to its storage, same as FillChar/Move's own X/
    // Source/Dest (below).  plang_tp_blockread/plang_tp_blockwrite always
    // return the actual record count transferred and take an int8 "has a
    // result argument" flag: WITH one (HasResult != 0) a short transfer is
    // not an error at all, and this only stores the actual count into
    // result; WITHOUT one, the runtime call itself sets InOutRes (100/101)
    // on a short transfer -- see runtime/plang_file.cpp's own comment for
    // why that decision lives there and not here (this is exactly the kind
    // of "which error, if any" decision the RecSize field comment and this
    // item's own plan put in the runtime, not codegen).
    if ((lo == "blockread" || lo == "blockwrite") && s.Args.size() >= 3
            && RangeGuards.isTurbo()) {
        auto* fp     = FileVars.fileVarPtr(*s.Args[0]);
        auto* buf    = EmitLValue(*s.Args[1]);
        auto* count  = ToI64(EmitExpr(*s.Args[2]), exprIsSigned(*s.Args[2]));
        const bool hasResult = s.Args.size() > 3;
        auto* fn = RtFns.getExternFnN(
            lo == "blockread" ? "plang_tp_blockread" : "plang_tp_blockwrite",
            I64Ty, {PtrTy, PtrTy, I64Ty, I8Ty});
        auto* actual = B.CreateCall(fn, {fp, buf, count,
            llvm::ConstantInt::get(I8Ty, hasResult ? 1 : 0)}, lo + ".actual");
        if (hasResult) {
            auto* resAddr = EmitLValue(*s.Args[3]);
            const auto& resTy = s.Args[3]->ResolvedType;
            auto* resLLTy = resTy ? Types.llvmTypeOfSemaType(*resTy) : I64Ty;
            auto* narrowed = resLLTy != I64Ty
                ? B.CreateZExtOrTrunc(actual, resLLTy, lo + ".result") : actual;
            B.CreateStore(narrowed, resAddr);
        }
        // WITHOUT a result argument, emitIoCheckIfNeeded is what decides
        // whether the InOutRes 100/101 the runtime call may just have set
        // aborts under {$I+} -- exactly the same choke point Reset/Rewrite/
        // Append/Close above already go through.  WITH one, the runtime
        // call above never set InOutRes to begin with (HasResult
        // suppresses it), so this is harmless there too.
        emitIoCheckIfNeeded(s.Loc);
        return;
    }
    // -std=turbo only: Erase(f) / Rename(f, newname).  Both act on F->Name
    // (the name Assign bound f to), requiring F be fmClosed first -- a
    // RUNTIME check (F->Mode), not a Sema one; see Builtins.def's own
    // comment for the confirmed InOutRes 102 ("file not assigned", FPC's
    // own field practice) either sets on a still-open f.
    if (lo == "erase" && !s.Args.empty() && RangeGuards.isTurbo()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_tp_erase",
            llvm::Type::getVoidTy(Ctx), {PtrTy});
        B.CreateCall(fn, {fp});
        emitIoCheckIfNeeded(s.Loc);
        return;
    }
    if (lo == "rename" && s.Args.size() == 2 && RangeGuards.isTurbo()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        // Same string(n)-has-no-NUL-terminator marshalling Assign's own
        // filename argument needs, just above.
        auto* nm = StrCall.emitCStrArg(*s.Args[1]);
        auto* fn = RtFns.getExternFnN("plang_tp_rename",
            llvm::Type::getVoidTy(Ctx), {PtrTy, PtrTy});
        B.CreateCall(fn, {fp, nm});
        emitIoCheckIfNeeded(s.Loc);
        return;
    }
    // -std=turbo only: Flush(f) -- flush f's buffered output without
    // closing it.
    if (lo == "flush" && !s.Args.empty() && RangeGuards.isTurbo()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_tp_flush",
            llvm::Type::getVoidTy(Ctx), {PtrTy});
        B.CreateCall(fn, {fp});
        emitIoCheckIfNeeded(s.Loc);
        return;
    }
    // -std=turbo only: SetTextBuf(var f: Text; var buf[; size]).  buf is
    // untyped, same as BlockRead/BlockWrite's own above; size defaults to
    // 1024 when omitted (Builtins.def's own comment on why that specific
    // fallback, rather than SizeOf(buf), is used).
    if (lo == "settextbuf" && s.Args.size() >= 2 && RangeGuards.isTurbo()) {
        auto* fp  = FileVars.fileVarPtr(*s.Args[0]);
        auto* buf = EmitLValue(*s.Args[1]);
        auto* size = s.Args.size() > 2
            ? ToI64(EmitExpr(*s.Args[2]), exprIsSigned(*s.Args[2]))
            : llvm::ConstantInt::get(I64Ty, 1024);
        auto* fn = RtFns.getExternFnN("plang_tp_settextbuf",
            llvm::Type::getVoidTy(Ctx), {PtrTy, PtrTy, I64Ty});
        B.CreateCall(fn, {fp, buf, size});
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
    // -std=turbo only: Close under TP has none of ISO's §6.4.3.5
    // "finish the final line" step (see plang_tp_close's own comment,
    // runtime/plang_file.cpp, for exactly what is deliberately left out).
    if (lo == "close" && !s.Args.empty() && RangeGuards.isTurbo()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_tp_close",
            llvm::Type::getVoidTy(Ctx), {PtrTy});
        B.CreateCall(fn, {fp});
        emitIoCheckIfNeeded(s.Loc);
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
        auto* idx     = ToI64(EmitExpr(*s.Args[1]), exprIsSigned(*s.Args[1]));
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

    // TP-only: Randomize -- reseeds RandSeed from wall-clock time, so
    // successive RUNS of the same program get a different Random sequence;
    // see runtime/plang_math.cpp's plang_tp_randomize for what "wall-clock
    // time" means here (clock_gettime(CLOCK_REALTIME), not plang_gettimestamp
    // /time_t's one-second resolution -- two runs started in the same second
    // would otherwise collide).
    if (lo == "randomize") {
        B.CreateCall(RtFns.getExternFnN("plang_tp_randomize",
            llvm::Type::getVoidTy(Ctx), {}), {});
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

    // Turbo Tier 4, Cluster C item 5: Crt's own Delay(MS: Word) -- see
    // Builtins.def's own comment on why this is a dialect-wide builtin
    // rather than scoped to `uses Crt`.  MS is widened to i64 the same way
    // every other small-integer runtime argument is (e.g. Randomize's
    // sibling Halt(n) just below).
    if (lo == "delay" && !s.Args.empty()) {
        auto* ms = ToI64(EmitExpr(*s.Args[0]), exprIsSigned(*s.Args[0]));
        B.CreateCall(RtFns.getExternFnN("plang_crt_delay",
            llvm::Type::getVoidTy(Ctx), {I64Ty}), {ms});
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
                                      : ToI64(EmitExpr(*s.Args[0]), exprIsSigned(*s.Args[0]));
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

    // Turbo Tier 5, Cluster A item 6: 'Fail' inside a constructor -- see
    // CurCtorOkAlloca's own comment (CGProcCall.h) and curCtorOkAlloca's
    // (CodeGenImpl.h) for the whole ABI design.  Shares Exit's own epilogue
    // block (ExitBB): the only difference from a bare 'Exit;' is which flag
    // gets set before the branch.
    if (lo == "fail") {
        auto* okAlloca = CurCtorOkAlloca();
        if (!okAlloca)
            codegenICE("'Fail' reached CodeGen outside a constructor -- "
                       "Sema::checkCallStmt's own 'fail' arm should have "
                       "refused this already");
        B.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt1Ty(Ctx), 0),
                      okAlloca);
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
                                    : ToI64(EmitExpr(*s.Args[0]), exprIsSigned(*s.Args[0]));
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
                         Sets.setBaseOf(*s.Args[0]), Sets.declaredRangeOf(*s.Args[0]), s.Loc,
                         exprIsSigned(*s.Args[1]));
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
            auto* step = s.Args.size() > 1 ? ToI64(EmitExpr(*s.Args[1]), exprIsSigned(*s.Args[1]))
                                            : llvm::ConstantInt::get(I64Ty, 1);
            if (lo == "dec") step = B.CreateNeg(step, "dec.step");
            llvm::Type* elemLLVMTy = Types.llvmTypeOfSemaType(*ty->PointeeType);
            auto* np = B.CreateGEP(elemLLVMTy, cur, {step}, "incdec.ptr");
            B.CreateStore(np, addr);
            return;
        }
        auto* llTy = ty ? Types.llvmTypeOfSemaType(*ty) : I64Ty;
        auto* cur  = B.CreateLoad(llTy, addr, "incdec.cur");
        // s.Args[0]'s own ResolvedType is x's -- consulted explicitly rather
        // than left to ToI64's LLVM-width guess, which zero-extended a
        // negative ShortInt (i8, signed) x and then failed the in-range
        // increment's own range check below spuriously (issue #177).
        auto* v = ToI64(cur, exprIsSigned(*s.Args[0]));
        auto* k = s.Args.size() > 1 ? ToI64(EmitExpr(*s.Args[1]), exprIsSigned(*s.Args[1]))
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
        auto* count = ToI64(EmitExpr(*s.Args[1]), exprIsSigned(*s.Args[1]));
        auto* val   = B.CreateTrunc(ToI64(EmitExpr(*s.Args[2]), exprIsSigned(*s.Args[2])), I8Ty, "fillchar.val");
        // A negative Count is a no-op in real TP7/fpc, not a size_t (issue
        // #628): CreateMemSet's size operand is unsigned, so passing the i64
        // `count` straight through would reinterpret a negative value as a
        // huge byte count and stomp far past `dst`.  Clamp to zero first.
        auto* isNegCount = B.CreateICmpSLT(count, llvm::ConstantInt::get(I64Ty, 0), "fillchar.count.isneg");
        count = B.CreateSelect(isNegCount, llvm::ConstantInt::get(I64Ty, 0), count, "fillchar.count.clamped");
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
        auto* count = ToI64(EmitExpr(*s.Args[2]), exprIsSigned(*s.Args[2]));
        // Same negative-Count no-op as FillChar above (issue #628): a
        // negative i64 handed to CreateMemMove's unsigned size operand
        // becomes a near-SIZE_MAX copy and segfaults.
        auto* isNegCount = B.CreateICmpSLT(count, llvm::ConstantInt::get(I64Ty, 0), "move.count.isneg");
        count = B.CreateSelect(isNegCount, llvm::ConstantInt::get(I64Ty, 0), count, "move.count.clamped");
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
        auto* idx = ToI64(EmitExpr(*s.Args[1]), exprIsSigned(*s.Args[1]));
        auto* cnt = ToI64(EmitExpr(*s.Args[2]), exprIsSigned(*s.Args[2]));
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
        auto* idx = ToI64(EmitExpr(*s.Args[2]), exprIsSigned(*s.Args[2]));
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
        auto* newLen = ToI64(EmitExpr(*s.Args[1]), exprIsSigned(*s.Args[1]));
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

    // Turbo Tier 5, Cluster A item 6: new(p, Ctor[(args)]) for a pointer to
    // an object type -- Sema::checkNewInit already confirmed Ctor really is
    // a constructor and validated its arguments; s.NewInitMethod non-empty
    // is exactly that confirmation.  Real Borland/FPC's own ABI (confirmed
    // against a local fpc -Mtp build -- see err_fail_outside_constructor's
    // own comment, DiagnosticSemaKinds.def, and curCtorOkAlloca's,
    // CodeGenImpl.h): allocate, stamp '_vptr' (the SAME stampVptr
    // emitVarValueInit gives a directly declared local/global, just handed
    // freshly allocated memory instead of a variable's own storage), call
    // the constructor, and if -- and only if -- it called 'Fail' (returned
    // false), throw the whole thing away and leave p nil, exactly the
    // "unwinds back through New itself" contract fpc -Mtp demonstrates.  No
    // destructor runs on the Fail path either way (confirmed empirically,
    // even when an ancestor's own portion of construction had already
    // completed) -- Dispose is not called here, only the raw deallocation
    // 'new(p)' alone already uses (RtFns.getRuntimeDisposeFn()).
    if (lo == "new" && !s.NewInitMethod.empty()) {
        const auto& pt = s.Args[0]->ResolvedType;
        const plang::Type& Pointee = *pt->PointeeType;
        auto* addr = EmitLValue(*s.Args[0]);
        // issue #622's own refactor: the allocate/StampVptr/StampFieldVptrs/
        // emitBoundMethodCall/Fail-unwind sequence that used to live inline
        // here is now emitNewObjectValue (CGProcCall.h's own comment), so
        // that CGFuncCall's 'p := New(PtrType, Ctor[(args)])' -- New used as
        // a FUNCTION, with no lvalue of its own to store into -- shares it
        // rather than duplicating the branch-and-PHI Fail-unwind logic.
        B.CreateStore(emitNewObjectValue(Pointee, *s.Args[1]), addr);
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
        // Issue #514: deliberately NO StampVptr/StampFieldVptrs call here,
        // for Pointee (or any object-typed field nested inside it, however
        // deep), even though pointee may well be -- or contain -- an object
        // type with a `_vptr`.  This is the PLAIN new(p) form -- real
        // Borland/FPC warns "use extended syntax of NEW and DISPOSE for
        // instances of objects" for exactly this call and never stamps a
        // vptr for it either, so a later virtual call through the result
        // cleanly traps "Runtime error 216" instead of dispatching to a
        // real override (confirmed against a local `fpc -Mtp` build) --
        // matched here, not "fixed" to be safer, per this project's own
        // policy of matching real field practice on a dialect ambiguity.
        // plang_new's own calloc (runtime/plang_sys.cpp) already leaves
        // every byte of Pointee's storage NULL, `_vptr` slot(s) included --
        // never uninitialized garbage -- so nothing needs to be written
        // here at all: CGFuncCall::emitMethodCallExpr's own vptr-load (and
        // its two siblings, CGProcCall's emitMethodCallStmt/
        // emitBoundMethodCall) already check for exactly that NULL and trap
        // there, right before the indirect call that would otherwise
        // segfault reading a function pointer from near address zero.
        return;
    }
    // Turbo Tier 5, Cluster A item 6: dispose(p, Dtor[(args)]) -- New/Init's
    // mirror.  Sema::checkDisposeDone already confirmed Dtor really is a
    // destructor; s.DisposeDoneMethod non-empty is that confirmation.  A
    // destructor commonly IS virtual in real TP7 idiom -- see
    // emitBoundMethodCall's own comment for why that dispatch is exactly
    // right here too -- so the destructor named in SOURCE need not be the
    // one actually run: 'dispose(pAnimal, Done)' through an ancestor-typed
    // pointer still reaches whatever descendant's own Done override the
    // object's '_vptr' names.  Runs BEFORE the plain deallocation below
    // (confirmed against a local fpc -Mtp build: the destructor's own
    // output appears before the memory is freed), reusing that same
    // deallocation rather than a second copy of it.
    if (lo == "dispose" && !s.DisposeDoneMethod.empty()) {
        const auto& pt = s.Args[0]->ResolvedType;
        const plang::Type& Pointee = *pt->PointeeType;
        auto* ptr = EmitExpr(*s.Args[0]);

        std::string dtorName;
        std::span<const std::unique_ptr<ExprNode>> dtorArgs;
        if (auto* CE = llvm::dyn_cast<CallExpr>(s.Args[1].get())) {
            dtorName = CE->Name;
            dtorArgs = CE->Args;
        } else if (auto* Id = llvm::dyn_cast<IdentExpr>(s.Args[1].get())) {
            dtorName = Id->Name;
        } else {
            codegenICE("dispose(p, Dtor) reached CodeGen with an "
                       "unrecognized second-argument shape -- "
                       "Sema::checkDisposeDone should have refused this "
                       "already");
        }
        (void)emitBoundMethodCall(ptr, Pointee, dtorName, dtorArgs);

        B.CreateCall(RtFns.getRuntimeDisposeFn(), {ptr});
        return;
    }
    if (lo == "dispose" && !s.Args.empty()) {
        auto* val = EmitExpr(*s.Args[0]);
        B.CreateCall(RtFns.getRuntimeDisposeFn(), {val});
        return;
    }

    // TP-only: GetMem(var P: Pointer; Size: Int64) -- a wholly separate,
    // NON-ABORTING allocation entry point from New's plang_new just above
    // (runtime/plang_sys.cpp's plang_tp_getmem's own top comment explains
    // the whole design, including how HeapError is consulted on failure).
    if (lo == "getmem" && s.Args.size() == 2) {
        auto* addr = EmitLValue(*s.Args[0]);
        auto* size = ToI64(EmitExpr(*s.Args[1]), exprIsSigned(*s.Args[1]));
        auto* fn = RtFns.getExternFnN("plang_tp_getmem", PtrTy, {I64Ty});
        auto* p = B.CreateCall(fn, {size}, "getmem");
        B.CreateStore(p, addr);
        return;
    }
    // TP-only: FreeMem(P: Pointer[, Size: Int64]) -- Size defaults to 0 when
    // omitted; plang_tp_freemem ignores it either way (its own comment).
    if (lo == "freemem" && !s.Args.empty()) {
        auto* p    = EmitExpr(*s.Args[0]);
        auto* size = s.Args.size() > 1 ? ToI64(EmitExpr(*s.Args[1]), exprIsSigned(*s.Args[1]))
                                        : llvm::ConstantInt::get(I64Ty, 0);
        auto* fn = RtFns.getExternFnN("plang_tp_freemem",
                                       llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty});
        B.CreateCall(fn, {p, size});
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

bool CGProcCall::tryEmitDosProcCall(const CallStmt& s) {
    // Turbo Tier 4, Cluster C item 6: share/plang/units/Dos.pas's own header
    // comment explains why six of its exports (every one with a `string`
    // VALUE parameter) cannot be reached the way the REST of Dos's exports
    // are, by a plain linker-symbol alias -- plang's own calling convention
    // for a `string` passed by value does not match the real x86-64 SysV
    // ABI a hand-written C++ function can reproduce.  Recognized here by
    // NAME, but only once Linkage.importOwner confirms the call actually
    // resolved to something 'uses Dos' brought into scope -- a program's
    // OWN same-named procedure (no 'uses Dos' at all) has an empty
    // importOwner and falls straight through to the ordinary path below,
    // unaffected.
    if (!eqCI(Linkage.importOwner(s.Name), "dos")) return false;
    const std::string lo = toLower(s.Name);
    auto* voidTy = llvm::Type::getVoidTy(Ctx);
    auto* i16Ty  = llvm::Type::getInt16Ty(Ctx);
    if (lo == "chdir" || lo == "mkdir" || lo == "rmdir") {
        const char* rt = lo == "chdir" ? "plang_dos_chdir"
                        : lo == "mkdir" ? "plang_dos_mkdir"
                                        : "plang_dos_rmdir";
        auto* fn = RtFns.getExternFnN(rt, voidTy, {PtrTy});
        B.CreateCall(fn, {StrCall.emitCStrArg(*s.Args[0])});
        return true;
    }
    if (lo == "exec") {
        auto* fn = RtFns.getExternFnN("plang_dos_exec", voidTy, {PtrTy, PtrTy});
        B.CreateCall(fn, {StrCall.emitCStrArg(*s.Args[0]),
                           StrCall.emitCStrArg(*s.Args[1])});
        return true;
    }
    if (lo == "findfirst") {
        auto* fn = RtFns.getExternFnN("plang_dos_findfirst", voidTy,
                                       {PtrTy, i16Ty, PtrTy});
        auto* pathArg = StrCall.emitCStrArg(*s.Args[0]);
        // Issue #696: a bare EmitExpr(*s.Args[1]) hands back whatever LLVM
        // type the actual's own expression shape produces -- I64Ty for
        // every integer LITERAL (CGExprCore.cpp's IntLitExpr case always
        // materializes i64, unconditionally of context), regardless of
        // plang_dos_findfirst's own `uint16_t attr` parameter just above.
        // A Word variable or a named constant like Dos.pas's faAnyFile
        // happened to already carry the right (narrower) LLVM type by the
        // time it got here and never tripped this, so `FindFirst('*.*',
        // faAnyFile, sr)` worked while the equally-legal, and far more
        // idiomatic, `FindFirst('*.*', 63, sr)` was an LLVM IR verifier
        // failure ("i64 63 passed to an i16 parameter") on the single most
        // common call form.  StringCallMarshalling::emitCallArg is the
        // general "marshal ONE actual to a callee's declared LLVM param
        // type" mechanism every user-declared-procedure call already goes
        // through (CGCallMarshal::marshalArgs); it ends in exactly the
        // narrow/widen-to-paramTy coercion this raw builtin call site was
        // missing, so routing through it here rather than a bespoke
        // truncate fixes this the same way a user-declared `procedure
        // p(attr: Word)` was already unaffected.
        auto* attrArg = StrCall.emitCallArg(*s.Args[1], i16Ty, /*byRef=*/false);
        auto* recArg  = EmitLValue(*s.Args[2]);
        B.CreateCall(fn, {pathArg, attrArg, recArg});
        return true;
    }
    return false;
}

namespace {
// Same ancestor-chain walk as CGFuncCall.cpp's identical local helper (its
// own doc comment explains the whole design and why it is a fresh copy
// rather than a shared one -- this project's own established convention for
// small per-file call-emission helpers, e.g. CGProcCall.h's own sstrArgPtr
// vs. CGFuncCall.cpp's).
const plang::Type* methodOwnerType(const plang::Type& RecvTy,
                                    const std::string& Method) {
    for (const plang::Type* Cur = &RecvTy; Cur; Cur = Cur->Parent.get())
        for (const auto& M : Cur->ObjectMethods)
            if (eqCI(M.Name, Method)) return Cur;
    return nullptr;
}

// Turbo Tier 5, Cluster A item 5: same fresh-copy convention as
// methodOwnerType just above; see CGFuncCall.cpp's identical
// methodEntryOf for the design.
const plang::Type::Method* methodEntryOf(const plang::Type& Owner,
                                          const std::string& Method) {
    for (const auto& M : Owner.ObjectMethods)
        if (eqCI(M.Name, Method)) return &M;
    return nullptr;
}

// Turbo Tier 5, Cluster B item 8: an external declaration (never a
// definition) for a method this translation unit did not itself compile --
// same fresh-copy convention as methodOwnerType/methodEntryOf just above;
// this is CGProcCall.cpp's own copy of Codegen::Impl::declareImportedMethod
// (CodeGenProcs.cpp), which CGProcCall cannot call directly (constructed
// standalone, with no reference back to Impl -- see this class's own
// constructor). Built from M's own RESOLVED signature, exactly like the
// Impl-side twin; see that one's own comment (CodeGenImpl.h) for why a
// conformant-array or procedural-type parameter is a clear codegenICE
// rather than a wrong-ABI guess.
llvm::Function* declareForeignMethod(llvm::Module& Mod, llvm::LLVMContext& Ctx,
                                     llvm::PointerType* PtrTy, CGTypes& Types,
                                     const plang::Type::Method& M,
                                     const std::string& mangledName) {
    if (auto* existing = Mod.getFunction(mangledName)) return existing;

    std::vector<llvm::Type*> paramTys;
    paramTys.push_back(PtrTy); // Self
    for (const auto& P : M.Params) {
        if (P.IsUntyped) { paramTys.push_back(PtrTy); continue; }
        if (!P.Ty)
            codegenICE("imported method '" + mangledName + "' has a "
                       "parameter '" + P.Name + "' with no resolved type");
        if (P.Ty->Kind == plang::TypeKind::ConformantArray
                || P.Ty->Kind == plang::TypeKind::Procedure
                || P.Ty->Kind == plang::TypeKind::Function)
            codegenICE("imported method '" + mangledName + "' has a "
                       "conformant-array or procedural-type parameter '"
                       + P.Name + "' -- not yet supported across a unit "
                         "boundary");
        const bool constByRef = P.IsConst && !P.Ty->isError()
            && plang::isStructuredForConstByRef(*P.Ty);
        const bool passByRef = P.IsVar || constByRef;
        paramTys.push_back(passByRef ? PtrTy : Types.llvmTypeOfSemaType(*P.Ty));
    }

    llvm::Type* retTy = llvm::Type::getVoidTy(Ctx);
    if (M.IsConstructor) retTy = llvm::Type::getInt1Ty(Ctx);
    else if (M.IsFunction && M.RetType) retTy = Types.llvmTypeOfSemaType(*M.RetType);

    auto* fnTy = llvm::FunctionType::get(retTy, paramTys, /*isVarArg=*/false);
    return llvm::Function::Create(fnTy, llvm::GlobalValue::ExternalLinkage,
                                  mangledName, &Mod);
}
} // namespace

void CGProcCall::emitMethodCallStmt(const MethodCallStmt& s) {
    if (!s.Receiver->ResolvedType)
        codegenICE("method call has no resolved receiver type");
    const Type* Owner = methodOwnerType(*s.Receiver->ResolvedType, s.Method);
    if (!Owner)
        codegenICE("method '" + s.Method + "' has no owning type in its "
                   "receiver's own ancestor chain -- Sema should have "
                   "refused this call already");
    std::string mangledName = Linkage.mangledMethod(Owner->Name, s.Method, Owner->DeclaringModule);

    llvm::Value* selfPtr = EmitLValue(*s.Receiver);
    if (!selfPtr) codegenICE("method call receiver has no address");

    // Turbo Tier 5, Cluster B item 8: this USED to guess the callee's
    // parameter types from the CALL SITE's own argument expressions
    // (Arg->ResolvedType) whenever the callee was not yet declared here --
    // dead code before this item (emitAllProcedures's own method pre-pass
    // always declares every SAME-TRANSLATION-UNIT method's real signature
    // before any body runs, so this fallback could only ever fire for a
    // genuinely foreign method, which did not exist before object types
    // could cross a unit boundary at all). That guess is wrong whenever an
    // argument's own static type is merely COMPATIBLE with, rather than
    // IDENTICAL to, the callee's real declared parameter type -- a string
    // LITERAL argument (TypeKind::String, lowered to a bare `ptr`) passed to
    // a `string` (TypeKind::ShortString, lowered to a `{i8,[255 x i8]}`
    // struct) formal produced a declaration under the literal's OWN type
    // instead of the formal's, and StringCallMarshalling::emitCallArg,
    // reading that wrong declared type back at the actual call, sent the
    // literal through EmitLValue (a literal has no address) instead of the
    // struct-building path -- an LLVM IR verifier "Operand is null" on
    // every cross-unit method call passing a string actual. Declared from
    // Owner's own resolved Method entry instead -- the real signature,
    // exactly like declareForeignMethod's every other caller below.
    auto* callee = Mod.getFunction(mangledName);
    if (!callee) {
        const Type::Method* MEntry = methodEntryOf(*Owner, s.Method);
        if (!MEntry)
            codegenICE("method '" + mangledName + "' reached CodeGen "
                       "unresolved -- Sema should have refused this call "
                       "already");
        callee = declareForeignMethod(Mod, Ctx, PtrTy, Types, *MEntry, mangledName);
    }

    std::vector<llvm::Value*> args;
    args.push_back(selfPtr);

    // Issue #299 Phase 1 / #182 follow-up: the per-argument marshalling loop
    // shared with CGProcCall::emitUserProcCall/CGFuncCall::emitUserFuncCall/
    // emitMethodCallExpr -- see CGCallMarshal.h.  Same as emitMethodCallExpr's
    // own call just below (CGFuncCall.cpp), starting pi/args after Self.
    Marshal.marshalArgs(mangledName, callee->getFunctionType(), s.Args, args);
    // Turbo Tier 5, Cluster A item 5: see CGFuncCall::emitMethodCallExpr's
    // identical branch for the whole design -- a virtual method is called
    // INDIRECTLY, through the receiver's own `_vptr` and Owner's own
    // VmtSlot index, never Owner's mangled symbol directly.
    const Type::Method* MEntry = methodEntryOf(*Owner, s.Method);
    if (MEntry && MEntry->IsVirtual) {
        auto vptrOff = Types.vptrOffsetOf(*s.Receiver->ResolvedType);
        if (!vptrOff)
            codegenICE("virtual method '" + s.Method + "' call has a "
                       "receiver type with no `_vptr` -- Sema should have "
                       "refused a virtual method on a hierarchy with none");
        auto* vptrSlot = B.CreateGEP(I8Ty, selfPtr,
            {llvm::ConstantInt::get(I64Ty, *vptrOff)}, "self.vptr.addr");
        auto* vmt = B.CreateLoad(PtrTy, vptrSlot, "self.vmt");
        // Issue #514: see CGFuncCall::emitMethodCallExpr's identical branch
        // for the whole design -- a plain New(p)'s unstamped `_vptr` slot
        // reads back NULL (plang_new's own calloc), which this traps
        // cleanly (Borland/FPC's "Runtime error 216") instead of segfaulting
        // the GEP+load just below.
        RangeGuards.emitNilCheck(vmt);
        auto* slotAddr = B.CreateGEP(PtrTy, vmt,
            {llvm::ConstantInt::get(I64Ty, MEntry->VmtSlot)}, "vmt.slot.addr");
        auto* fnPtr = B.CreateLoad(PtrTy, slotAddr, "vmt.fn");
        B.CreateCall(callee->getFunctionType(), fnPtr, args);
    } else {
        B.CreateCall(callee, args);
    }
}

// Turbo Tier 5, Cluster A item 6: see this method's own declaration
// (CGProcCall.h) for the whole design.
llvm::Value* CGProcCall::emitBoundMethodCall(
        llvm::Value* selfPtr, const Type& RecvTy, const std::string& Method,
        std::span<const std::unique_ptr<ExprNode>> Args) {
    const Type* Owner = methodOwnerType(RecvTy, Method);
    if (!Owner)
        codegenICE("'" + Method + "' has no owning type in '" + RecvTy.Name
                   + "'s own ancestor chain -- Sema should have refused "
                     "this already");
    std::string mangledName = Linkage.mangledMethod(Owner->Name, Method, Owner->DeclaringModule);

    // Every method THIS TRANSLATION UNIT declares is pre-declared (at
    // minimum) by emitAllProcedures's own method pre-pass before ANY body is
    // emitted -- see emitInheritedCallStmt's own identical comment for why a
    // not-yet-declared fallback (emitMethodCallStmt's own, for a genuine
    // Pascal-source method call reached through a real Receiver expression)
    // is not needed here for THAT reason: New/Init and Dispose/Done both run
    // from inside some OTHER function's own body, which cannot itself be
    // emitted until the pre-pass for its whole block has already run.
    // Turbo Tier 5, Cluster B item 8: Owner may still be declared in a
    // DIFFERENT translation unit than this one (New(P, Init(...)) on a
    // cross-unit object type), which the pre-pass never reaches at all --
    // declareForeignMethod covers exactly that case, from Owner's own
    // resolved Method entry.
    auto* callee = Mod.getFunction(mangledName);
    if (!callee) {
        const Type::Method* MEntry = methodEntryOf(*Owner, Method);
        if (!MEntry)
            codegenICE("'" + mangledName + "' reached CodeGen unresolved -- "
                       "Sema::checkNewInit/checkDisposeDone should have "
                       "refused this already or the method pre-pass should "
                       "have declared it");
        callee = declareForeignMethod(Mod, Ctx, PtrTy, Types, *MEntry, mangledName);
    }

    std::vector<llvm::Value*> args;
    args.push_back(selfPtr);

    // Issue #299 Phase 1 / #182 follow-up: same per-argument marshalling loop
    // emitMethodCallStmt uses just above -- see CGCallMarshal.h.
    Marshal.marshalArgs(mangledName, callee->getFunctionType(), Args, args);

    // A destructor commonly IS virtual in real TP7 idiom (confirmed
    // against a local fpc -Mtp build) -- exactly the point of
    // Dispose(P, Done): a caller holding only an ancestor-typed pointer
    // still reaches the actual runtime type's own destructor.  A
    // constructor can never be virtual (enforced at Sema:
    // err_object_virtual_constructor), so MEntry->IsVirtual is always
    // false for New(P, Init(...))'s own call and this branch is
    // unreachable for it -- one code path serves both, exactly like
    // emitMethodCallStmt's own identical branch just above.
    const Type::Method* MEntry = methodEntryOf(*Owner, Method);
    if (MEntry && MEntry->IsVirtual) {
        auto vptrOff = Types.vptrOffsetOf(RecvTy);
        if (!vptrOff)
            codegenICE("virtual method '" + Method + "' call has a "
                       "receiver type with no `_vptr` -- Sema should have "
                       "refused a virtual method on a hierarchy with none");
        auto* vptrSlot = B.CreateGEP(I8Ty, selfPtr,
            {llvm::ConstantInt::get(I64Ty, *vptrOff)}, "self.vptr.addr");
        auto* vmt = B.CreateLoad(PtrTy, vptrSlot, "self.vmt");
        // Issue #514: see CGFuncCall::emitMethodCallExpr's identical branch
        // for the whole design.  Reached here by Dispose(p, Done) on a
        // VIRTUAL destructor (a constructor can never be virtual --
        // err_object_virtual_constructor -- so New(P, Ctor(...))'s own call
        // never takes this branch): a plain New(p), later Dispose(p, Done)'d
        // without ever having been New(p, Init(...))'d, has the same
        // unstamped, NULL `_vptr` slot as a virtual METHOD call on it would.
        RangeGuards.emitNilCheck(vmt);
        auto* slotAddr = B.CreateGEP(PtrTy, vmt,
            {llvm::ConstantInt::get(I64Ty, MEntry->VmtSlot)}, "vmt.slot.addr");
        auto* fnPtr = B.CreateLoad(PtrTy, slotAddr, "vmt.fn");
        return B.CreateCall(callee->getFunctionType(), fnPtr, args);
    }
    return B.CreateCall(callee, args);
}

// Turbo Tier 5, issue #622: see this method's own declaration (CGProcCall.h)
// for the whole design.
llvm::Value* CGProcCall::emitNewObjectValue(const Type& Pointee,
                                            const ExprNode& CtorArg) {
    int64_t Bytes = (int64_t)Mod.getDataLayout().getTypeAllocSize(
        Types.llvmTypeOfSemaType(Pointee));
    auto* ptr = B.CreateCall(RtFns.getRuntimeNewFn(),
                                   {llvm::ConstantInt::get(I64Ty, Bytes)});
    StampVptr(ptr, Pointee);
    // Issue #511: Pointee's own OBJECT-typed fields (a value-composed
    // member, not an ancestor -- an ancestor's vptr is the SAME slot
    // StampVptr just above already found) need their own '_vptr' too,
    // exactly like a directly declared instance of Pointee would get from
    // emitVarValueInit -- see StampFieldVptrs's own comment (CGProcCall.h).
    StampFieldVptrs(ptr, Pointee);

    std::string ctorName;
    std::span<const std::unique_ptr<ExprNode>> ctorArgs;
    if (auto* CE = llvm::dyn_cast<CallExpr>(&CtorArg)) {
        ctorName = CE->Name;
        ctorArgs = CE->Args;
    } else if (auto* Id = llvm::dyn_cast<IdentExpr>(&CtorArg)) {
        ctorName = Id->Name;
    } else {
        codegenICE("New(..., Ctor) reached CodeGen with an unrecognized "
                   "constructor-argument shape -- Sema::checkNewInit/"
                   "checkNewInitExpr should have refused this already");
    }
    auto* ok = emitBoundMethodCall(ptr, Pointee, ctorName, ctorArgs);

    auto* curFn = B.GetInsertBlock()->getParent();
    auto* okBB   = llvm::BasicBlock::Create(Ctx, "new.ctor.ok", curFn);
    auto* failBB = llvm::BasicBlock::Create(Ctx, "new.ctor.fail", curFn);
    auto* contBB = llvm::BasicBlock::Create(Ctx, "new.ctor.cont", curFn);
    B.CreateCondBr(ok, okBB, failBB);

    B.SetInsertPoint(okBB);
    B.CreateBr(contBB);

    B.SetInsertPoint(failBB);
    B.CreateCall(RtFns.getRuntimeDisposeFn(), {ptr});
    B.CreateBr(contBB);

    B.SetInsertPoint(contBB);
    auto* result = B.CreatePHI(PtrTy, 2, "new.result");
    result->addIncoming(ptr, okBB);
    result->addIncoming(llvm::ConstantPointerNull::get(PtrTy), failBB);
    return result;
}

// Turbo Tier 5, Cluster A item 5: see this method's own declaration
// (CGProcCall.h) for the whole design.
void CGProcCall::emitInheritedCallStmt(const InheritedCallStmt& s) {
    // Issue #624: bare 'inherited;' with no ancestor at all -- Sema
    // (Sema::checkInheritedCall) leaves ResolvedMethod/ImplementingType
    // both empty for exactly this one legal combination (see its own
    // comment) rather than refusing it, so this is not the "should never
    // happen" case the ICE below still guards for the explicit form (which
    // Sema always errors out of before CodeGen runs at all). Nothing to
    // call -- emit nothing.
    if (s.Method.empty() && s.ImplementingType.empty()) return;
    if (s.ImplementingType.empty() || s.ResolvedMethod.empty())
        codegenICE("'inherited' reached CodeGen unresolved -- "
                   "Sema::checkInheritedCallStmt should have refused this "
                   "already or filled in ImplementingType/ResolvedMethod");
    std::string mangledName = Linkage.mangledMethod(s.ImplementingType, s.ResolvedMethod,
                                                    s.ImplementingModule);

    // Every method THIS TRANSLATION UNIT declares is pre-declared (at
    // minimum) by emitAllProcedures's own method pre-pass before ANY body --
    // including this one, whichever method contains this 'inherited' -- is
    // emitted, so within one translation unit this is never the "not yet
    // defined" case CGFuncCall::emitMethodCallExpr's own static-call
    // fallback has to handle: an out-of-line ancestor method body always has
    // at least a declaration standing under its mangled name by the time any
    // OTHER method's own body starts.
    //
    // Turbo Tier 5, Cluster B item 8: the ancestor 'inherited' reaches may
    // now be declared in a DIFFERENT translation unit than this one (its own
    // out-of-line body was never part of this compile, and never will be),
    // which the pre-pass cannot reach at all -- declared here instead, on
    // first use.
    auto* callee = Mod.getFunction(mangledName);
    if (!callee) {
        if (s.Method.empty()) {
            // Bare 'inherited;' forwards this activation's own parameters
            // positionally and unchanged (just below) -- Sema's own
            // override-signature check (resolveObjectType) already
            // guarantees the ancestor's own parameter list is identical to
            // the CURRENTLY EXECUTING function's, so that function's own
            // FunctionType (curFn, read a few lines down -- fetched once
            // more here, before it is otherwise needed, purely to build this
            // declaration) is exactly the signature to declare, with no
            // Type::Method lookup needed at all.
            llvm::Function* enclosing = B.GetInsertBlock()->getParent();
            callee = llvm::Function::Create(enclosing->getFunctionType(),
                                            llvm::GlobalValue::ExternalLinkage,
                                            mangledName, &Mod);
        } else {
            // Issue #682: this declaration's own shape -- and the
            // marshalling metadata DeclareForeignInheritedCallee also
            // registers -- must come from the ANCESTOR METHOD'S OWN
            // RESOLVED SIGNATURE (s.ImplementingOwnerType, filled in by
            // Sema::checkInheritedCallStmt as the real Type object
            // MethodSym was found on), read off through methodEntryOf
            // exactly the way an ordinary cross-unit method call already
            // builds ITS OWN foreign declaration (declareForeignMethod,
            // just above in this file) -- NEVER from this call's own
            // written argument expressions (s.Args' ResolvedType), which
            // was the bug: a `var`/const-by-reference/set-typed formal's
            // real shape is invisible from the actual's own static type (an
            // Integer actual passed to a `var x: Integer` formal looks
            // identical, at the call site, to one passed to a plain
            // `x: Integer` formal), so the guessed declaration passed
            // ordinary values where the real (separately-compiled) callee
            // expects pointers -- a caller/callee ABI mismatch invisible to
            // LLVM's own verifier (both sides individually well-typed) and
            // only surfacing as a segfault, or silently unrebased set bits,
            // at run time (confirmed via disassembly: the caller passed a
            // 16-bit value where the callee, compiled separately with the
            // real `var` parameter, expected a pointer, and dereferenced it).
            if (!s.ImplementingOwnerType)
                codegenICE("'inherited " + s.Method + "' reached CodeGen with "
                           "no resolved ancestor Type -- Sema::checkInheritedCallStmt "
                           "should have refused this already or filled in "
                           "ImplementingOwnerType");
            const Type::Method* MEntry =
                methodEntryOf(*s.ImplementingOwnerType, s.ResolvedMethod);
            if (!MEntry)
                codegenICE("'inherited " + s.Method + "' resolved to '"
                           + mangledName + "', which cannot be found on its "
                             "own ImplementingOwnerType -- Sema should have "
                             "refused this already");
            callee = DeclareForeignInheritedCallee(*MEntry, mangledName);
        }
    }

    // The CURRENTLY EXECUTING function's own Self -- forwarded unchanged,
    // never re-read through the 'Self' symbol table entry, so this works
    // identically whether or not the enclosing method happens to still have
    // 'Self' in scope under that name.
    llvm::Function* curFn = B.GetInsertBlock()->getParent();
    if (curFn->arg_size() == 0)
        codegenICE("'inherited' used inside a function with no 'Self' "
                   "parameter -- Sema::checkInheritedCallStmt should have "
                   "refused this outside a method body already");
    llvm::Value* selfPtr = curFn->getArg(0);

    std::vector<llvm::Value*> args;
    args.push_back(selfPtr);

    if (s.Method.empty()) {
        // Bare 'inherited;': this activation's own remaining parameters,
        // forwarded positionally with no re-marshalling -- see this
        // function's own declaration comment for why that is sound.
        for (unsigned i = 1; i < curFn->arg_size(); ++i)
            args.push_back(curFn->getArg(i));
        B.CreateCall(callee, args);
        return;
    }

    // Issue #299 Phase 1 / #182 follow-up: same per-argument marshalling loop
    // emitMethodCallStmt uses just above -- an explicit 'inherited
    // Method(args)' takes its own written argument list exactly like an
    // ordinary method call does.  See CGCallMarshal.h.
    Marshal.marshalArgs(mangledName, callee->getFunctionType(), s.Args, args);
    B.CreateCall(callee, args);
}

void CGProcCall::emitUserProcCall(const CallStmt& s) {
    if (tryEmitDosProcCall(s)) return;
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

    // Issue #299 Phase 1: the per-argument marshalling loop shared with
    // CGFuncCall::emitUserFuncCall/emitMethodCallExpr -- see CGCallMarshal.h.
    Marshal.marshalArgs(mangledName, callee->getFunctionType(), s.Args, args);
    B.CreateCall(callee, args);
}
