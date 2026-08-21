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
    return setOps_->toSetWidth(v);
}

llvm::Value* Codegen::Impl::clampToSetWidth(llvm::Value* v) {
    return setOps_->clampToSetWidth(v);
}

int64_t Codegen::Impl::setBaseOf(const ExprNode& e) {
    return setOps_->setBaseOf(e);
}

llvm::Value* Codegen::Impl::alignSet(llvm::Value* v, int64_t from, int64_t to) {
    return setOps_->alignSet(v, from, to);
}

llvm::Value* Codegen::Impl::setBitIndex(llvm::Value* ordinal, int64_t base) {
    return setOps_->setBitIndex(ordinal, base);
}

llvm::Value* Codegen::Impl::emitSetSingleton(llvm::Value* ordinal, int64_t base) {
    return setOps_->emitSetSingleton(ordinal, base);
}

llvm::Value* Codegen::Impl::emitSetRange(llvm::Value* lo, llvm::Value* hi,
                                         int64_t base) {
    return setOps_->emitSetRange(lo, hi, base);
}

llvm::Value* Codegen::Impl::emitSetMember(llvm::Value* ordinal, llvm::Value* set,
                                          int64_t base) {
    return setOps_->emitSetMember(ordinal, set, base);
}

llvm::Value* Codegen::Impl::emitSetBinary(TokenKind op, llvm::Value* a,
                                          llvm::Value* b) {
    return setOps_->emitSetBinary(op, a, b);
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
