#include "BuiltinIO.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"

#include "CodegenICE.h"

using namespace plang;

// ====================================================================
// Built-in write / writeln / read — dispatch to plang_runtime functions
// ====================================================================

// -std=turbo: every file-directed plang_writeln_file call site in this file
// (a bare writeln(f), and the trailing newline after every write(f,...)/
// writeln(f,...) value) has to dispatch to plang_writeln_file_turbo instead
// (runtime/plang_file.cpp) -- this item's P7-rule choke point: dialect
// selection happens at CODEGEN TIME, through which symbol codegen calls,
// never inside the runtime function itself.  Centralized here rather than
// inlining the same `Opts.turbo() ? ..._turbo : ...` ternary at each of the
// several call sites below.
void BuiltinIO::emitWritelnFile(llvm::Value* fp) {
    B.CreateCall(RtFns.getExternFnN(fileFn("plang_writeln_file"),
        llvm::Type::getVoidTy(Ctx), {PtrTy}), {fp});
}

/// -std=turbo only: see this method's own declaration (BuiltinIO.h).
llvm::Value* BuiltinIO::turboStdFilePtr(bool isInput) const {
    if (!Opts.turbo()) return nullptr;
    auto* ve = SymTab.findVar(isInput ? "Input" : "Output");
    return ve ? ve->ptr : nullptr;
}

void BuiltinIO::emitBuiltinWrite(const std::vector<std::unique_ptr<ExprNode>>& args, bool newline) {
    // Detect file variable as first argument: write(f, ...) vs write(...)
    size_t start = 0;
    llvm::Value* fp = nullptr;
    bool binaryTyped = false;
    if (!args.empty() && FileVars.isFileVar(*args[0])) {
        fp          = FileVars.fileVarPtr(*args[0]);
        start       = 1;
        binaryTyped = FileVars.isTypedBinaryFileVar(*args[0]);
    } else {
        // -std=turbo only: a bare write/writeln with no explicit file
        // argument now means the predefined Output variable, which
        // Assign/Rewrite may have redirected away from stdout -- routing
        // it through fp (Output's own storage), the SAME file-pointer-
        // taking codepath every other file write already uses below,
        // rather than leaving fp null (which every helper in this file
        // still reads as "write straight to the console", ISO/EP's own
        // unredirectable case, left completely untouched by this branch).
        fp = turboStdFilePtr(/*isInput=*/false);
    }

    if (start >= args.size()) {
        // writeln with no value arguments (just the newline/file)
        if (newline) {
            if (fp)
                emitWritelnFile(fp);
            else
                B.CreateCall(RtFns.getRuntimeFn("plang_writeln", nullptr), {});
        }
        return;
    }

    emitWriteArgs(args, start, newline, fp, binaryTyped);
}

