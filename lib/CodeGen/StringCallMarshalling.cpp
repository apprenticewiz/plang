#include "StringCallMarshalling.h"

#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"

#include "CodegenICE.h"

using namespace plang;

llvm::Value* StringCallMarshalling::emitCallArg(const ExprNode& arg,
                                                 llvm::Type* paramTy, bool byRef) {
    // ISO §6.6.3.2: a value parameter of pointer type is given the pointer,
    // not the place the pointer was read from.  Both are `ptr` in the
    // signature, so only the formal's kind tells them apart.
    if (!byRef && paramTy == PtrTy && arg.ResolvedType
            && arg.ResolvedType->Kind == TypeKind::Pointer)
        return EmitExpr(arg);
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
            auto* tmp = CreateEntryAlloca(st, "str.arg");
            emitStrStore(tmp, i64c(cap), arg);
            return B.CreateLoad(st, tmp, "str.arg.val");
        }
    }
    auto* v = paramTy == PtrTy ? EmitLValue(arg) : EmitExpr(arg);
    // ISO §6.6.3.2: a value parameter is a variable of its own that the actual
    // is assigned to, and an array is passed here as its value rather than its
    // address.  An array expression evaluates to an address — a string
    // constant is a global, an array variable its storage — so the value to
    // hand over is read from there.
    if (paramTy && paramTy->isArrayTy() && v && v->getType() == PtrTy)
        v = B.CreateLoad(paramTy, v, "arr.arg");
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
        v = CoerceToType(v, paramTy);
    return v;
}

llvm::Value* StringCallMarshalling::emitStrAddr(const ExprNode& e) {
    // A component of a larger object has an address of its own, and taking it
    // is the only way to reach the component rather than a copy of it.  A
    // substring also has an address, but it is the address of the string it was
    // cut from, so it has to go the other way.
    if (llvm::isa<FieldExpr>(&e) || llvm::isa<IndexExpr>(&e)
        || llvm::isa<DerefExpr>(&e))
        if (auto* p = EmitLValue(e))
            return p;
    return EmitExpr(e);
}

llvm::Value* StringCallMarshalling::emitCStrArg(const ExprNode& e) {
    // Only a string(n) value needs converting -- everything else EmitExpr
    // returns for a string-shaped argument (a plain string literal outside
    // Extended Pascal, an already-null-terminated `String`) is already a
    // `char *`.
    if (!ExprIsVarStr(e)) return EmitExpr(e);
    auto* addr = emitStrAddr(e);
    if (!addr) codegenICE("a string value with no address");
    auto* len  = Strings.strLoadLen(addr);
    auto* data = Strings.strDataPtr(addr);
    auto* i8Ty = llvm::Type::getInt8Ty(Ctx);
    llvm::Value* buf;
    // ExprStrCap is exprStrCapStatic: a discriminant-fixed capacity is not a
    // compile-time constant (that's the whole point of a discriminant), so it
    // widens to PlangMaxStringCapacity there.  string(300) is legal, and its
    // 300 was new()'s runtime discriminant, never a compile-time probe --
    // sizing the buffer from that widened guess let `len` below, the actual
    // runtime length the memcpy copies, run past it: a 300-character value
    // memcpy'd into a 256-byte stack buffer, 45 bytes past the end of the
    // allocation.  Sized from `len` itself instead -- the very value the
    // memcpy and the NUL store already use below -- the buffer is exactly as
    // large as what fills it, by construction, no matter what the static
    // type could or couldn't tell us about the capacity behind it.
    if (e.ResolvedType->ExtentVaries) {
        auto* bytes = B.CreateAdd(len, i64c(1), "str.cstr.size");
        buf = CreateDynAlloca(bytes, "str.cstr");
    } else {
        // With a real constant capacity this is exact, so the buffer --
        // unlike the runtime length that fills it -- can be a reused
        // entry-block alloca the same way every other string temporary in
        // this file is.
        buf = CreateEntryAlloca(
            llvm::ArrayType::get(i8Ty, static_cast<uint64_t>(ExprStrCap(e)) + 1),
            "str.cstr");
    }
    B.CreateMemCpy(buf, llvm::MaybeAlign(), data, llvm::MaybeAlign(), len);
    auto* nulAt = B.CreateInBoundsGEP(i8Ty, buf, len, "str.cstr.nul");
    B.CreateStore(llvm::ConstantInt::get(i8Ty, 0), nulAt);
    return buf;
}

