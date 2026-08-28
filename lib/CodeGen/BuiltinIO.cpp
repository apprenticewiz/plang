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

void BuiltinIO::emitBuiltinWrite(const std::vector<std::unique_ptr<ExprNode>>& args, bool newline) {
    // Detect file variable as first argument: write(f, ...) vs write(...)
    size_t start = 0;
    llvm::Value* fp = nullptr;
    bool binaryTyped = false;
    if (!args.empty() && FileVars.isFileVar(*args[0])) {
        fp          = FileVars.fileVarPtr(*args[0]);
        start       = 1;
        binaryTyped = FileVars.isTypedBinaryFileVar(*args[0]);
    }

    if (start >= args.size()) {
        // writeln with no value arguments (just the newline/file)
        if (newline) {
            if (fp)
                B.CreateCall(RtFns.getExternFnN("plang_writeln_file",
                    llvm::Type::getVoidTy(Ctx), {PtrTy}), {fp});
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
        bool newline, llvm::Value* fp, bool binaryTyped) {
    for (size_t i = start; i < args.size(); ++i) {
        bool addNl = newline && (i == args.size() - 1);
        // Check for WriteParam (field-width formatting)
        const ExprNode* argExpr = args[i].get();
        llvm::Value*    width   = nullptr;
        llvm::Value*    decimals = nullptr;
        if (auto* wp = llvm::dyn_cast<WriteParam>(argExpr)) {
            argExpr  = wp->Value.get();
            width    = wp->Width    ? ToI64(EmitExpr(*wp->Width))    : nullptr;
            decimals = wp->Decimals ? ToI64(EmitExpr(*wp->Decimals)) : nullptr;
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
                    RtFns.getExternFnN("plang_write_binary", llvm::Type::getVoidTy(Ctx),
                                 {PtrTy, PtrTy, I64Ty}),
                    {fp, addr, llvm::ConstantInt::get(I64Ty, esz)});
                continue;
            }
            auto* val = EmitExpr(*argExpr);
            if (!val) continue;
            // ISO §6.9.1: write(f,e) is f^ := e, so what lands in the file is a
            // component.  An integer written to a file of real has to widen
            // first, or the bytes would be read back as a real.
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
                RtFns.getExternFnN("plang_write_binary", llvm::Type::getVoidTy(Ctx),
                             {PtrTy, PtrTy, I64Ty}),
                {fp, tmp, llvm::ConstantInt::get(I64Ty, esz)});
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
                        RtFns.getExternFnN("plang_str_write_file_w",
                                     llvm::Type::getVoidTy(Ctx),
                                     {PtrTy, PtrTy, I64Ty, I64Ty}),
                        {fp, sptr, capV, width});
                else
                    B.CreateCall(
                        RtFns.getExternFnN("plang_str_write_file",
                                     llvm::Type::getVoidTy(Ctx),
                                     {PtrTy, PtrTy, I64Ty}),
                        {fp, sptr, capV});
                if (addNl)
                    B.CreateCall(
                        RtFns.getExternFnN("plang_writeln_file",
                                     llvm::Type::getVoidTy(Ctx), {PtrTy}), {fp});
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
    // EP §6.9.3.6: a complex is written as its two components, so it needs a
    // writer of its own rather than one of the scalar ones.
    if (ty == Complex.complexTy()) {
        auto* re = B.CreateExtractValue(val, 0, "cplx.re");
        auto* im = B.CreateExtractValue(val, 1, "cplx.im");
        auto* voidT = llvm::Type::getVoidTy(Ctx);
        if (fp) {
            B.CreateCall(RtFns.getExternFnN("plang_write_file_cplx", voidT,
                                            {PtrTy, DblTy, DblTy}), {fp, re, im});
            if (newline)
                B.CreateCall(
                    RtFns.getExternFnN("plang_writeln_file", voidT, {PtrTy}), {fp});
        } else {
            B.CreateCall(
                RtFns.getExternFnN(newline ? "plang_writeln_cplx" : "plang_write_cplx",
                             voidT, {DblTy, DblTy}), {re, im});
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
    auto callFile = [&](const std::string& fn, llvm::Type* pt, llvm::Value* v) {
        B.CreateCall(RtFns.getExternFnN(fn, voidTy, {PtrTy, pt}), {fp, v});
    };

    if (ty->isIntegerTy(64)) {
        fp ? callFile("plang_write_file_i64", I64Ty, val)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_i64", I64Ty, val);
    } else if (ty->isDoubleTy()) {
        fp ? callFile("plang_write_file_f64", DblTy, val)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_f64", DblTy, val);
    } else if (isBool) {
        auto* ext = ty->isIntegerTy(1)
            ? B.CreateZExt(val, I8Ty, "bool.ext") : val;
        fp ? callFile("plang_write_file_bool", I8Ty, ext)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_bool", I8Ty, ext);
    } else if (ty->isIntegerTy(8)) {
        fp ? callFile("plang_write_file_char", I8Ty, val)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_char", I8Ty, val);
    } else {
        fp ? callFile("plang_write_file_str", PtrTy, val)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_str", PtrTy, val);
    }
    // File writeln: append newline after value
    if (fp && newline)
        B.CreateCall(RtFns.getExternFnN("plang_writeln_file", voidTy, {PtrTy}), {fp});
}

// Emit a write call with field-width (and optional decimal-places) formatting.
void BuiltinIO::emitWriteValueFormatted(llvm::Value* val, llvm::Value* w, llvm::Value* d,
                                             bool newline, llvm::Value* fp,
                                             const plang::Type* semaTy) {
    if (!w) { emitWriteValue(val, newline, fp, semaTy); return; } // no width
    auto* voidTy = llvm::Type::getVoidTy(Ctx);
    std::string nl = newline ? "ln" : "";
    llvm::Type* ty = val->getType();
    if (ty == Complex.complexTy()) {
        auto* re = B.CreateExtractValue(val, 0, "cplx.re");
        auto* im = B.CreateExtractValue(val, 1, "cplx.im");
        auto* dv = d ? d : llvm::ConstantInt::get(I64Ty, -1, true);
        if (fp) {
            B.CreateCall(
                RtFns.getExternFnN("plang_write_file_cplx_w", voidTy,
                             {PtrTy, DblTy, DblTy, I64Ty, I64Ty}),
                {fp, re, im, w, dv});
            if (newline)
                B.CreateCall(
                    RtFns.getExternFnN("plang_writeln_file", voidTy, {PtrTy}), {fp});
        } else {
            B.CreateCall(
                RtFns.getExternFnN("plang_write" + nl + "_cplx_w", voidTy,
                             {DblTy, DblTy, I64Ty, I64Ty}),
                {re, im, w, dv});
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
        auto callFile = [&](const std::string& fn,
                            std::initializer_list<llvm::Type*> argTys,
                            std::initializer_list<llvm::Value*> args) {
            std::vector<llvm::Type*>  tys{PtrTy};
            std::vector<llvm::Value*> vs{fp};
            tys.insert(tys.end(), argTys);
            vs.insert(vs.end(), args);
            B.CreateCall(RtFns.getExternFnN(fn, voidTy, tys), vs);
        };
        if (ty->isIntegerTy(64)) {
            callFile("plang_write_file_i64_w", {I64Ty, I64Ty}, {val, w});
        } else if (ty->isDoubleTy()) {
            if (d) callFile("plang_write_file_f64_f", {DblTy, I64Ty, I64Ty}, {val, w, d});
            else   callFile("plang_write_file_f64_e", {DblTy, I64Ty}, {val, w});
        } else if (isBool) {
            auto* ext = ty->isIntegerTy(1)
                ? B.CreateZExt(val, I8Ty, "bool.ext") : val;
            callFile("plang_write_file_bool_w", {I8Ty, I64Ty}, {ext, w});
        } else if (ty->isIntegerTy(8)) {
            callFile("plang_write_file_char_w", {I8Ty, I64Ty}, {val, w});
        } else {
            callFile("plang_write_file_str_w", {PtrTy, I64Ty}, {val, w});
        }
        if (newline)
            B.CreateCall(RtFns.getExternFnN("plang_writeln_file", voidTy, {PtrTy}), {fp});
        return;
    }

    if (ty->isIntegerTy(64)) {
        B.CreateCall(RtFns.getExternFnN("plang_write" + nl + "_i64_w", voidTy, {I64Ty, I64Ty}),
                           {val, w});
    } else if (ty->isDoubleTy()) {
        if (d)
            B.CreateCall(RtFns.getExternFnN("plang_write" + nl + "_f64_f", voidTy, {DblTy, I64Ty, I64Ty}),
                               {val, w, d});
        else
            B.CreateCall(RtFns.getExternFnN("plang_write" + nl + "_f64_e", voidTy, {DblTy, I64Ty}),
                               {val, w});
    } else if (isBool) {
        auto* ext = ty->isIntegerTy(1)
            ? B.CreateZExt(val, I8Ty, "bool.ext") : val;
        B.CreateCall(RtFns.getExternFnN("plang_write" + nl + "_bool_w", voidTy, {I8Ty, I64Ty}),
                           {ext, w});
    } else if (ty->isIntegerTy(8)) {
        B.CreateCall(RtFns.getExternFnN("plang_write" + nl + "_char_w", voidTy, {I8Ty, I64Ty}),
                           {val, w});
    } else {
        B.CreateCall(RtFns.getExternFnN("plang_write" + nl + "_str_w", voidTy, {PtrTy, I64Ty}),
                           {val, w});
    }
}

// Helper to determine read type for an argument.
std::string BuiltinIO::readFnSuffix(llvm::Type* ty) {
    if (ty->isDoubleTy())        return "_f64";
    if (ty->isIntegerTy(8))      return "_char";
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
                RtFns.getExternFnN("plang_str_read_fixed_file", llvm::Type::getVoidTy(Ctx),
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
    // to be stored.  readFnSuffix answers from the LLVM type alone, and a
    // subrange of char is held in a full ordinal -- so `read(c)` on a
    // `'a'..'z'` variable called the INTEGER reader, tried to parse a number
    // out of "xy", found none and left the variable untouched.
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
    std::string suffix  = readFnSuffix(ty);
    llvm::Type* readTy  = I64Ty;
    if (base && base->Kind == TypeKind::Real)      { suffix = "_f64";  readTy = DblTy; }
    else if (base && base->Kind == TypeKind::Char) { suffix = "_char"; readTy = I8Ty;  }

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
        RangeGuards.emitRangeCheck(ToI64(got), rt->SubLo, rt->SubHi,
                       /*isIndex=*/false, arg.Loc);
    }
}

/// Advances past the rest of the current line on fp (or stdin).
void BuiltinIO::emitSkipLine(llvm::Value* fp) {
    if (fp)
        B.CreateCall(RtFns.getExternFnN("plang_readln_file",
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
            const bool convert = dstTy && dstTy != compTy
                                 && dstTy->isSingleValueType()
                                 && compTy->isSingleValueType();
            auto* dest = convert ? CreateEntryAlloca(compTy, "bin.rd.tmp") : addr;
            int64_t esz = (int64_t)Mod.getDataLayout().getTypeAllocSize(compTy);
            B.CreateCall(
                RtFns.getExternFnN("plang_read_binary", llvm::Type::getVoidTy(Ctx),
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
    if (!args.empty() && FileVars.isFileVar(*args[0])) { fp = FileVars.fileVarPtr(*args[0]); start = 1; }

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

bool BuiltinIO::writesAsBoolean(const llvm::Type* ty, const plang::Type* semaTy) {
    if (ty->isIntegerTy(1)) return true;
    return ty->isIntegerTy(8) && semaTy && semaTy->Kind == TypeKind::Boolean;
}

bool BuiltinIO::writesAsChar(const llvm::Type* ty, const plang::Type* semaTy) {
    if (ty->isIntegerTy(8)) return true;
    if (!ty->isIntegerTy() || !semaTy) return false;
    const plang::Type* t = semaTy;
    while (t->Kind == TypeKind::Subrange && t->SubBase) t = t->SubBase.get();
    return t->Kind == TypeKind::Char;
}