/// Lowers args[start..] as write-parameters.  Shared by write/writeln and by
/// writestr, which supplies its own destination and no file.
void BuiltinIO::emitWriteArgs(
        const std::vector<std::unique_ptr<ExprNode>>& args, size_t start,
        bool newline, llvm::Value* fp, bool binaryTyped, size_t end) {
    if (end > args.size()) end = args.size();
    for (size_t i = start; i < end; ++i) {
        bool addNl = newline && (i == end - 1);
        // Check for WriteParam (field-width formatting)
        const ExprNode* argExpr = args[i].get();
        llvm::Value*    width   = nullptr;
        llvm::Value*    decimals = nullptr;
        if (auto* wp = llvm::dyn_cast<WriteParam>(argExpr)) {
            argExpr  = wp->Value.get();
            // Sema only requires Width/Decimals to be isIntegral()
            // (SemaExpr.cpp's WriteParam arm), so either can be a signed
            // narrow or unsigned wide Turbo ordinal (issue #177's sibling
            // audit).
            width    = wp->Width    ? ToI64(EmitExpr(*wp->Width), exprIsSigned(*wp->Width))
                                     : nullptr;
            decimals = wp->Decimals ? ToI64(EmitExpr(*wp->Decimals), exprIsSigned(*wp->Decimals))
                                     : nullptr;
        }

        // Binary typed file: write raw bytes.
        if (binaryTyped && fp) {
            // A VarString component's in-memory shape -- { i64 length, [N x
            // i8] data } -- IS the record ISO §6.9.1 wants written, byte for
            // byte, no format conversion.  But emitExpr for a VarString
            // returns its ADDRESS, not a loaded struct value (every other
            // caller wants a pointer to pass to the string runtime), and the
            // generic path below stores whatever emitExpr returned into a
            // temporary and writes that temporary's own size -- storing an
            // 8-byte pointer and then reporting the 8-vs-88-byte mismatch
            // against the file's real component size.  Used directly, the
            // address emitExpr already returns is exactly what a straight
            // memcpy into the file wants; nothing to store first.
            if (ExprIsVarStr(*argExpr)) {
                auto* addr = EmitExpr(*argExpr);
                if (!addr) continue;
                auto* compTy = FileVars.getFileElemType(*args[0]);
                if (!compTy)
                    codegenICE("a binary typed file with no component type to "
                               "size its writes by");
                const auto& dl  = Mod.getDataLayout();
                const int64_t esz = (int64_t)dl.getTypeAllocSize(compTy);
                B.CreateCall(
                    RtFns.getExternFnN(fileFn("plang_write_binary"), llvm::Type::getVoidTy(Ctx),
                                 {PtrTy, PtrTy, I64Ty}),
                    {fp, addr, llvm::ConstantInt::get(I64Ty, esz)});
                continue;
            }
            auto* val = EmitExpr(*argExpr);
            if (!val) continue;
            // ISO §6.9.1: write(f,e) is f^ := e, so what lands in the file is a
            // component.  An integer written to a file of real has to widen
            // first, or the bytes would be read back as a real.
            //
            // -std=turbo: TP's typed Read/Write requires the value's type to
            // be IDENTICAL to the file's component type, not just assignment
            // compatible -- Sema (SemaStmt.cpp's read/write arms,
            // err_turbo_typed_file_exact_type) already refuses a mismatched
            // Turbo program before it reaches codegen, so this call is
            // reachable under Turbo only when val's LLVM type already equals
            // compTy and CoerceToType would be a no-op -- but skipping it
            // outright here, rather than relying on that invariant, is what
            // actually keeps codegen from EVER performing ISO's implicit
            // widening for Turbo, matching real Turbo Pascal (no `writeln`-
            // style numeric promotion on a typed file the way ISO's f^ := e
            // assignment-compatibility rule allows).
            if (!Opts.turbo())
                if (auto* compTy = FileVars.getFileElemType(*args[0]))
                    if (compTy->isSingleValueType() && val->getType()->isSingleValueType())
                        val = CoerceToType(val, compTy);
            // Store the value to a temporary alloca so we can pass its address.
            auto* tmp = CreateEntryAlloca(val->getType(), "bin.wr.tmp");
            B.CreateStore(val, tmp);
            // R6: how many bytes land in the file is a fact about the FILE's
            // COMPONENT TYPE, not about whatever emitExpr happened to produce.
            // Taking it from the value made the file's shape depend on how the
            // expression was lowered, which is the same class as sizing an
            // object from a name: right whenever the two agree and unnoticed
            // when they stop.  Measured before the change: no test in the suite
            // had them differ -- so this is not a bug fix, it is removing the
            // way one could arrive without anything saying so.
            auto* compTy = FileVars.getFileElemType(*args[0]);
            if (!compTy)
                codegenICE("a binary typed file with no component type to size "
                           "its writes by");
            const auto& dl  = Mod.getDataLayout();
            const int64_t esz = (int64_t)dl.getTypeAllocSize(compTy);
            // The temporary holds the VALUE, so writing the component's width
            // out of it is only sound while the two are the same size.  They
            // are, everywhere the suite reaches; if they ever stop, this says
            // so rather than reading past the temporary.
            if (dl.getTypeAllocSize(val->getType()) != dl.getTypeAllocSize(compTy))
                codegenICE("a value written to a typed file is not the width of "
                           "the file's component");
            B.CreateCall(
                RtFns.getExternFnN(fileFn("plang_write_binary"), llvm::Type::getVoidTy(Ctx),
                             {PtrTy, PtrTy, I64Ty}),
                {fp, tmp, llvm::ConstantInt::get(I64Ty, esz)});
            continue;
        }
        // Turbo string[N]: its own writer, with its own (one-byte-header)
        // runtime entry points -- checked ahead of the VarString/CharStr
        // branch below, since ShortString is neither.  StrCall.emitStrAddr,
        // not EmitLValue: argExpr may be a COMPUTED ShortString value with no
        // storage of its own -- `writeln(s + t)`, most concretely, now that
        // this item's own concatenation support (CGBinaryOps.cpp) makes that
        // a real, expected expression -- and EmitLValue has no BinaryExpr
        // case at all, so it silently returned null for one and this whole
        // write argument vanished with no diagnostic (`continue`, just
        // below, reads as "not a writable string" rather than "not
        // addressable at all").  emitStrAddr already falls back to EmitExpr
        // for exactly this case (EmitExpr's own ShortString IdentExpr/Index/
        // Field/Deref cases, CGExprCore.cpp, all return an address too, by
        // the same "a string value is carried by its address" contract
        // VarString already established).
        if (argExpr->ResolvedType && argExpr->ResolvedType->Kind == TypeKind::ShortString) {
            auto* addr = StrCall.emitStrAddr(*argExpr);
            if (!addr) continue;
            auto* capV = i64c(argExpr->ResolvedType->StrCapacity);
            auto* voidTy = llvm::Type::getVoidTy(Ctx);
            if (fp) {
                // plang_sstr_write_file(_w) are PascalFile-aware, one-byte-
                // header writers of their own -- see plang_file.cpp's own
                // comment.  The newline is a separate plang_writeln_file(fp)
                // call, the same convention every other typed file write in
                // this function already follows (VarString/CharStr's branch
                // just below, and the scalar path further down).
                if (width)
                    B.CreateCall(
                        RtFns.getExternFnN("plang_sstr_write_file_w", voidTy,
                                     {PtrTy, PtrTy, I64Ty, I64Ty}),
                        {fp, addr, capV, width});
                else
                    B.CreateCall(
                        RtFns.getExternFnN("plang_sstr_write_file", voidTy,
                                     {PtrTy, PtrTy, I64Ty}),
                        {fp, addr, capV});
                if (addNl)
                    emitWritelnFile(fp);
                continue;
            }
            if (width) {
                auto* fn = Strings.getStrFn(
                    addNl ? "plang_sstr_writeln_w" : "plang_sstr_write_w",
                    voidTy, {PtrTy, I64Ty, I64Ty});
                B.CreateCall(fn, {addr, capV, width});
            } else {
                auto* fn = Strings.getStrFn(addNl ? "plang_sstr_writeln" : "plang_sstr_write",
                    voidTy, {PtrTy, I64Ty});
                B.CreateCall(fn, {addr, capV});
            }
            continue;
        }
        // VarString arguments → string runtime; everything else → scalar write.
        // ISO §6.4.3.2: a packed array[1..n] of char is written as the string
        // it is, which the string writers already do once it is shaped like
        // one.  Writing it as a scalar reached plang_write_str, which reads
        // until a terminator the array does not have.
        if (ExprIsVarStr(*argExpr) || ExprIsCharStr(*argExpr)) {
            const bool chars = ExprIsCharStr(*argExpr);
            // R5: one walk for both, or the subscripts on the way to the
            // string are emitted twice.
            auto [strP, strC] = chars ? std::pair<llvm::Value*, llvm::Value*>{}
                                      : Schema.strAddrAndCap(*argExpr);
            auto* sptr = chars ? StrCall.emitCharStrAsStr(*argExpr) : strP;
            if (!sptr) continue;
            auto* capV = chars ? i64c(ExprCharStrLen(*argExpr)) : strC;
            if (fp) {
                // string(N) is not null-terminated, so it needs its own writer
                // rather than the char* one the generic path would pick.  A
                // field width applies here as much as on the standard output;
                // dropping it wrote the whole string into a field too small.
                if (width)
                    B.CreateCall(
                        RtFns.getExternFnN(fileFn("plang_str_write_file_w"),
                                     llvm::Type::getVoidTy(Ctx),
                                     {PtrTy, PtrTy, I64Ty, I64Ty}),
                        {fp, sptr, capV, width});
                else
                    B.CreateCall(
                        RtFns.getExternFnN(fileFn("plang_str_write_file"),
                                     llvm::Type::getVoidTy(Ctx),
                                     {PtrTy, PtrTy, I64Ty}),
                        {fp, sptr, capV});
                if (addNl)
                    emitWritelnFile(fp);
            } else if (width) {
                auto* fn = Strings.getStrFn(addNl ? "plang_str_writeln_w" : "plang_str_write_w",
                    llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty, I64Ty});
                B.CreateCall(fn, {sptr, capV, width});
            } else {
                auto* fn = Strings.getStrFn(addNl ? "plang_str_writeln" : "plang_str_write",
                    llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty});
                B.CreateCall(fn, {sptr, capV});
            }
        } else {
            auto* val = EmitExpr(*argExpr);
            if (!val) continue;
            const plang::Type* semaTy = argExpr->ResolvedType.get();
            if (width) emitWriteValueFormatted(val, width, decimals, addNl, fp, semaTy);
            else       emitWriteValue(val, addNl, fp, semaTy);
        }
    }
}

