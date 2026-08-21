#include "CodeGenImpl.h"
using namespace plang;

// ====================================================================
// String interning
// ====================================================================

llvm::GlobalVariable* Codegen::Impl::internStrGV(const std::string& content) {
    return strings_->internStrGV(content);
}

llvm::Value* Codegen::Impl::internStrPtr(const std::string& content) {
    return strings_->internStrPtr(content);
}

llvm::Constant* Codegen::Impl::internStrStruct(const std::string& content) {
    return strings_->internStrStruct(content);
}

// ====================================================================
// Sets (ISO §6.7.2.4)
//
// A set is a flat bitmask, one bit per ordinal of the base type.  Every
// operation below is emitted inline: the bitwise ones map directly to LLVM
// instructions, which avoids having to define a calling convention for a
// 256-bit value crossing into the C runtime.
//
// Membership and construction clamp their ordinal so an out-of-range value
// yields the empty set or false rather than a shift past the type width,
// which LLVM treats as poison.
// ====================================================================

llvm::Value* Codegen::Impl::toSetWidth(llvm::Value* v) {
    auto* st = setTy();
    if (v->getType() == st) return v;
    if (v->getType()->isIntegerTy())
        return builder.CreateZExtOrTrunc(v, st, "set.cast");
    codegenICE("expected an integer value where a set was required");
}

/// True when 0 <= ord < PlangMaxSetElements, plus the ordinal clamped into
/// that window so the caller can shift by it unconditionally.
static llvm::Value* clampOrdinal(llvm::IRBuilder<>& b, llvm::Type* i64Ty,
                                 llvm::Value* ord, llvm::Value*& inRange) {
    auto* lo = b.CreateICmpSGE(ord, llvm::ConstantInt::get(i64Ty, 0), "set.ge0");
    auto* hi = b.CreateICmpSLT(ord,
        llvm::ConstantInt::get(i64Ty, PlangMaxSetElements), "set.ltmax");
    inRange  = b.CreateAnd(lo, hi, "set.inrange");
    return b.CreateSelect(inRange, ord, llvm::ConstantInt::get(i64Ty, 0), "set.ord");
}

/// Clamps v into [0, PlangMaxSetElements - 1].
llvm::Value* Codegen::Impl::clampToSetWidth(llvm::Value* v) {
    auto* zero = llvm::ConstantInt::get(i64Ty, 0);
    auto* max  = llvm::ConstantInt::get(i64Ty, PlangMaxSetElements - 1);
    auto  pick = [&](llvm::Intrinsic::ID id, llvm::Value* a, llvm::Value* b) {
        return builder.CreateCall(
            llvm::Intrinsic::getOrInsertDeclaration(mod.get(), id, {i64Ty}), {a, b});
    };
    return pick(llvm::Intrinsic::smin, pick(llvm::Intrinsic::smax, v, zero), max);
}

int64_t Codegen::Impl::setBaseOf(const ExprNode& e) {
    const auto& t = e.ResolvedType;
    return (t && t->Kind == TypeKind::Set) ? setOffsetOf(*t) : 0;
}

llvm::Value* Codegen::Impl::alignSet(llvm::Value* v, int64_t from, int64_t to) {
    if (!v || from == to) return v;
    // A member sits at the bit given by its ordinal less the window's origin,
    // so moving a value from a window based at `from` into one based at `to`
    // moves every bit by the difference between the two origins.
    //
    // ISO §6.4.5 c) makes two set types compatible when their base types are,
    // and two compatible bases need not begin at the same ordinal: `set of
    // -5..10` and `set of 0..10` are compatible and their windows are five bits
    // apart.  Without this the bits were carried across unmoved and every
    // member came out shifted — {1, 3} read back as {-4, -2}.
    auto* st  = setTy();
    auto* amt = llvm::ConstantInt::get(
        st, static_cast<uint64_t>(from > to ? from - to : to - from));
    auto* x = toSetWidth(v);
    return from > to ? builder.CreateShl (x, amt, "set.align")
                     : builder.CreateLShr(x, amt, "set.align");
}

