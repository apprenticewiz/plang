#include "CodegenImpl.h"
using namespace plang;

// ====================================================================
// Built-in write / writeln / read — dispatch to plang_runtime functions
// ====================================================================

void Codegen::Impl::emitBuiltinWrite(const std::vector<std::unique_ptr<ExprNode>>& args, bool newline) {
    // Detect file variable as first argument: write(f, ...) vs write(...)
    size_t start = 0;
    llvm::Value* fp = nullptr;
    bool binaryTyped = false;
    if (!args.empty() && isFileVar(*args[0])) {
        fp          = fileVarPtr(*args[0]);
        start       = 1;
        binaryTyped = isTypedBinaryFileVar(*args[0]);
    }

    if (start >= args.size()) {
        // writeln with no value arguments (just the newline/file)
        if (newline) {
            if (fp)
                builder.CreateCall(getExternFnN("plang_writeln_file",
                    llvm::Type::getVoidTy(ctx), {ptrTy}), {fp});
            else
                builder.CreateCall(getRuntimeFn("plang_writeln", nullptr), {});
        }
        return;
    }

    emitWriteArgs(args, start, newline, fp, binaryTyped);
}

/// Lowers args[start..] as write-parameters.  Shared by write/writeln and by
/// writestr, which supplies its own destination and no file.
void Codegen::Impl::emitWriteArgs(
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
            width    = wp->Width    ? toI64(emitExpr(*wp->Width))    : nullptr;
            decimals = wp->Decimals ? toI64(emitExpr(*wp->Decimals)) : nullptr;
        }

        // Binary typed file: write raw bytes.
        if (binaryTyped && fp) {
            auto* val = emitExpr(*argExpr);
            if (!val) continue;
            // ISO §6.9.1: write(f,e) is f^ := e, so what lands in the file is a
            // component.  An integer written to a file of real has to widen
            // first, or the bytes would be read back as a real.
            if (auto* compTy = getFileElemType(*args[0]))
                if (compTy->isSingleValueType() && val->getType()->isSingleValueType())
                    val = coerceToType(val, compTy);
            // Store the value to a temporary alloca so we can pass its address.
            auto* tmp = createEntryAlloca(val->getType(), "bin.wr.tmp");
            builder.CreateStore(val, tmp);
            int64_t esz = (int64_t)mod->getDataLayout().getTypeAllocSize(val->getType());
            builder.CreateCall(
                getExternFnN("plang_write_binary", llvm::Type::getVoidTy(ctx),
                             {ptrTy, ptrTy, i64Ty}),
                {fp, tmp, llvm::ConstantInt::get(i64Ty, esz)});
            continue;
        }
        // VarString arguments → string runtime; everything else → scalar write.
        // ISO §6.4.3.2: a packed array[1..n] of char is written as the string
        // it is, which the string writers already do once it is shaped like
        // one.  Writing it as a scalar reached plang_write_str, which reads
        // until a terminator the array does not have.
        if (exprIsVarStr(*argExpr) || exprIsCharStr(*argExpr)) {
            const bool chars = exprIsCharStr(*argExpr);
            int64_t cap  = chars ? exprCharStrLen(*argExpr) : exprStrCap(*argExpr);
            auto* sptr = chars ? emitCharStrAsStr(*argExpr) : emitStrAddr(*argExpr);
            if (!sptr) continue;
            auto* capV = llvm::ConstantInt::get(i64Ty, cap);
            if (fp) {
                // string(N) is not null-terminated, so it needs its own writer
                // rather than the char* one the generic path would pick.  A
                // field width applies here as much as on the standard output;
                // dropping it wrote the whole string into a field too small.
                if (width)
                    builder.CreateCall(
                        getExternFnN("plang_str_write_file_w",
                                     llvm::Type::getVoidTy(ctx),
                                     {ptrTy, ptrTy, i64Ty, i64Ty}),
                        {fp, sptr, capV, width});
                else
                    builder.CreateCall(
                        getExternFnN("plang_str_write_file",
                                     llvm::Type::getVoidTy(ctx),
                                     {ptrTy, ptrTy, i64Ty}),
                        {fp, sptr, capV});
                if (addNl)
                    builder.CreateCall(
                        getExternFnN("plang_writeln_file",
                                     llvm::Type::getVoidTy(ctx), {ptrTy}), {fp});
            } else if (width) {
                auto* fn = getStrFn(addNl ? "plang_str_writeln_w" : "plang_str_write_w",
                    llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, i64Ty});
                builder.CreateCall(fn, {sptr, capV, width});
            } else {
                auto* fn = getStrFn(addNl ? "plang_str_writeln" : "plang_str_write",
                    llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty});
                builder.CreateCall(fn, {sptr, capV});
            }
        } else {
            auto* val = emitExpr(*argExpr);
            if (!val) continue;
            const plang::Type* semaTy = argExpr->ResolvedType.get();
            if (width) emitWriteValueFormatted(val, width, decimals, addNl, fp, semaTy);
            else       emitWriteValue(val, addNl, fp, semaTy);
        }
    }
}