// Emit a write call, dispatching on LLVM type.
// fp: file pointer (null = stdout).
void BuiltinIO::emitWriteValue(llvm::Value* val, bool newline, llvm::Value* fp,
                                   const plang::Type* semaTy) {
    llvm::Type* ty  = val->getType();
    // Turbo `Single` reuses the f64 writers Real (64-bit) already has,
    // promoted here rather than duplicating plang_real.cpp's formatting
    // logic for a second width -- the runtime never sees anything but a
    // double.  wasSingle survives the promotion below so the f64 dispatch
    // arm can still tell a Single from a genuine Real and route to the
    // significant-digit-capped f32 writers (see plang_real.h's
    // PlangSingleMaxDecPlaces) instead of double's own -- without it, a
    // Single's default write showed the promotion's own double-precision
    // noise past the 32-bit value's actual ~9 significant digits.
    const bool wasSingle = ty->isFloatTy();
    if (ty->isFloatTy()) {
        val = B.CreateFPExt(val, DblTy, "single.widen");
        ty  = val->getType();
    }
    // EP §6.9.3.6: a complex is written as its two components, so it needs a
    // writer of its own rather than one of the scalar ones.
    if (ty == Complex.complexTy()) {
        auto* re = B.CreateExtractValue(val, 0, "cplx.re");
        auto* im = B.CreateExtractValue(val, 1, "cplx.im");
        auto* voidT = llvm::Type::getVoidTy(Ctx);
        auto* upper = turboFlag();
        if (fp) {
            B.CreateCall(RtFns.getExternFnN("plang_write_file_cplx", voidT,
                                            {PtrTy, DblTy, DblTy, I8Ty}), {fp, re, im, upper});
            if (newline)
                B.CreateCall(
                    RtFns.getExternFnN("plang_writeln_file", voidT, {PtrTy}), {fp});
        } else {
            B.CreateCall(
                RtFns.getExternFnN(newline ? "plang_writeln_cplx" : "plang_write_cplx",
                             voidT, {DblTy, DblTy, I8Ty}), {re, im, upper});
        }
        return;
    }
    const bool isBool = writesAsBoolean(ty, semaTy);
    const bool isChar = !isBool && writesAsChar(ty, semaTy);
    if (isChar && !ty->isIntegerTy(8)) {
        val = B.CreateTrunc(val, I8Ty, "char.narrow");
        ty  = val->getType();
    } else if (!isBool && !isChar && ty->isIntegerTy() && !ty->isIntegerTy(64)) {
        // Every ISO 7185/EP ordinal that reaches here is already i64 (both
        // dialects stamp Width=64 on Integer/Subrange/Enum); Turbo's 16-bit
        // Integer is the first thing that can arrive narrower.  Without this,
        // the chain below falls all the way to the string writer for the
        // narrower width, since none of isIntegerTy(64)/isDoubleTy/isBool/
        // isIntegerTy(8) match it -- passing a non-pointer integer where the
        // runtime's string writer expects a ptr is an LLVM IR verifier abort,
        // not a silently wrong answer.
        const bool signedExt = !semaTy || semaTy->IsSigned;
        val = signedExt ? B.CreateSExt(val, I64Ty, "int.widen")
                         : B.CreateZExt(val, I64Ty, "int.widen");
        ty  = val->getType();
    }
    auto* voidTy    = llvm::Type::getVoidTy(Ctx);
    std::string pfx = fp ? "plang_write_file" : (std::string("plang_write") + (newline ? "ln" : ""));

    auto callStdout = [&](const std::string& fn, llvm::Type* pt, llvm::Value* v) {
        B.CreateCall(RtFns.getRuntimeFn(fn, pt), {v});
    };
    // -std=turbo: resolved to the `_turbo` sibling (fileFn) -- Write/Writeln
    // are ALL-dialect builtins, so every scalar file writer below is shared
    // with ISO/EP and needs this item's P7-rule choke point.
    auto callFile = [&](const std::string& fn, llvm::Type* pt, llvm::Value* v) {
        B.CreateCall(RtFns.getExternFnN(fileFn(fn), voidTy, {PtrTy, pt}), {fp, v});
    };

    if (ty->isIntegerTy(64)) {
        // QWord (64-bit unsigned) is the one ordinal a signed formatter gets
        // wrong -- see plang_write_u64's own comment: every narrower
        // unsigned rung was already zero-extended to i64 above, so it never
        // sets the sign bit and the signed/unsigned writers agree; only a
        // genuinely 64-bit-wide unsigned value can disagree.
        const bool uns = writesAsUnsigned64(semaTy);
        fp ? callFile(uns ? "plang_write_file_u64" : "plang_write_file_i64", I64Ty, val)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + (uns ? "_u64" : "_i64"), I64Ty, val);
    } else if (ty->isDoubleTy()) {
        // f64/bool both take an extra Upper flag now (the Turbo real-format
        // profile / TRUE-FALSE spelling), so they go through getExternFnN's
        // full parameter list directly rather than callFile/callStdout's
        // one-value shape (getRuntimeFn, which callStdout wraps, only builds
        // a single-argument signature).
        auto* upper = turboFlag();
        const char* fam = wasSingle ? "f32" : "f64";
        if (fp)
            B.CreateCall(RtFns.getExternFnN(fileFn(std::string("plang_write_file_") + fam), voidTy,
                             {PtrTy, DblTy, I8Ty}), {fp, val, upper});
        else
            B.CreateCall(RtFns.getExternFnN(std::string("plang_write") + (newline?"ln":"") + "_" + fam,
                             voidTy, {DblTy, I8Ty}), {val, upper});
    } else if (isBool) {
        auto* ext = toBoolByte(val);
        auto* upper = turboFlag();
        if (fp)
            B.CreateCall(RtFns.getExternFnN(fileFn("plang_write_file_bool"), voidTy, {PtrTy, I8Ty, I8Ty}),
                         {fp, ext, upper});
        else
            B.CreateCall(RtFns.getExternFnN(std::string("plang_write") + (newline?"ln":"") + "_bool",
                             voidTy, {I8Ty, I8Ty}), {ext, upper});
    } else if (ty->isIntegerTy(8)) {
        fp ? callFile("plang_write_file_char", I8Ty, val)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_char", I8Ty, val);
    } else {
        fp ? callFile("plang_write_file_str", PtrTy, val)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_str", PtrTy, val);
    }
    // File writeln: append newline after value
    if (fp && newline)
        emitWritelnFile(fp);
}