llvm::Value* Codegen::Impl::alignSetArg(llvm::Value* v, const ExprNode& arg,
                                        const std::string& mangled,
                                        size_t astArgIdx) {
    // A var parameter arrives as an address, and ISO §6.6.3.3 requires its
    // actual to be of the parameter's own type, so no window is crossed there.
    if (!v || v->getType() != setTy()) return v;
    const auto It = paramMeta_.find(mangled);
    if (It == paramMeta_.end() || astArgIdx >= It->second.size()) return v;
    return alignSet(v, setBaseOf(arg), It->second[astArgIdx].setBase);
}

llvm::Value* Codegen::Impl::setBitIndex(llvm::Value* ordinal, int64_t base) {
    auto* ord = toI64(ordinal);
    if (base == 0) return ord;
    return builder.CreateSub(ord, llvm::ConstantInt::get(i64Ty, base, true),
                             "set.rebase");
}

llvm::Value* Codegen::Impl::emitSetSingleton(llvm::Value* ordinal, int64_t base) {
    auto* st    = setTy();
    auto* zero  = llvm::ConstantInt::get(st, 0);
    llvm::Value* inRange = nullptr;
    auto* ord   = clampOrdinal(builder, i64Ty, setBitIndex(ordinal, base), inRange);
    auto* bit   = builder.CreateShl(llvm::ConstantInt::get(st, 1),
                                    builder.CreateZExt(ord, st), "set.bit");
    return builder.CreateSelect(inRange, bit, zero, "set.single");
}

llvm::Value* Codegen::Impl::emitSetRange(llvm::Value* lo, llvm::Value* hi,
                                         int64_t base) {
    auto* st       = setTy();
    auto* zero     = llvm::ConstantInt::get(st, 0);
    auto* allOnes  = llvm::ConstantInt::getAllOnesValue(st);
    auto* i64Zero  = llvm::ConstantInt::get(i64Ty, 0);
    auto* i64Max   = llvm::ConstantInt::get(i64Ty, PlangMaxSetElements - 1);

    // Emptiness is decided from the original bounds; the clamped copies below
    // only keep the shift amounts inside the type width, since a shift past it
    // is poison in LLVM even on a path whose result is discarded.
    auto* l = setBitIndex(lo, base);
    auto* h = setBitIndex(hi, base);
    auto* empty = builder.CreateOr(
        builder.CreateICmpSGT(l, h, "set.lo.gt.hi"),
        builder.CreateOr(builder.CreateICmpSGT(l, i64Max, "set.lo.big"),
                         builder.CreateICmpSLT(h, i64Zero, "set.hi.neg")),
        "set.empty");
    auto* lClamped = clampToSetWidth(l);
    auto* hClamped = clampToSetWidth(h);

    // bits >= lo
    auto* lowBits = builder.CreateShl(allOnes,
        builder.CreateZExt(lClamped, st), "set.lowbits");

    // bits > hi, i.e. allOnes << (hi + 1).  hi == max would shift by the full
    // width, so substitute a shift of 0 and select the result away.
    auto* hiIsMax = builder.CreateICmpEQ(hClamped, i64Max, "set.hi.ismax");
    auto* shAmt   = builder.CreateSelect(hiIsMax, i64Zero,
        builder.CreateAdd(hClamped, llvm::ConstantInt::get(i64Ty, 1)), "set.hi.sh");
    auto* above   = builder.CreateSelect(hiIsMax, zero,
        builder.CreateShl(allOnes, builder.CreateZExt(shAmt, st)), "set.above");

    auto* mask = builder.CreateAnd(lowBits, builder.CreateNot(above), "set.range");
    return builder.CreateSelect(empty, zero, mask, "set.range.chk");
}