// Emit a write call, dispatching on LLVM type.
// fp: file pointer (null = stdout).
void Codegen::Impl::emitWriteValue(llvm::Value* val, bool newline, llvm::Value* fp,
                                   const plang::Type* semaTy) {
    llvm::Type* ty  = val->getType();
    // EP §6.9.3.6: a complex is written as its two components, so it needs a
    // writer of its own rather than one of the scalar ones.
    if (ty == complexTy()) {
        auto* re = builder.CreateExtractValue(val, 0, "cplx.re");
        auto* im = builder.CreateExtractValue(val, 1, "cplx.im");
        auto* voidT = llvm::Type::getVoidTy(ctx);
        if (fp) {
            builder.CreateCall(getExternFnN("plang_write_file_cplx", voidT,
                                            {ptrTy, dblTy, dblTy}), {fp, re, im});
            if (newline)
                builder.CreateCall(
                    getExternFnN("plang_writeln_file", voidT, {ptrTy}), {fp});
        } else {
            builder.CreateCall(
                getExternFnN(newline ? "plang_writeln_cplx" : "plang_write_cplx",
                             voidT, {dblTy, dblTy}), {re, im});
        }
        return;
    }
    const bool isBool = writesAsBoolean(ty, semaTy);
    const bool isChar = !isBool && writesAsChar(ty, semaTy);
    if (isChar && !ty->isIntegerTy(8)) {
        val = builder.CreateTrunc(val, i8Ty, "char.narrow");
        ty  = val->getType();
    }
    auto* voidTy    = llvm::Type::getVoidTy(ctx);
    std::string pfx = fp ? "plang_write_file" : (std::string("plang_write") + (newline ? "ln" : ""));

    auto callStdout = [&](const std::string& fn, llvm::Type* pt, llvm::Value* v) {
        builder.CreateCall(getRuntimeFn(fn, pt), {v});
    };
    auto callFile = [&](const std::string& fn, llvm::Type* pt, llvm::Value* v) {
        builder.CreateCall(getExternFnN(fn, voidTy, {ptrTy, pt}), {fp, v});
    };

    if (ty->isIntegerTy(64)) {
        fp ? callFile("plang_write_file_i64", i64Ty, val)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_i64", i64Ty, val);
    } else if (ty->isDoubleTy()) {
        fp ? callFile("plang_write_file_f64", dblTy, val)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_f64", dblTy, val);
    } else if (isBool) {
        auto* ext = ty->isIntegerTy(1)
            ? builder.CreateZExt(val, i8Ty, "bool.ext") : val;
        fp ? callFile("plang_write_file_bool", i8Ty, ext)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_bool", i8Ty, ext);
    } else if (ty->isIntegerTy(8)) {
        fp ? callFile("plang_write_file_char", i8Ty, val)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_char", i8Ty, val);
    } else {
        fp ? callFile("plang_write_file_str", ptrTy, val)
           : callStdout(std::string("plang_write") + (newline?"ln":"") + "_str", ptrTy, val);
    }
    // File writeln: append newline after value
    if (fp && newline)
        builder.CreateCall(getExternFnN("plang_writeln_file", voidTy, {ptrTy}), {fp});
}