// Emit a write call with field-width (and optional decimal-places) formatting.
void BuiltinIO::emitWriteValueFormatted(llvm::Value* val, llvm::Value* w, llvm::Value* d,
                                             bool newline, llvm::Value* fp,
                                             const plang::Type* semaTy) {
    if (!w) { emitWriteValue(val, newline, fp, semaTy); return; } // no width
    auto* voidTy = llvm::Type::getVoidTy(Ctx);
    std::string nl = newline ? "ln" : "";
    llvm::Type* ty = val->getType();
    // See the identical promotion in emitWriteValue: Single reuses Real's
    // f64 writers for the fixed-decimals (:w:d) form, but the exponential
    // (:w, no decimals) form needs to still tell a Single apart post-
    // promotion -- see wasSingle's use below and emitWriteValue's identical
    // comment on why.
    const bool wasSingle = ty->isFloatTy();
    if (ty->isFloatTy()) {
        val = B.CreateFPExt(val, DblTy, "single.widen");
        ty  = val->getType();
    }
    if (ty == Complex.complexTy()) {
        auto* re = B.CreateExtractValue(val, 0, "cplx.re");
        auto* im = B.CreateExtractValue(val, 1, "cplx.im");
        auto* dv = d ? d : llvm::ConstantInt::get(I64Ty, -1, true);
        auto* upper = turboFlag();
        if (fp) {
            B.CreateCall(
                RtFns.getExternFnN("plang_write_file_cplx_w", voidTy,
                             {PtrTy, DblTy, DblTy, I64Ty, I64Ty, I8Ty}),
                {fp, re, im, w, dv, upper});
            if (newline)
                B.CreateCall(
                    RtFns.getExternFnN("plang_writeln_file", voidTy, {PtrTy}), {fp});
        } else {
            B.CreateCall(
                RtFns.getExternFnN("plang_write" + nl + "_cplx_w", voidTy,
                             {DblTy, DblTy, I64Ty, I64Ty, I8Ty}),
                {re, im, w, dv, upper});
        }
        return;
    }
    const bool isBool = writesAsBoolean(ty, semaTy);
    const bool isChar = !isBool && writesAsChar(ty, semaTy);
    if (isChar && !ty->isIntegerTy(8)) {
        val = B.CreateTrunc(val, I8Ty, "char.narrow");
        ty  = val->getType();
    } else if (!isBool && !isChar && ty->isIntegerTy() && !ty->isIntegerTy(64)) {
        // Same widening as the unformatted emitWriteValue above, needed here
        // too since a `writeln(i:5)` field-width form goes through this
        // separate dispatch chain with an identical isIntegerTy(64)/isDoubleTy/
        // isBool/isIntegerTy(8)/else-string shape.
        const bool signedExt = !semaTy || semaTy->IsSigned;
        val = signedExt ? B.CreateSExt(val, I64Ty, "int.widen")
                         : B.CreateZExt(val, I64Ty, "int.widen");
        ty  = val->getType();
    }

    // A file destination takes a different family of writers, and the newline
    // is a separate call rather than part of the name.
    if (fp) {
        // -std=turbo: resolved to the `_turbo` sibling (fileFn), same as
        // emitWriteValue's own callFile just above -- these field-width
        // writers are just as shared with ISO/EP.
        auto callFile = [&](const std::string& fn,
                            std::initializer_list<llvm::Type*> argTys,
                            std::initializer_list<llvm::Value*> args) {
            std::vector<llvm::Type*>  tys{PtrTy};
            std::vector<llvm::Value*> vs{fp};
            tys.insert(tys.end(), argTys);
            vs.insert(vs.end(), args);
            B.CreateCall(RtFns.getExternFnN(fileFn(fn), voidTy, tys), vs);
        };
        // Upper (f64/bool)/NoTrunc (bool/str)/AlwaysWrite (char): the same
        // single CodeGen-resolved isTurbo() fact, threaded as a trailing
        // extra argument per plang_io.cpp's convention -- see turboFlag()'s
        // own comment.  One flag value serves every parameter slot below,
        // since they are all exactly this one fact, just named for what
        // each callee does with it.
        auto* turbo = turboFlag();
        if (ty->isIntegerTy(64)) {
            // See emitWriteValue's identical QWord split.
            callFile(writesAsUnsigned64(semaTy) ? "plang_write_file_u64_w" : "plang_write_file_i64_w",
                     {I64Ty, I64Ty}, {val, w});
        } else if (ty->isDoubleTy()) {
            // A fixed decimals clause (:w:d) is left on the shared f64
            // formatter even for a Single -- the requested D already bounds
            // how many decimal digits print, so there is no unbounded-noise
            // case for it to cap the way the exponential (:w alone) form
            // needs to.
            if (d) callFile("plang_write_file_f64_f", {DblTy, I64Ty, I64Ty, I8Ty}, {val, w, d, turbo});
            else   callFile(wasSingle ? "plang_write_file_f32_e" : "plang_write_file_f64_e",
                             {DblTy, I64Ty, I8Ty}, {val, w, turbo});
        } else if (isBool) {
            auto* ext = toBoolByte(val);
            callFile("plang_write_file_bool_w", {I8Ty, I64Ty, I8Ty, I8Ty}, {ext, w, turbo, turbo});
        } else if (ty->isIntegerTy(8)) {
            callFile("plang_write_file_char_w", {I8Ty, I64Ty, I8Ty}, {val, w, turbo});
        } else {
            callFile("plang_write_file_str_w", {PtrTy, I64Ty, I8Ty}, {val, w, turbo});
        }
        if (newline)
            emitWritelnFile(fp);
        return;
    }

    auto* turbo = turboFlag();
    if (ty->isIntegerTy(64)) {
        // See emitWriteValue's identical QWord split.
        const std::string suf = writesAsUnsigned64(semaTy) ? "_u64_w" : "_i64_w";
        B.CreateCall(RtFns.getExternFnN("plang_write" + nl + suf, voidTy, {I64Ty, I64Ty}),
                           {val, w});
    } else if (ty->isDoubleTy()) {
        if (d)
            B.CreateCall(RtFns.getExternFnN("plang_write" + nl + "_f64_f", voidTy, {DblTy, I64Ty, I64Ty, I8Ty}),
                               {val, w, d, turbo});
        else
            B.CreateCall(RtFns.getExternFnN("plang_write" + nl + (wasSingle ? "_f32_e" : "_f64_e"),
                               voidTy, {DblTy, I64Ty, I8Ty}), {val, w, turbo});
    } else if (isBool) {
        auto* ext = toBoolByte(val);
        B.CreateCall(RtFns.getExternFnN("plang_write" + nl + "_bool_w", voidTy, {I8Ty, I64Ty, I8Ty, I8Ty}),
                           {ext, w, turbo, turbo});
    } else if (ty->isIntegerTy(8)) {
        B.CreateCall(RtFns.getExternFnN("plang_write" + nl + "_char_w", voidTy, {I8Ty, I64Ty, I8Ty}),
                           {val, w, turbo});
    } else {
        B.CreateCall(RtFns.getExternFnN("plang_write" + nl + "_str_w", voidTy, {PtrTy, I64Ty, I8Ty}),
                           {val, w, turbo});
    }
}