llvm::Value* Codegen::Impl::emitSetMember(llvm::Value* ordinal, llvm::Value* set,
                                          int64_t base) {
    auto* st = setTy();
    llvm::Value* inRange = nullptr;
    auto* ord   = clampOrdinal(builder, i64Ty, setBitIndex(ordinal, base), inRange);
    auto* shifted = builder.CreateLShr(toSetWidth(set),
                                       builder.CreateZExt(ord, st), "set.shr");
    auto* bit = builder.CreateTrunc(shifted, i1Ty, "set.bit");
    return builder.CreateAnd(inRange, bit, "set.in");
}

llvm::Value* Codegen::Impl::emitSetBinary(TokenKind op, llvm::Value* a,
                                          llvm::Value* b) {
    auto* x = toSetWidth(a);
    auto* y = toSetWidth(b);
    switch (op) {
        case TokenKind::Plus:     return builder.CreateOr(x, y, "set.union");
        case TokenKind::Times:    return builder.CreateAnd(x, y, "set.isect");
        case TokenKind::Minus:
            return builder.CreateAnd(x, builder.CreateNot(y), "set.diff");
        case TokenKind::SymDiff:  return builder.CreateXor(x, y, "set.symdiff");
        case TokenKind::Equal:    return builder.CreateICmpEQ(x, y, "set.eq");
        case TokenKind::NotEqual: return builder.CreateICmpNE(x, y, "set.ne");
        // a <= b holds when a contributes no bits outside b.
        case TokenKind::LessThanOrEqual:
            return builder.CreateICmpEQ(builder.CreateAnd(x, y), x, "set.subset");
        case TokenKind::GreaterThanOrEqual:
            return builder.CreateICmpEQ(builder.CreateAnd(x, y), y, "set.supset");
        default: return nullptr;
    }
}

// ====================================================================
// ISO runtime checks
// ====================================================================

void Codegen::Impl::emitGuard(llvm::Value* failCond, const char* name,
                              llvm::function_ref<void()> emitFail) {
    rangeGuards_->emitGuard(failCond, name, emitFail);
}

void Codegen::Impl::emitDivZeroCheck(llvm::Value* divisor, const char* op) {
    rangeGuards_->emitDivZeroCheck(divisor, op);
}

void Codegen::Impl::emitModDivisorCheck(llvm::Value* divisor) {
    rangeGuards_->emitModDivisorCheck(divisor);
}

void Codegen::Impl::emitNilCheck(llvm::Value* ptr) {
    rangeGuards_->emitNilCheck(ptr);
}

void Codegen::Impl::emitRangeCheck(llvm::Value* val, int64_t lo, int64_t hi,
                                   bool isIndex, SourceLocation Loc) {
    rangeGuards_->emitRangeCheck(val, lo, hi, isIndex, Loc);
}

void Codegen::Impl::emitRangeCheckDyn(llvm::Value* val, llvm::Value* lo,
                                      llvm::Value* hi, bool isIndex,
                                      SourceLocation Loc) {
    rangeGuards_->emitRangeCheckDyn(val, lo, hi, isIndex, Loc);
}

// ====================================================================
// External function declarations
// ====================================================================

llvm::Function* Codegen::Impl::getExternFn(const std::string& name, llvm::FunctionType* ty) {
    return runtimeFns_->getExternFn(name, ty);
}

llvm::Function* Codegen::Impl::getRTMathRR(const std::string& name) {
    return runtimeFns_->getRTMathRR(name);
}

llvm::Function* Codegen::Impl::getRTMathRI(const std::string& name) {
    return runtimeFns_->getRTMathRI(name);
}

llvm::Function* Codegen::Impl::getRTMathII(const std::string& name) {
    return runtimeFns_->getRTMathII(name);
}

llvm::Function* Codegen::Impl::getRuntimeFn(const std::string& name, llvm::Type* argTy) {
    return runtimeFns_->getRuntimeFn(name, argTy);
}