// Emit a write call with field-width (and optional decimal-places) formatting.
void Codegen::Impl::emitWriteValueFormatted(llvm::Value* val, llvm::Value* w, llvm::Value* d,
                                             bool newline, llvm::Value* fp,
                                             const plang::Type* semaTy) {
    if (!w) { emitWriteValue(val, newline, fp, semaTy); return; } // no width
    auto* voidTy = llvm::Type::getVoidTy(ctx);
    std::string nl = newline ? "ln" : "";
    llvm::Type* ty = val->getType();
    if (ty == complexTy()) {
        auto* re = builder.CreateExtractValue(val, 0, "cplx.re");
        auto* im = builder.CreateExtractValue(val, 1, "cplx.im");
        auto* dv = d ? d : llvm::ConstantInt::get(i64Ty, -1, true);
        if (fp) {
            builder.CreateCall(
                getExternFnN("plang_write_file_cplx_w", voidTy,
                             {ptrTy, dblTy, dblTy, i64Ty, i64Ty}),
                {fp, re, im, w, dv});
            if (newline)
                builder.CreateCall(
                    getExternFnN("plang_writeln_file", voidTy, {ptrTy}), {fp});
        } else {
            builder.CreateCall(
                getExternFnN("plang_write" + nl + "_cplx_w", voidTy,
                             {dblTy, dblTy, i64Ty, i64Ty}),
                {re, im, w, dv});
        }
        return;
    }
    const bool isBool = writesAsBoolean(ty, semaTy);
    if (!isBool && writesAsChar(ty, semaTy) && !ty->isIntegerTy(8)) {
        val = builder.CreateTrunc(val, i8Ty, "char.narrow");
        ty  = val->getType();
    }

    // A file destination takes a different family of writers, and the newline
    // is a separate call rather than part of the name.
    if (fp) {
        auto callFile = [&](const std::string& fn,
                            std::initializer_list<llvm::Type*> argTys,
                            std::initializer_list<llvm::Value*> args) {
            std::vector<llvm::Type*>  tys{ptrTy};
            std::vector<llvm::Value*> vs{fp};
            tys.insert(tys.end(), argTys);
            vs.insert(vs.end(), args);
            builder.CreateCall(getExternFnN(fn, voidTy, tys), vs);
        };
        if (ty->isIntegerTy(64)) {
            callFile("plang_write_file_i64_w", {i64Ty, i64Ty}, {val, w});
        } else if (ty->isDoubleTy()) {
            if (d) callFile("plang_write_file_f64_f", {dblTy, i64Ty, i64Ty}, {val, w, d});
            else   callFile("plang_write_file_f64_e", {dblTy, i64Ty}, {val, w});
        } else if (isBool) {
            auto* ext = ty->isIntegerTy(1)
                ? builder.CreateZExt(val, i8Ty, "bool.ext") : val;
            callFile("plang_write_file_bool_w", {i8Ty, i64Ty}, {ext, w});
        } else if (ty->isIntegerTy(8)) {
            callFile("plang_write_file_char_w", {i8Ty, i64Ty}, {val, w});
        } else {
            callFile("plang_write_file_str_w", {ptrTy, i64Ty}, {val, w});
        }
        if (newline)
            builder.CreateCall(getExternFnN("plang_writeln_file", voidTy, {ptrTy}), {fp});
        return;
    }

    if (ty->isIntegerTy(64)) {
        builder.CreateCall(getExternFnN("plang_write" + nl + "_i64_w", voidTy, {i64Ty, i64Ty}),
                           {val, w});
    } else if (ty->isDoubleTy()) {
        if (d)
            builder.CreateCall(getExternFnN("plang_write" + nl + "_f64_f", voidTy, {dblTy, i64Ty, i64Ty}),
                               {val, w, d});
        else
            builder.CreateCall(getExternFnN("plang_write" + nl + "_f64_e", voidTy, {dblTy, i64Ty}),
                               {val, w});
    } else if (isBool) {
        auto* ext = ty->isIntegerTy(1)
            ? builder.CreateZExt(val, i8Ty, "bool.ext") : val;
        builder.CreateCall(getExternFnN("plang_write" + nl + "_bool_w", voidTy, {i8Ty, i64Ty}),
                           {ext, w});
    } else if (ty->isIntegerTy(8)) {
        builder.CreateCall(getExternFnN("plang_write" + nl + "_char_w", voidTy, {i8Ty, i64Ty}),
                           {val, w});
    } else {
        builder.CreateCall(getExternFnN("plang_write" + nl + "_str_w", voidTy, {ptrTy, i64Ty}),
                           {val, w});
    }
}