// Helper to determine read type for an argument.
//
// Width alone used to decide the i8 case: any i8 destination was taken for a
// Char, the same shortcut writesAsChar's own comment describes -- true before
// Turbo's sized-integer ladder existed (ISO 7185/EP stamp Width=64 on every
// Integer), false now that ShortInt/Byte also lower to i8.  A ShortInt/Byte
// destination reaching plang_read_char read exactly one raw character and
// used ITS ASCII CODE as the value ("200" read '2' == 50), rather than
// parsing the token as a number -- semaTy's own Kind, peeled through a
// subrange the same way the Real/Char overrides at this function's call site
// already do, is what actually answers "is this a char".
std::string BuiltinIO::readFnSuffix(llvm::Type* ty, const plang::Type* semaTy) {
    if (ty->isDoubleTy()) return "_f64";
    if (ty->isIntegerTy(8)) {
        if (!semaTy) return "_char"; // no Sema type: keep the old guess
        const plang::Type* t = semaTy;
        while (t->Kind == TypeKind::Subrange && t->SubBase) t = t->SubBase.get();
        return t->Kind == TypeKind::Char ? "_char" : "_i64";
    }
    return "_i64";
}

/// Reads one variable from fp (or stdin when fp is null).  Does not touch the
/// line terminator, so callers compose readln as "read each, then skip line".
void BuiltinIO::emitReadArg(const ExprNode& arg, llvm::Value* fp) {
    // R5, and the same defect emitAssign already carried: a string whose
    // capacity a discriminant fixes needs an address AND a capacity, and
    // emitLValue and exprStrCapV each resolve the access path from scratch.
    // Every subscript on the way to the string was emitted more than once, so
    // `read(q^.a[next].s)` called `next` three times.  ISO §6.9.1 evaluates
    // each variable-access of a read once.  One walk, both answers.
    llvm::Value* addr = nullptr;
    llvm::Value* cap  = nullptr;
    if (ExprIsVarStr(arg) && arg.ResolvedType->ExtentVaries)
        if (auto path = Schema.schemaPathOf(arg))
            if (auto* c = Schema.strCapFromPath(*path)) { addr = path->addr; cap = c; }

    // Returning quietly would leave the variable out of the read entirely.
    if (!addr) addr = EmitLValue(arg);
    if (!addr) codegenICE("read/readln target is not an assignable variable");

    // Turbo string[N]: a minimal reader of its own, the read-side twin of
    // emitWriteArgs's ShortString branch above -- see plang_sstr.cpp's own
    // comment for scope.  Checked ahead of the VarString check just below:
    // ShortString is a completely separate TypeKind with its own (one-byte
    // header) runtime reader, not a VarString variant.
    if (arg.ResolvedType && arg.ResolvedType->Kind == TypeKind::ShortString) {
        auto* capV = i64c(arg.ResolvedType->StrCapacity);
        if (fp)
            // plang_sstr_read_file is PascalFile-aware -- the read-side twin
            // of emitWriteArgs's plang_sstr_write_file above; see
            // plang_file.cpp's own comment.
            B.CreateCall(
                RtFns.getExternFnN("plang_sstr_read_file", llvm::Type::getVoidTy(Ctx),
                             {PtrTy, PtrTy, I64Ty}),
                {fp, addr, capV});
        else
            B.CreateCall(
                Strings.getStrFn("plang_sstr_read", llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty}),
                {addr, capV});
        return;
    }

    // string(N) is a { i64, [N x i8] } struct, so it needs the string reader;
    // the scalar readers would overwrite the length field.
    if (ExprIsVarStr(arg)) {
        // How much may be stored, so folding the probe here truncated the
        // input to one character for a discriminant-sized string.
        if (!cap) cap = Schema.exprStrCapV(arg);
        if (fp)
            B.CreateCall(
                RtFns.getExternFnN("plang_str_read_file", llvm::Type::getVoidTy(Ctx),
                             {PtrTy, PtrTy, I64Ty}),
                {fp, addr, cap});
        else
            B.CreateCall(
                Strings.getStrFn("plang_str_read", llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty}),
                {addr, cap});
        return;
    }

    // ISO §6.10.1(e): a fixed-string-type (packed array[1..n] of char) reads
    // like the varying-string sibling just above, minus the length field --
    // Sema's read-parameter check accepted it, and codegen had no case for
    // it at all, so it fell through to the scalar path below, which reads a
    // [n x i8] LLVM array as if it were an i64 (readFnSuffix has no answer
    // for an array type) and silently did nothing useful.
    if (ExprIsCharStr(arg)) {
        auto* n = i64c(ExprCharStrLen(arg));
        if (fp)
            B.CreateCall(
                RtFns.getExternFnN(fileFn("plang_str_read_fixed_file"), llvm::Type::getVoidTy(Ctx),
                             {PtrTy, PtrTy, I64Ty}),
                {fp, addr, n});
        else
            B.CreateCall(
                Strings.getStrFn("plang_str_read_fixed", llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty}),
                {addr, n});
        return;
    }

    // What is being read into, from Sema, not from a name lookup.  Only an
    // identifier had a type here, so `read(a[i])`, `read(r.f)` and `read(p^)`
    // fell back to i64 -- which picked the *integer* reader for a char
    // variable, and then stored eight bytes into one.  Reading into a
    // component of an array of char clobbered the whole array and four bytes
    // past the end of it.  ISO 7185, no dialect flag needed.
    llvm::Type* ty = I64Ty;
    if (const auto& rt = arg.ResolvedType; rt && !rt->isError()
            && Types.canLowerSemaType(*rt))
        ty = Types.llvmTypeOfSemaType(*rt);
    else if (auto* id = llvm::dyn_cast<IdentExpr>(&arg))
        if (auto* ve = SymTab.findVar(id->Name)) ty = ve->type;

    // WHICH reader, from what the type IS rather than from how wide it happens
    // to be stored.  A subrange of char is held in a full ordinal -- so
    // `read(c)` on a `'a'..'z'` variable called the INTEGER reader, tried to
    // parse a number out of "xy", found none and left the variable untouched.
    const plang::Type* base = arg.ResolvedType.get();
    while (base && base->Kind == TypeKind::Subrange && base->SubBase)
        base = base->SubBase.get();

    // readTy is the WIDTH THE READER ITSELF WRITES THROUGH dest AT, which
    // must track suffix, not ty: readFnSuffix's fallback is always "_i64"
    // for anything that isn't double or i8 (an ordinal reader has exactly
    // one width, plang_read_i64, regardless of the destination's own
    // width), so a Turbo 16-bit Integer's ty=i16 must still get readTy=I64Ty
    // here -- leaving readTy == ty (as this used to, unconditionally) makes
    // `convert` false below and hands plang_read_i64 a pointer to a 2-byte
    // stack slot, an 8-byte write through it that clobbers 6 bytes past the
    // end.  ISO 7185/EP never reach this arm at a narrower width (both
    // dialects stamp Integer/Subrange/Enum at Width=64), so this is
    // unreachable before Turbo, the same way the write-side widening above
    // was.
    std::string suffix  = readFnSuffix(ty, base);
    llvm::Type* readTy  = I64Ty;
    if (base && base->Kind == TypeKind::Real)      { suffix = "_f64";  readTy = DblTy; }
    else if (base && base->Kind == TypeKind::Char) { suffix = "_char"; readTy = I8Ty;  }
    // Turbo's QWord (Integer, Width 64, unsigned) is the one ordinal whose
    // full range does not fit a signed 64-bit parse: reading
    // "18446744073709551615" through plang_read_i64's strtoll ERANGEs.  Only
    // Width 64 needs its own reader -- Word/Cardinal/LongWord's unsigned
    // ranges all fit comfortably inside int64_t, so plang_read_i64's ordinary
    // signed parse (followed by CoerceToType's truncation into the narrower
    // destination below) already reads them correctly.  readTy stays I64Ty:
    // plang_read_u64 stores through a uint64_t*, the same 8 bytes at the same
    // address a plang_read_i64 destination would use.
    else if (base && base->Kind == TypeKind::Integer && base->Width == 64 && !base->IsSigned)
        suffix = "_u64";

    // Turbo reverses the numeric scanners entirely (whole-token, the entire
    // token must parse, $/0x/&/% radix prefixes -- plang_io.cpp's
    // plang_read_i64_turbo/plang_read_f64_turbo and plang_file.cpp's file
    // twins) rather than differing by one resolved value the ISO/EP entry
    // point can absorb, so this picks a second, purpose-built entry point --
    // the RangeCheckGuards precedent (plang_tp_runerror vs plang_err_div_zero)
    // for when the two dialects' behavior is not just a parameter apart.
    // Char reads are deliberately excluded: Turbo's raw-byte char read vs
    // ISO/EP's line-marker-as-space substitution is a separate, already-
    // settled design point (plang_file.cpp's plang_read_file_char, citing
    // ISO §6.4.3.5) this task does not touch.  QWord ("_u64") is included --
    // it only ever exists under -std=turbo (Sema.cpp registers the whole
    // sized-integer ladder behind Opts.turbo()), so plang_read_u64_turbo is
    // the only "_u64" entry point CodeGen ever actually needs, but the plain
    // plang_read_u64 exists too, on the same "additive, never called back
    // into" footing plang_read_i64/_f64 already have relative to their own
    // _turbo twins.
    if (Opts.turbo() && (suffix == "_i64" || suffix == "_f64" || suffix == "_u64"))
        suffix += "_turbo";
    // -std=turbo, file-directed char reads only: a SECOND, independent reason
    // to append "_turbo", nothing to do with the numeric grammar difference
    // above -- this item's own fileReady/InOutRes choke point, which every
    // Turbo-reachable file-I/O runtime entry point needs regardless of
    // whether its own parsing grammar differs from ISO/EP's.  The comment
    // just above still holds for WHY char reads get no grammar-driven
    // "_turbo" suffix; this is a different axis entirely, and the `fp &&`
    // guard keeps it scoped to an actual PascalFile -- a bare `read(c)` from
    // stdin has no such file to be not-open, so plang_read_char (no file,
    // plang_io.cpp) is correctly left alone.
    if (fp && Opts.turbo() && suffix == "_char")
        suffix += "_turbo";

    // The runtime stores through the pointer at the reader's own width, so a
    // variable of a different width is read into a temporary and converted --
    // the same shape read(a[i]) already needed.
    const bool convert = readTy != ty;
    llvm::Value* dest  = convert ? CreateEntryAlloca(readTy, "rd.tmp") : addr;

    if (fp) {
        B.CreateCall(
            RtFns.getExternFnN("plang_read_file" + suffix,
                         llvm::Type::getVoidTy(Ctx), {PtrTy, PtrTy}),
            {fp, dest});
    } else {
        B.CreateCall(RtFns.getRuntimeFn("plang_read" + suffix, PtrTy), {dest});
    }
    if (convert)
        B.CreateStore(
            CoerceToType(B.CreateLoad(readTy, dest, "rd.val"), ty), addr);

    // ISO §6.9.1 makes read(f, v) into `v := f^`, so §6.4.6's requirement that
    // the value lie within a subrange's interval applies here exactly as it
    // does to an assignment written out.  Nothing checked it, so `read(d)` for
    // `d: 1..9` accepted 99 and left the variable holding a value its type
    // cannot represent -- which every later use then trusts: an array indexed
    // by it, a case selector, a `for` bound.
    // Lo == Hi (e.g. `5..5`) is a legal singleton subrange, not a sentinel
    // for "no real bounds" -- Kind == Subrange already guarantees these are
    // real, so excluding Lo == Hi here would just let read() write any value
    // into a singleton subrange uncaught.
    if (const auto& rt = arg.ResolvedType;
            rt && rt->Kind == TypeKind::Subrange) {
        auto* got = B.CreateLoad(ty, addr, "rd.chk");
        // arg's own signedness (== rt's, arg.ResolvedType): a read() target
        // whose declared subrange picked a signed narrow or unsigned wide
        // Turbo storage width (TP7 ch.19's own rule -- see
        // TypeContext::getSubrange) otherwise widened wrong here, ahead of
        // the very range check meant to catch an out-of-bounds value (issue
        // #177's sibling audit).
        RangeGuards.emitRangeCheck(ToI64(got, exprIsSigned(arg)), rt->SubLo, rt->SubHi,
                       /*isIndex=*/false, arg.Loc);
    }
}