llvm::Value* StringCallMarshalling::emitCharStrAsStr(const ExprNode& e) {
    const int64_t n = ExprCharStrLen(e);
    auto* src = EmitLValue(e);
    if (!src) codegenICE("a string-type value with no address");
    auto* tmp = CreateEntryAlloca(Types.strStructType(n), "chars.as.str");
    B.CreateStore(llvm::ConstantInt::get(I64Ty, n), tmp);
    B.CreateMemCpy(Strings.strDataPtr(tmp), llvm::MaybeAlign(),
                   src, llvm::MaybeAlign(),
                   llvm::ConstantInt::get(I64Ty, n));
    return tmp;
}

void StringCallMarshalling::emitCharStrStore(llvm::Value* dst, int64_t n,
                                              const ExprNode& src) {
    // Sema has already held the source to exactly n characters, so this is a
    // straight copy of n bytes in every case.
    if (auto* sl = llvm::dyn_cast<StringLitExpr>(&src)) {
        B.CreateMemCpy(dst, llvm::MaybeAlign(),
                       Strings.internStrPtr(sl->Value), llvm::MaybeAlign(),
                       llvm::ConstantInt::get(I64Ty, n));
        return;
    }
    if (ExprIsCharStr(src)) {
        auto* from = EmitLValue(src);
        if (!from) codegenICE("a string-type value with no address");
        B.CreateMemCpy(dst, llvm::MaybeAlign(), from, llvm::MaybeAlign(),
                       llvm::ConstantInt::get(I64Ty, n));
        return;
    }
    // A string(n) source keeps its characters behind the length field.
    if (ExprIsVarStr(src)) {
        auto* from = emitStrAddr(src);
        if (!from) codegenICE("a string value with no address");
        // §6.4.3.2 requires the lengths to match, and Sema settles that when it
        // knows the capacity.  It cannot when a discriminant fixes it, so it
        // lets the assignment through -- and copying n bytes out of a string
        // holding fewer read past the end of the allocation and dropped heap
        // bytes into the array.  Checked here instead, against the length the
        // string actually has.
        if (src.ResolvedType->ExtentVaries) {
            auto* len = Strings.strLoadLen(from);
            auto* bad = B.CreateICmpNE(len, i64c(n), "charstr.len.bad");
            RangeGuards.emitGuard(bad, "charstrlen", [&] {
                B.CreateCall(
                    RtFns.getExternFnN("plang_err_str_length",
                                       llvm::Type::getVoidTy(Ctx), {I64Ty, I64Ty}),
                    {len, i64c(n)});
            });
        }
        B.CreateMemCpy(dst, llvm::MaybeAlign(), Strings.strDataPtr(from),
                       llvm::MaybeAlign(),
                       llvm::ConstantInt::get(I64Ty, n));
        return;
    }
    auto* rhs = EmitExpr(src);
    if (!rhs) codegenICE("string assignment from an unlowerable expression");
    B.CreateMemCpy(dst, llvm::MaybeAlign(), rhs, llvm::MaybeAlign(),
                   llvm::ConstantInt::get(I64Ty, n));
}

void StringCallMarshalling::emitStrStore(llvm::Value* dst, llvm::Value* capDst,
                                          const ExprNode& src) {
    // A literal is already a run of bytes; going through emitExpr would build
    // a string temporary first and copy it twice.
    if (auto* sl = llvm::dyn_cast<StringLitExpr>(&src)) {
        Strings.emitStrFromBytes(dst, capDst, Strings.internStrPtr(sl->Value),
                                 i64c(static_cast<int64_t>(sl->Value.size())));
        return;
    }
    // A string-type is n bytes with no length in front, so it has to be given
    // one before the string runtime can read it.
    if (ExprIsCharStr(src)) {
        const int64_t n = ExprCharStrLen(src);
        Strings.emitStrAssign(dst, capDst, emitCharStrAsStr(src), i64c(n));
        return;
    }
    // R5: the source's address and its capacity from ONE walk.  Taking the
    // value and then asking for the capacity resolved the access path twice,
    // so `z := q^.a[next].s` ran `next` more than once.
    if (ExprIsVarStr(src)) {
        auto [sp, sc] = Schema.strAddrAndCap(src);
        if (!sp) codegenICE("string assignment from an unlowerable expression");
        Strings.emitStrAssign(dst, capDst, sp, sc);
        return;
    }
    auto* rhs = EmitExpr(src); // char → i8; other → ptr (cstr)
    if (!rhs) codegenICE("string assignment from an unlowerable expression");
    if (rhs->getType()->isIntegerTy(8))
        Strings.emitStrFromChar(dst, capDst, rhs);
    else
        Strings.emitStrFromCStr(dst, capDst, rhs);
}