// Helper to determine read type for an argument.
std::string Codegen::Impl::readFnSuffix(llvm::Type* ty) {
    if (ty->isDoubleTy())        return "_f64";
    if (ty->isIntegerTy(8))      return "_char";
    return "_i64";
}

/// Reads one variable from fp (or stdin when fp is null).  Does not touch the
/// line terminator, so callers compose readln as "read each, then skip line".
void Codegen::Impl::emitReadArg(const ExprNode& arg, llvm::Value* fp) {
    // Returning quietly would leave the variable out of the read entirely.
    auto* addr = emitLValue(arg);
    if (!addr) codegenICE("read/readln target is not an assignable variable");

    // string(N) is a { i64, [N x i8] } struct, so it needs the string reader;
    // the scalar readers would overwrite the length field.
    if (exprIsVarStr(arg)) {
        auto* cap = llvm::ConstantInt::get(i64Ty, exprStrCap(arg), true);
        if (fp)
            builder.CreateCall(
                getExternFnN("plang_str_read_file", llvm::Type::getVoidTy(ctx),
                             {ptrTy, ptrTy, i64Ty}),
                {fp, addr, cap});
        else
            builder.CreateCall(
                getStrFn("plang_str_read", llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty}),
                {addr, cap});
        return;
    }

    llvm::Type* ty = i64Ty;
    if (auto* id = llvm::dyn_cast<IdentExpr>(&arg))
        if (auto* ve = findVar(id->Name)) ty = ve->type;

    if (fp) {
        builder.CreateCall(
            getExternFnN("plang_read_file" + readFnSuffix(ty),
                         llvm::Type::getVoidTy(ctx), {ptrTy, ptrTy}),
            {fp, addr});
    } else {
        builder.CreateCall(getRuntimeFn("plang_read" + readFnSuffix(ty), ptrTy), {addr});
    }
}

/// Advances past the rest of the current line on fp (or stdin).
void Codegen::Impl::emitSkipLine(llvm::Value* fp) {
    if (fp)
        builder.CreateCall(getExternFnN("plang_readln_file",
            llvm::Type::getVoidTy(ctx), {ptrTy}), {fp});
    else
        builder.CreateCall(getRuntimeFn("plang_readln", nullptr), {});
}