/// Advances past the rest of the current line on fp (or stdin).
void BuiltinIO::emitSkipLine(llvm::Value* fp) {
    if (fp)
        B.CreateCall(RtFns.getExternFnN(fileFn("plang_readln_file"),
            llvm::Type::getVoidTy(Ctx), {PtrTy}), {fp});
    else
        B.CreateCall(RtFns.getRuntimeFn("plang_readln", nullptr), {});
}

void BuiltinIO::emitBuiltinRead(const std::vector<std::unique_ptr<ExprNode>>& args) {
    size_t start = 0;
    llvm::Value* fp = nullptr;
    bool binaryTyped = false;
    if (!args.empty() && FileVars.isFileVar(*args[0])) {
        fp          = FileVars.fileVarPtr(*args[0]);
        start       = 1;
        binaryTyped = FileVars.isTypedBinaryFileVar(*args[0]);
    } else {
        // -std=turbo only: see emitBuiltinWrite's identical branch -- a
        // bare read/readln with no explicit file argument now means the
        // predefined Input variable, which Assign/Reset may have
        // redirected away from stdin.
        fp = turboStdFilePtr(/*isInput=*/true);
    }

    for (size_t i = start; i < args.size(); ++i) {
        if (binaryTyped && fp) {
            auto* addr = EmitLValue(*args[i]);
            if (!addr) continue;
            // ISO §6.9.1: read(f,v) is v := f^, so a component's worth is read
            // however wide the variable is, and only then converted.
            llvm::Type* compTy = FileVars.getFileElemType(*args[0]);
            // Likewise from Sema.  Here the identifier-only lookup decided
            // whether the component was read into a temporary at all, while the
            // byte count came from the file's component type regardless: a
            // `file of 'a'..'z'` read into an element of an `array of char`
            // wrote a whole eight-byte component into one byte.
            llvm::Type* dstTy  = nullptr;
            if (const auto& rt = args[i]->ResolvedType; rt && !rt->isError()
                    && Types.canLowerSemaType(*rt))
                dstTy = Types.llvmTypeOfSemaType(*rt);
            else if (auto* id = llvm::dyn_cast<IdentExpr>(args[i].get()))
                if (auto* ve = SymTab.findVar(id->Name)) dstTy = ve->type;
            if (!compTy) compTy = dstTy ? dstTy : I64Ty;
            // -std=turbo: no implicit widening on a typed file's Read, the
            // same "exact type identity" rule as the write side just above
            // (see emitWriteArgs's own comment) -- Sema already refuses a
            // mismatched Turbo program before this is reached
            // (err_turbo_typed_file_exact_type), so `convert` is unreachable
            // under Turbo either way; gated explicitly rather than relying
            // on that invariant, for the same reason as the write side.
            const bool convert = !Opts.turbo() && dstTy && dstTy != compTy
                                 && dstTy->isSingleValueType()
                                 && compTy->isSingleValueType();
            auto* dest = convert ? CreateEntryAlloca(compTy, "bin.rd.tmp") : addr;
            int64_t esz = (int64_t)Mod.getDataLayout().getTypeAllocSize(compTy);
            B.CreateCall(
                RtFns.getExternFnN(fileFn("plang_read_binary"), llvm::Type::getVoidTy(Ctx),
                             {PtrTy, PtrTy, I64Ty}),
                {fp, dest, llvm::ConstantInt::get(I64Ty, esz)});
            if (convert)
                B.CreateStore(
                    CoerceToType(B.CreateLoad(compTy, dest, "bin.rd"), dstTy),
                    addr);
        } else {
            emitReadArg(*args[i], fp);
        }
    }
}