llvm::Function* Codegen::Impl::getRuntimeBoolFn(const std::string& name) {
    return runtimeFns_->getRuntimeBoolFn(name);
}

llvm::Function* Codegen::Impl::getExternFnN(const std::string& name,
                                              llvm::Type* retTy,
                                              std::vector<llvm::Type*> params) {
    return runtimeFns_->getExternFnN(name, retTy, std::move(params));
}

// ---- file-variable helpers ----

bool Codegen::Impl::isTextTypeName(const TypeNode* tn) {
    if (auto* nn = llvm::dyn_cast<NamedTypeNode>(tn))
        return eqCI(nn->Name, "text");
    return false;
}

bool Codegen::Impl::isFileVar(const ExprNode& e) {
    // ISO §6.6.5.2 takes a file-variable, and a variable is anything §6.5
    // calls one: an element of an array of text is as much a file as a
    // variable whose name is written on its own.  Reading only the name meant
    // `rewrite(avf[i])` found nothing and passed null to the runtime.
    if (e.ResolvedType && e.ResolvedType->Kind == TypeKind::File) return true;
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e)) {
        auto* ve = findVar(id->Name);
        if (!ve) return false;
        // Program file-parameters have no declaring TypeNode, so fall back to
        // the storage type, which is authoritative either way.
        if (ve->type == fileStructType()) return true;
        if (ve->typeNode)
            return llvm::dyn_cast<FileTypeNode>(ve->typeNode) != nullptr
                || isTextTypeName(ve->typeNode);
    }
    return false;
}

llvm::Value* Codegen::Impl::fileVarPtr(const ExprNode& e) {
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e)) {
        auto* ve = findVar(id->Name);
        if (ve) return ve->ptr;
    }
    // A component of a structure, or whatever a pointer leads to: the address
    // is what the runtime wants and emitLValue is what produces one.
    return isFileVar(e) ? emitLValue(e) : nullptr;
}

const Type* Codegen::Impl::fileTypeOf(const ExprNode& e) {
    if (const Type* T = e.ResolvedType.get())
        if (T->Kind == TypeKind::File) return T;
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e))
        if (auto* ve = findVar(id->Name))
            if (ve->typeNode && ve->typeNode->ResolvedType
                    && ve->typeNode->ResolvedType->Kind == TypeKind::File)
                return ve->typeNode->ResolvedType.get();
    return nullptr;
}

bool Codegen::Impl::isTypedBinaryFileVar(const ExprNode& e) {
    // Whether the file holds records or characters decides which of two
    // unrelated representations it is written in, so reading the denoter and
    // taking a name for a text file is not a near miss.  `file of integer`
    // under a type name went down the text path and wrote its numbers out as
    // digits, and read them back as one number with the digits run together.
    const Type* T = fileTypeOf(e);
    if (!T || !T->ElemType) return false;   // untyped: byte-level
    // ISO §6.4.3.5: a file of char is a text file.
    return T->ElemType->Kind != TypeKind::Char;
}

llvm::Value* Codegen::Impl::fileBufferPtr(const ExprNode& fileExpr) {
    auto* fp = fileVarPtr(fileExpr);
    if (!fp) return nullptr;
    // §6.4.3.5 gives f^ the value of a space when eoln(f), which only holds of
    // a text file: on a one-byte binary file the same byte is a component with
    // the value 10 and has to arrive intact.  The two are indistinguishable
    // from the component size alone.
    auto* fn = getExternFnN("plang_file_buffer", ptrTy, {ptrTy, i64Ty, i8Ty});
    return builder.CreateCall(
        fn,
        {fp, llvm::ConstantInt::get(i64Ty, getFileElemSize(fileExpr)),
         llvm::ConstantInt::get(i8Ty, isTypedBinaryFileVar(fileExpr) ? 0 : 1)},
        "file.buf");
}