void Codegen::Impl::emitBuiltinRead(const std::vector<std::unique_ptr<ExprNode>>& args) {
    size_t start = 0;
    llvm::Value* fp = nullptr;
    bool binaryTyped = false;
    if (!args.empty() && isFileVar(*args[0])) {
        fp          = fileVarPtr(*args[0]);
        start       = 1;
        binaryTyped = isTypedBinaryFileVar(*args[0]);
    }

    for (size_t i = start; i < args.size(); ++i) {
        if (binaryTyped && fp) {
            auto* addr = emitLValue(*args[i]);
            if (!addr) continue;
            // ISO §6.9.1: read(f,v) is v := f^, so a component's worth is read
            // however wide the variable is, and only then converted.
            llvm::Type* compTy = getFileElemType(*args[0]);
            llvm::Type* dstTy  = nullptr;
            if (auto* id = llvm::dyn_cast<IdentExpr>(args[i].get()))
                if (auto* ve = findVar(id->Name)) dstTy = ve->type;
            if (!compTy) compTy = dstTy ? dstTy : i64Ty;
            const bool convert = dstTy && dstTy != compTy
                                 && dstTy->isSingleValueType()
                                 && compTy->isSingleValueType();
            auto* dest = convert ? createEntryAlloca(compTy, "bin.rd.tmp") : addr;
            int64_t esz = (int64_t)mod->getDataLayout().getTypeAllocSize(compTy);
            builder.CreateCall(
                getExternFnN("plang_read_binary", llvm::Type::getVoidTy(ctx),
                             {ptrTy, ptrTy, i64Ty}),
                {fp, dest, llvm::ConstantInt::get(i64Ty, esz)});
            if (convert)
                builder.CreateStore(
                    coerceToType(builder.CreateLoad(compTy, dest, "bin.rd"), dstTy),
                    addr);
        } else {
            emitReadArg(*args[i], fp);
        }
    }
}

// ISO §6.9.2: readln takes every variable from the current line and only then
// advances to the next one, so the values must use the readers that leave the
// terminator alone and the line is consumed exactly once at the end.
void Codegen::Impl::emitBuiltinReadln(const std::vector<std::unique_ptr<ExprNode>>& args) {
    size_t start = 0;
    llvm::Value* fp = nullptr;
    if (!args.empty() && isFileVar(*args[0])) { fp = fileVarPtr(*args[0]); start = 1; }

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
void Codegen::Impl::emitBuiltinWriteStr(
        const std::vector<std::unique_ptr<ExprNode>>& args) {
    const ExprNode& dest = *args[0];
    if (!exprIsVarStr(dest))
        codegenICE("writestr destination is not a string variable");
    auto* sPtr = emitLValue(dest);
    if (!sPtr) codegenICE("writestr destination is not assignable");

    auto* voidTy = llvm::Type::getVoidTy(ctx);
    builder.CreateCall(getExternFnN("plang_writestr_begin", voidTy, {}), {});

    emitWriteArgs(args, /*start=*/1, /*newline=*/false, /*fp=*/nullptr,
                  /*binaryTyped=*/false);

    builder.CreateCall(
        getExternFnN("plang_writestr_end", voidTy, {ptrTy, i64Ty}),
        {sPtr, llvm::ConstantInt::get(i64Ty, exprStrCap(dest))});
}

/// readstr(e, v1, ..., vn) — parse the variables out of the string e.
void Codegen::Impl::emitBuiltinReadStr(
        const std::vector<std::unique_ptr<ExprNode>>& args) {
    const ExprNode& src = *args[0];
    auto* voidTy = llvm::Type::getVoidTy(ctx);

    // The source may be a string(N) struct or a plain character pointer.
    llvm::Value* data = nullptr;
    llvm::Value* len  = nullptr;
    if (exprIsVarStr(src)) {
        auto* sPtr = emitLValue(src);
        if (!sPtr) sPtr = emitExpr(src);
        if (!sPtr) codegenICE("readstr source is not a readable string");
        // string(N) is { i64 length, [N x i8] data }.
        len  = builder.CreateLoad(i64Ty, sPtr, "readstr.len");
        data = builder.CreateConstGEP1_64(i8Ty, sPtr, 8, "readstr.data");
    } else {
        data = emitExpr(src);
        if (!data || !data->getType()->isPointerTy())
            codegenICE("readstr source is not a string expression");
        len = builder.CreateCall(
            getExternFnN("strlen", i64Ty, {ptrTy}), {data}, "readstr.len");
    }
    builder.CreateCall(getExternFnN("plang_readstr_begin", voidTy, {ptrTy, i64Ty}),
                       {data, len});

    for (size_t i = 1; i < args.size(); ++i) emitReadArg(*args[i], /*fp=*/nullptr);

    builder.CreateCall(getExternFnN("plang_readstr_end", voidTy, {}), {});
}