// ISO §6.9.2: readln takes every variable from the current line and only then
// advances to the next one, so the values must use the readers that leave the
// terminator alone and the line is consumed exactly once at the end.
void BuiltinIO::emitBuiltinReadln(const std::vector<std::unique_ptr<ExprNode>>& args) {
    size_t start = 0;
    llvm::Value* fp = nullptr;
    if (!args.empty() && FileVars.isFileVar(*args[0])) {
        fp = FileVars.fileVarPtr(*args[0]);
        start = 1;
    } else {
        // -std=turbo only: see emitBuiltinWrite's identical branch.
        fp = turboStdFilePtr(/*isInput=*/true);
    }

    for (size_t i = start; i < args.size(); ++i) emitReadArg(*args[i], fp);
    emitSkipLine(fp);
}

// ====================================================================
// EP §6.7.5.5 string transfer procedures
// ====================================================================
//
// The standard defines both in terms of an auxiliary text file, so rather than
// duplicating the formatting and parsing logic these bracket the ordinary
// write/read lowering with a runtime redirect onto a memory buffer.

/// writestr(s, p1, ..., pn) — format the write-parameters into the string s.
///
/// DELIBERATELY EP-only throughout, ExprIsVarStr calls included: writestr
/// and readstr are both registered `EP` in Builtins.def, and ShortString
/// only ever exists under -std=turbo (LangOptions::Standard is one mutually
/// exclusive enum, so turbo() and extendedPascal() can never both be true
/// for one compilation) -- so a ShortString argument can never reach either
/// of these two functions in a Sema-accepted program, and neither needs a
/// ShortString branch of its own.
void BuiltinIO::emitBuiltinWriteStr(
        const std::vector<std::unique_ptr<ExprNode>>& args) {
    const ExprNode& dest = *args[0];
    // EP §6.7.5.5: the destination "shall possess a fixed-string-type or a
    // variable-string-type" -- both, not only the varying one this asked
    // for.  A packed array[1..n] of char destination ICE'd outright.
    const bool fixed = !ExprIsVarStr(dest) && ExprIsCharStr(dest);
    if (!ExprIsVarStr(dest) && !fixed)
        codegenICE("writestr destination is not a string variable");
    auto* sPtr = EmitLValue(dest);
    if (!sPtr) codegenICE("writestr destination is not assignable");

    auto* voidTy = llvm::Type::getVoidTy(Ctx);
    B.CreateCall(RtFns.getExternFnN("plang_writestr_begin", voidTy, {}), {});

    emitWriteArgs(args, /*start=*/1, /*newline=*/false, /*fp=*/nullptr,
                  /*binaryTyped=*/false);

    if (fixed)
        B.CreateCall(
            RtFns.getExternFnN("plang_writestr_end_fixed", voidTy, {PtrTy, I64Ty}),
            {sPtr, i64c(ExprCharStrLen(dest))});
    else
        B.CreateCall(
            RtFns.getExternFnN("plang_writestr_end", voidTy, {PtrTy, I64Ty}),
            {sPtr, Schema.exprStrCapV(dest)});
}