llvm::Type* Codegen::Impl::getFileElemType(const ExprNode& fileExpr) {
    if (auto* id = llvm::dyn_cast<IdentExpr>(&fileExpr))
        if (auto* ve = findVar(id->Name))
            if (auto* ftn = llvm::dyn_cast_or_null<FileTypeNode>(ve->typeNode))
                if (ftn->Element) return llvmTypeOfNode(*ftn->Element);
    // The denoter recorded for the variable is a name when the file type was
    // declared under one, and a name says nothing about what the file holds.
    // Coming away with nothing here means an element size of one byte, so
    // every record read or written would be a byte of it.
    if (const Type* T = fileTypeOf(fileExpr))
        if (T->ElemType) return llvmTypeOfSemaType(*T->ElemType);
    return nullptr;
}

int64_t Codegen::Impl::getFileElemSize(const ExprNode& fileExpr) {
    if (auto* elemTy = getFileElemType(fileExpr))
        return (int64_t)mod->getDataLayout().getTypeAllocSize(elemTy);
    return 1; // text, or an untyped file: byte-level
}

int64_t Codegen::Impl::getFileIndexLow(const ExprNode& fileExpr) {
    if (const Type* T = fileTypeOf(fileExpr))
        if (T->IndexType) return T->IndexType->SubLo;
    return 0;
}

llvm::Function* Codegen::Impl::getRuntimeNewFn() {
    return runtimeFns_->getRuntimeNewFn();
}

llvm::Function* Codegen::Impl::getRuntimeDisposeFn() {
    return runtimeFns_->getRuntimeDisposeFn();
}

llvm::Function* Codegen::Impl::getRuntimeHaltFn() {
    return runtimeFns_->getRuntimeHaltFn();
}

// ====================================================================
// EP String helpers
// ====================================================================

llvm::Function* Codegen::Impl::getStrFn(const std::string& name, llvm::Type* retTy,
                                         std::initializer_list<llvm::Type*> argTys) {
    return getExternFnN(name, retTy, argTys);
}

llvm::Value* Codegen::Impl::strLoadLen(llvm::Value* strPtr) {
    (void)builder.CreateStructGEP(
        llvm::StructType::get(ctx, {i64Ty, llvm::ArrayType::get(i8Ty, 0)}),
        strPtr, 0, "len.ptr");
    // Use a plain load via GEP into the ptr (opaque pointers: just load i64 at offset 0)
    return builder.CreateLoad(i64Ty, strPtr, "str.len");
}

llvm::Value* Codegen::Impl::strDataPtr(llvm::Value* strPtr) {
    // data is at byte offset 8 (after the i64 length field)
    auto* i8Ptr = builder.CreateConstGEP1_64(i8Ty, strPtr, 8, "str.data");
    return i8Ptr;
}

void Codegen::Impl::emitStrAssign(llvm::Value* dst, llvm::Value* capDst,
                                   llvm::Value* src, llvm::Value* capSrc) {
    auto* fn = getStrFn("plang_str_assign",
        llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, ptrTy, i64Ty});
    builder.CreateCall(fn, {dst, capDst, src, capSrc});
}

void Codegen::Impl::emitStrFromCStr(llvm::Value* dst, llvm::Value* cap,
                                    llvm::Value* cstr) {
    auto* fn = getStrFn("plang_str_from_cstr",
        llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, ptrTy});
    builder.CreateCall(fn, {dst, cap, cstr});
}

void Codegen::Impl::emitStrFromChar(llvm::Value* dst, llvm::Value* cap,
                                    llvm::Value* c) {
    auto* fn = getStrFn("plang_str_from_char",
        llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, i8Ty});
    builder.CreateCall(fn, {dst, cap, c});
}

llvm::Value* Codegen::Impl::emitCallArg(const ExprNode& arg,
                                        llvm::Type* paramTy, bool byRef) {
    // ISO §6.6.3.2: a value parameter of pointer type is given the pointer,
    // not the place the pointer was read from.  Both are `ptr` in the
    // signature, so only the formal's kind tells them apart.
    if (!byRef && paramTy == ptrTy && arg.ResolvedType
            && arg.ResolvedType->Kind == TypeKind::Pointer)
        return emitExpr(arg);
    // A string parameter taken by value receives the whole { length, bytes }
    // struct, and its capacity is the callee's, not the argument's.  Build the
    // copy at the callee's width and hand over its value; passing the
    // argument's own address, which is what a string expression evaluates to,
    // would not even be the right type.
    // A char or a plain literal is string-compatible with the parameter, and
    // arrives here as something other than a string; emitStrStore knows how to
    // widen each of them, so the test is on the parameter, not the argument.
    const bool argIsStrLike =
        arg.ResolvedType
        && (arg.ResolvedType->Kind == TypeKind::VarString
            || arg.ResolvedType->Kind == TypeKind::String
            || arg.ResolvedType->Kind == TypeKind::Char);
    if (paramTy && paramTy->isStructTy() && argIsStrLike) {
        auto* st = llvm::cast<llvm::StructType>(paramTy);
        int64_t cap = 0;
        if (st->getNumElements() == 2)
            if (auto* at = llvm::dyn_cast<llvm::ArrayType>(st->getElementType(1)))
                cap = static_cast<int64_t>(at->getNumElements());
        if (cap > 0) {
            auto* tmp = createEntryAlloca(st, "str.arg");
            emitStrStore(tmp, cap, arg);
            return builder.CreateLoad(st, tmp, "str.arg.val");
        }
    }
    auto* v = paramTy == ptrTy ? emitLValue(arg) : emitExpr(arg);
    // ISO §6.6.3.2: a value parameter is a variable of its own that the actual
    // is assigned to, and an array is passed here as its value rather than its
    // address.  An array expression evaluates to an address — a string
    // constant is a global, an array variable its storage — so the value to
    // hand over is read from there.
    if (paramTy && paramTy->isArrayTy() && v && v->getType() == ptrTy)
        v = builder.CreateLoad(paramTy, v, "arr.arg");
    // §6.6.3.2 makes a value parameter a variable of its own that the actual is
    // *assigned* to, so §6.4.6's assignment compatibility applies and an
    // integer actual for a real formal widens.  Every other destination in the
    // compiler coerces -- emitAssign, emitFor, the constant initialisers -- and
    // this one did not, so `procedure scale(x: real)` called as `scale(3)`
    // emitted `call void @pas_scale(i64 3)` against a `void (double)` and the
    // module failed verification.  A program could not use a real parameter
    // without writing every actual as a real.
    if (paramTy && v && v->getType() != paramTy
            && paramTy->isSingleValueType() && v->getType()->isSingleValueType()
            && !paramTy->isPointerTy() && !v->getType()->isPointerTy())
        v = coerceToType(v, paramTy);
    return v;
}

llvm::Value* Codegen::Impl::emitStrAddr(const ExprNode& e) {
    // A component of a larger object has an address of its own, and taking it
    // is the only way to reach the component rather than a copy of it.  A
    // substring also has an address, but it is the address of the string it was
    // cut from, so it has to go the other way.
    if (llvm::isa<FieldExpr>(&e) || llvm::isa<IndexExpr>(&e)
        || llvm::isa<DerefExpr>(&e))
        if (auto* p = emitLValue(e))
            return p;
    return emitExpr(e);
}

llvm::Value* Codegen::Impl::emitCharStrAsStr(const ExprNode& e) {
    const int64_t n = exprCharStrLen(e);
    auto* src = emitLValue(e);
    if (!src) codegenICE("a string-type value with no address");
    auto* tmp = createEntryAlloca(strStructType(n), "chars.as.str");
    builder.CreateStore(llvm::ConstantInt::get(i64Ty, n), tmp);
    builder.CreateMemCpy(strDataPtr(tmp), llvm::MaybeAlign(),
                         src, llvm::MaybeAlign(),
                         llvm::ConstantInt::get(i64Ty, n));
    return tmp;
}