/// readstr(e, v1, ..., vn) — parse the variables out of the string e.
void BuiltinIO::emitBuiltinReadStr(
        const std::vector<std::unique_ptr<ExprNode>>& args) {
    const ExprNode& src = *args[0];
    auto* voidTy = llvm::Type::getVoidTy(Ctx);

    // The source may be a string(N) struct, ISO §6.4.3.2's fixed-string-type
    // (a packed array[1..n] of char, EP §6.7.5.5's OTHER legal string-
    // expression shape), or a plain character pointer.
    llvm::Value* data = nullptr;
    llvm::Value* len  = nullptr;
    if (ExprIsVarStr(src)) {
        auto* sPtr = EmitLValue(src);
        if (!sPtr) sPtr = EmitExpr(src);
        if (!sPtr) codegenICE("readstr source is not a readable string");
        // string(N) is { i64 length, [N x i8] data }.
        len  = B.CreateLoad(I64Ty, sPtr, "readstr.len");
        data = B.CreateConstGEP1_64(I8Ty, sPtr, 8, "readstr.data");
    } else if (ExprIsCharStr(src)) {
        // No length field to read, and no terminator to look for -- every
        // component is part of the value, so the length is the array's own,
        // fixed by its declaration.  Without this case, emitExpr(src) handed
        // the fallback below the ARRAY VALUE (not a pointer, since a fixed-
        // string is a small aggregate LLVM is free to keep in a register),
        // which failed `isPointerTy()` and ICE'd; a correctly-shaped pointer
        // would still have been wrong, since strlen has no terminator to find
        // in an unterminated fixed-width buffer.
        data = EmitLValue(src);
        if (!data) codegenICE("readstr source is not a readable string");
        len  = i64c(ExprCharStrLen(src));
    } else {
        data = EmitExpr(src);
        if (!data || !data->getType()->isPointerTy())
            codegenICE("readstr source is not a string expression");
        len = B.CreateCall(
            RtFns.getExternFnN("strlen", I64Ty, {PtrTy}), {data}, "readstr.len");
    }
    B.CreateCall(RtFns.getExternFnN("plang_readstr_begin", voidTy, {PtrTy, I64Ty}),
                       {data, len});

    for (size_t i = 1; i < args.size(); ++i) emitReadArg(*args[i], /*fp=*/nullptr);

    B.CreateCall(RtFns.getExternFnN("plang_readstr_end", voidTy, {}), {});
}

/// TP-only: Str(x [: width [: decimals]], var s).  Same writestr-capture
/// bracketing emitBuiltinWriteStr uses above, but reversed (x is FIRST, not
/// the destination) and ShortString-shaped at the far end: plang_writestr_end
/// writes an EP eight-byte length header, which is why this calls a THIRD
/// sibling, plang_writestr_end_sstr (plang_io.cpp, beside plang_writestr_end/
/// _end_fixed), rather than either existing one -- ShortString's one-byte
/// header is a different geometry from both.
void BuiltinIO::emitBuiltinStr(
        const std::vector<std::unique_ptr<ExprNode>>& args) {
    const ExprNode& dest = *args[1];
    if (dest.ResolvedType->Kind != TypeKind::ShortString)
        codegenICE("Str destination is not a ShortString variable");
    auto* sPtr = EmitLValue(dest);
    if (!sPtr) codegenICE("Str destination is not assignable");

    auto* voidTy = llvm::Type::getVoidTy(Ctx);
    B.CreateCall(RtFns.getExternFnN("plang_writestr_begin", voidTy, {}), {});

    // Format ONLY args[0] (the value) -- args[1] is the destination, never a
    // value to write, which is why this passes end=1 rather than emitting
    // the whole argument list the way write/writestr's own calls do.
    emitWriteArgs(args, /*start=*/0, /*newline=*/false, /*fp=*/nullptr,
                  /*binaryTyped=*/false, /*end=*/1);

    B.CreateCall(
        RtFns.getExternFnN("plang_writestr_end_sstr", voidTy, {PtrTy, I64Ty}),
        {sPtr, i64c(dest.ResolvedType->StrCapacity)});
}

bool BuiltinIO::writesAsBoolean(const llvm::Type* ty, const plang::Type* semaTy) {
    if (ty->isIntegerTy(1)) return true;
    // i16/i32 are only ever WordBool/LongBool (Type::IsLooseBool) reaching
    // here as semaTy->Kind == Boolean; nothing else plang stores at those
    // widths is a Boolean-shaped value, so the Kind check alone (matching
    // the pre-existing i8 case just below) is enough to tell them apart from
    // an equally-wide Word/LongInt.
    return semaTy && semaTy->Kind == TypeKind::Boolean
        && (ty->isIntegerTy(8) || ty->isIntegerTy(16) || ty->isIntegerTy(32));
}

llvm::Value* BuiltinIO::toBoolByte(llvm::Value* val) const {
    llvm::Type* ty = val->getType();
    if (ty->isIntegerTy(8)) return val;
    llvm::Value* nz = ty->isIntegerTy(1)
        ? val
        : B.CreateICmpNE(val, llvm::ConstantInt::get(ty, 0), "bool.nz");
    return B.CreateZExt(nz, I8Ty, "bool.ext");
}

bool BuiltinIO::writesAsChar(const llvm::Type* ty, const plang::Type* semaTy) {
    if (!ty->isIntegerTy()) return false;
    // Width alone used to decide this: any i8 was taken for a char.  That
    // was an unreachable-in-practice shortcut before Turbo's sized-integer
    // ladder (ShortInt, Byte, ...) existed -- ISO 7185 and Extended Pascal
    // stamp Width=64 on every Integer, and Turbo's own Integer is 16-bit, so
    // the only i8 ordinal that ever reached here really was a Char.
    // ShortInt and Byte are the first Integer-kind types that are also i8,
    // and the shortcut wrote their values through plang_write(ln)_char,
    // printing the raw byte as a (usually unprintable) character instead of
    // a number.  semaTy's own Kind -- looking through a subrange to its
    // host, the same as the wide subrange-of-char case below always has --
    // is what actually answers "is this a char", and is used whenever it is
    // available.
    if (!semaTy) return ty->isIntegerTy(8); // no Sema type: keep the old guess
    const plang::Type* t = semaTy;
    while (t->Kind == TypeKind::Subrange && t->SubBase) t = t->SubBase.get();
    return t->Kind == TypeKind::Char;
}

bool BuiltinIO::writesAsUnsigned64(const plang::Type* semaTy) {
    // Every ordinal narrower than 64 bits is sign/zero-extended to i64 before
    // reaching an isIntegerTy(64) dispatch site (the SExt/ZExt widening in
    // both emitWriteValue and emitWriteValueFormatted, just above their own
    // callers of this), so an unsigned value under 2^63 already prints
    // identically through the signed writer -- only a genuinely 64-bit-wide
    // unsigned ordinal (Turbo's QWord) can hold a value with the i64 sign bit
    // set, which %PRId64 would print as negative.  Peeling Subrange matches
    // CodeGenImpl::ordinalIsUnsigned's own precedent, for a subrange whose
    // bounds happen to be declared against QWord.
    const plang::Type* t = semaTy;
    while (t && t->Kind == TypeKind::Subrange && t->SubBase) t = t->SubBase.get();
    return t && t->Kind == TypeKind::Integer && t->Width == 64 && !t->IsSigned;
}