void Codegen::Impl::emitCharStrStore(llvm::Value* dst, int64_t n,
                                      const ExprNode& src) {
    // Sema has already held the source to exactly n characters, so this is a
    // straight copy of n bytes in every case.
    if (auto* sl = llvm::dyn_cast<StringLitExpr>(&src)) {
        builder.CreateMemCpy(dst, llvm::MaybeAlign(),
                             internStrPtr(sl->Value), llvm::MaybeAlign(),
                             llvm::ConstantInt::get(i64Ty, n));
        return;
    }
    if (exprIsCharStr(src)) {
        auto* from = emitLValue(src);
        if (!from) codegenICE("a string-type value with no address");
        builder.CreateMemCpy(dst, llvm::MaybeAlign(), from, llvm::MaybeAlign(),
                             llvm::ConstantInt::get(i64Ty, n));
        return;
    }
    // A string(n) source keeps its characters behind the length field.
    if (exprIsVarStr(src)) {
        auto* from = emitStrAddr(src);
        if (!from) codegenICE("a string value with no address");
        // §6.4.3.2 requires the lengths to match, and Sema settles that when it
        // knows the capacity.  It cannot when a discriminant fixes it, so it
        // lets the assignment through -- and copying n bytes out of a string
        // holding fewer read past the end of the allocation and dropped heap
        // bytes into the array.  Checked here instead, against the length the
        // string actually has.
        if (src.ResolvedType->ExtentVaries) {
            auto* len = strLoadLen(from);
            auto* bad = builder.CreateICmpNE(len, i64c(n), "charstr.len.bad");
            emitGuard(bad, "charstrlen", [&] {
                builder.CreateCall(
                    getExternFnN("plang_err_str_length",
                                 llvm::Type::getVoidTy(ctx), {i64Ty, i64Ty}),
                    {len, i64c(n)});
            });
        }
        builder.CreateMemCpy(dst, llvm::MaybeAlign(), strDataPtr(from),
                             llvm::MaybeAlign(),
                             llvm::ConstantInt::get(i64Ty, n));
        return;
    }
    auto* rhs = emitExpr(src);
    if (!rhs) codegenICE("string assignment from an unlowerable expression");
    builder.CreateMemCpy(dst, llvm::MaybeAlign(), rhs, llvm::MaybeAlign(),
                         llvm::ConstantInt::get(i64Ty, n));
}

void Codegen::Impl::emitStrStore(llvm::Value* dst, llvm::Value* capDst,
                                 const ExprNode& src) {
    // A literal is already a run of bytes; going through emitExpr would build
    // a string temporary first and copy it twice.
    if (auto* sl = llvm::dyn_cast<StringLitExpr>(&src)) {
        emitStrFromCStr(dst, capDst, internStrPtr(sl->Value));
        return;
    }
    // A string-type is n bytes with no length in front, so it has to be given
    // one before the string runtime can read it.
    if (exprIsCharStr(src)) {
        const int64_t n = exprCharStrLen(src);
        emitStrAssign(dst, capDst, emitCharStrAsStr(src), i64c(n));
        return;
    }
    // R5: the source's address and its capacity from ONE walk.  Taking the
    // value and then asking for the capacity resolved the access path twice,
    // so `z := q^.a[next].s` ran `next` more than once.
    if (exprIsVarStr(src)) {
        auto [sp, sc] = strAddrAndCap(src);
        if (!sp) codegenICE("string assignment from an unlowerable expression");
        emitStrAssign(dst, capDst, sp, sc);
        return;
    }
    auto* rhs = emitExpr(src); // char → i8; other → ptr (cstr)
    if (!rhs) codegenICE("string assignment from an unlowerable expression");
    if (rhs->getType()->isIntegerTy(8))
        emitStrFromChar(dst, capDst, rhs);
    else
        emitStrFromCStr(dst, capDst, rhs);
}
