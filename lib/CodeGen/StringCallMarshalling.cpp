#include "StringCallMarshalling.h"

#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"

#include "CodegenICE.h"
#include "OrdinalSignedness.h"

using namespace plang;

llvm::Value* StringCallMarshalling::emitCallArg(const ExprNode& arg,
                                                 llvm::Type* paramTy, bool byRef) {
    // ISO §6.6.3.2: a value parameter of pointer type is given the pointer,
    // not the place the pointer was read from.  Both are `ptr` in the
    // signature, so only the formal's kind tells them apart.
    //
    // `nil` itself is TypeKind::Nil, not TypeKind::Pointer (Sema's own
    // checkExpr gives a NilExpr type TyNil, SemaExpr.cpp) -- a real,
    // separate type so `nil` stays assignment-compatible with EVERY pointer
    // type rather than one fixed one, but it means a bare `Foo(nil)` call
    // reached the fallback below instead of this arm: `paramTy == PtrTy`
    // (true) sent it to EmitLValue(arg), which cannot take the "address" a
    // literal has none of and returned null -- an LLVM IR verifier failure
    // ("Operand is null") on ANY value-pointer-parameter call actually
    // passed a literal `nil`, not anything specific to an extern-declared
    // callee. Admitting TypeKind::Nil here too fixes it the same way
    // TypeKind::Pointer already was: EmitExpr(NilExpr) is exactly
    // ConstantPointerNull (CGExprCore.cpp), the right value either way.
    if (!byRef && paramTy == PtrTy && arg.ResolvedType
            && (arg.ResolvedType->Kind == TypeKind::Pointer
                || arg.ResolvedType->Kind == TypeKind::Nil))
        return EmitExpr(arg);
    // A string parameter taken by value receives the whole { length, bytes }
    // struct, and its capacity is the callee's, not the argument's.  Build the
    // copy at the callee's width and hand over its value; passing the
    // argument's own address, which is what a string expression evaluates to,
    // would not even be the right type.  This is also what makes Turbo's
    // "value parameters copied at the callee's declared width" work: passing
    // a string[20] actual to a `procedure p(s: string[5])` formal builds a
    // 5-capacity temporary and truncates into it (plang_sstr_assign), never
    // hands over or reads out of the caller's own 20-capacity storage.
    // A char or a plain literal is string-compatible with the parameter, and
    // arrives here as something other than a string; emitStrStore/
    // emitSstrStore each know how to widen one, so the test is on the
    // parameter, not the argument -- argIsStrLike below now also admits
    // ShortString for that same reason.
    const bool argIsStrLike =
        arg.ResolvedType
        && (arg.ResolvedType->Kind == TypeKind::VarString
            || arg.ResolvedType->Kind == TypeKind::ShortString
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
            // CONFIRMED-LIVE BUG, fixed here: EP's { i64, [N x i8] } and
            // ShortString's packed <{ i8, [N x i8] }> are BOTH two-element
            // structs whose second element is an [N x i8] array, so `cap`
            // just above -- read purely from that array's element count --
            // comes out right for either one, but WHICH RUNTIME FUNCTION
            // FAMILY to marshal through is a separate question the array
            // shape alone cannot answer.  Unconditionally calling
            // emitStrStore here, regardless of what `st` actually was, sent
            // a ShortString actual through plang_str_from_bytes/
            // plang_str_assign's EIGHT-BYTE length-header geometry into a
            // buffer that is really a ONE-BYTE-header ShortString struct --
            // silent corruption, no diagnostic.  `st->isPacked()` is what
            // actually tells the two apart: CGTypes::sstrStructType builds
            // ShortString's struct with isPacked=true SPECIFICALLY so this
            // is a guarantee rather than an accident of the current
            // DataLayout (see its own comment), while strStructType's EP
            // struct is never packed.  Checked here against the CALLEE's own
            // declared struct type, not guessed from the argument's kind.
            if (st->isPacked()) emitSstrStore(tmp, i64c(cap), arg);
            else                emitStrStore(tmp, i64c(cap), arg);
            return B.CreateLoad(st, tmp, "str.arg.val");
        }
    }
    // Turbo Tier 5, Cluster A item 7: a descendant OBJECT VALUE passed
    // where an ANCESTOR-typed value parameter is expected (Sema::
    // isAssignCompatible's own Object case, SemaExpr.cpp, now allows this
    // -- 'D: TDog; procedure ByVal(A: TAnimal); ByVal(D);').  Loading the
    // whole descendant struct and handing it to a callee declared over the
    // (smaller, different-shaped) ancestor struct is an LLVM IR verifier
    // failure ("Call parameter type does not match function signature"),
    // not merely a wasted copy -- the two are genuinely different LLVM
    // types.  Real object "value slicing" (confirmed against a local fpc
    // -Mtp build, cov1.pas's own 'A := D;') keeps only the ancestor's own
    // sub-object, which layoutOfObject already places at element 0 of
    // every descendant's own struct (CGTypes.cpp's own comment) -- so the
    // fix is the SAME GEP-through-element-0 walk selfFieldPtr/
    // objectFieldPtr's own ancestor recursion already uses for a single
    // FIELD, just stopping as soon as the STRUCT TYPE itself matches
    // paramTy instead of continuing on to a named field.
    if (paramTy && paramTy->isStructTy() && arg.ResolvedType
            && arg.ResolvedType->Kind == TypeKind::Object
            && Types.llvmTypeOfSemaType(*arg.ResolvedType) != paramTy) {
        auto* addr = EmitLValue(arg);
        if (!addr) codegenICE("an object value with no address to narrow");
        auto* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 0);
        llvm::Type* curTy = Types.llvmTypeOfSemaType(*arg.ResolvedType);
        const Type* curSemaTy = arg.ResolvedType.get();
        // Walk up Parent/element-0 in lockstep until the struct type
        // matches what the callee declared, or there is nowhere left to
        // go (Sema already proved paramTy IS some ancestor's own struct
        // type, via isAssignCompatible's objectIsOrDescendsFrom, so this
        // is guaranteed to terminate at or before the root).
        while (curTy != paramTy && curSemaTy && curSemaTy->Parent) {
            addr = B.CreateGEP(curTy, addr, {zero, zero}, "obj.narrow");
            curSemaTy = curSemaTy->Parent.get();
            curTy = Types.llvmTypeOfSemaType(*curSemaTy);
        }
        if (curTy != paramTy)
            codegenICE("object value-parameter narrowing reached '"
                       + (curSemaTy ? curSemaTy->Name : std::string("?"))
                       + "' without matching the callee's own struct type");
        return B.CreateLoad(paramTy, addr, "obj.narrowed");
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
        // arg's own Sema-resolved signedness, not a guess from v's LLVM
        // width: a signed narrow (ShortInt) or unsigned wide (Word/
        // Cardinal) Turbo-ordinal actual passed by value used to widen with
        // the pre-ladder heuristic here, so `s: ShortInt; s := -5;
        // show(s)` for `procedure show(x: LongInt)` printed x=251 instead
        // of -5 (issue #177).
        v = CoerceToType(v, paramTy, exprIsSigned(arg));
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
    // A char value converts too, and needs its own case: EmitExpr returns it
    // as a bare i8, not a pointer, so it is not among the "everything else"
    // the comment below is about.  EP §6.1.7 makes a single-character string
    // literal -- the 'x' in `update(f, 'x')` -- type char rather than
    // string(1), which is exactly the shape reset/rewrite/extend/update's
    // optional file-name argument takes when the name is one character long.
    // Before this, that i8 reached plang_update (etc.) unconverted, against
    // a `const char *` parameter LLVM's verifier rejects a scalar for.
    // Issue #296.
    if (e.ResolvedType && e.ResolvedType->Kind == TypeKind::Char) {
        auto* i8Ty = llvm::Type::getInt8Ty(Ctx);
        auto* buf  = CreateEntryAlloca(llvm::ArrayType::get(i8Ty, 2), "str.cstr.ch");
        B.CreateStore(EmitExpr(e), buf);
        auto* nulAt = B.CreateInBoundsGEP(i8Ty, buf, i64c(1), "str.cstr.ch.nul");
        B.CreateStore(llvm::ConstantInt::get(i8Ty, 0), nulAt);
        return buf;
    }
    // Turbo string[N] (ShortString): a ONE-byte length prefix followed by
    // its data, not null-terminated at all (plang_sstr.cpp's own layout
    // comment) -- so, like string(n) just below, it needs converting rather
    // than falling to the "already a char *" default this function's own
    // comment describes.  TP's own Assign(f, name) (CGProcCall.cpp) is the
    // first caller that can reach this with a ShortString argument -- every
    // OTHER user of this function (EP's reset/rewrite/extend/update) can
    // never see a ShortString, since that type does not exist outside
    // -std=turbo -- but the check is unconditional here anyway, the same
    // way the VarStr branch below is: correctness by construction, not by
    // trusting every call site to know which dialect it is in.
    if (ExprIsShortStr(e)) {
        auto* addr = emitStrAddr(e);
        if (!addr) codegenICE("a ShortString value with no address");
        auto* i8Ty  = llvm::Type::getInt8Ty(Ctx);
        auto* len   = B.CreateZExt(Strings.sstrLoadLen(addr), I64Ty, "sstr.cstr.len");
        auto* data  = Strings.sstrDataPtr(addr);
        const int64_t cap = ExprShortStrCap(e);
        auto* buf = CreateEntryAlloca(
            llvm::ArrayType::get(i8Ty, static_cast<uint64_t>(cap) + 1), "sstr.cstr");
        B.CreateMemCpy(buf, llvm::MaybeAlign(), data, llvm::MaybeAlign(), len);
        auto* nulAt = B.CreateInBoundsGEP(i8Ty, buf, len, "sstr.cstr.nul");
        B.CreateStore(llvm::ConstantInt::get(i8Ty, 0), nulAt);
        return buf;
    }
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
        // §6.4.3.2 requires the lengths to match, and Sema settles that when
        // it knows the capacity -- but a capacity that matches n is not the
        // same thing as a LENGTH that does.  A string(n)'s length is a
        // mutable run-time field independent of its capacity: `s := 'hi'` on
        // a string(5) leaves it at length 2 despite room for 5, and copying n
        // bytes out of it regardless read whatever stale bytes happened to
        // follow in the buffer.  This used to run only when a discriminant
        // left the capacity itself unknown to Sema (ExtentVaries); that is
        // only the case Sema could not even ATTEMPT its compile-time check
        // for, not the only case where a matching capacity can still have a
        // shorter run-time length, so a fixed-capacity string(5) reassigned
        // to something shorter went unchecked and leaked the earlier value's
        // trailing bytes.  Checked here instead, against the length the
        // string actually has, unconditionally.
        auto* len = Strings.strLoadLen(from);
        auto* bad = B.CreateICmpNE(len, i64c(n), "charstr.len.bad");
        RangeGuards.emitGuard(bad, "charstrlen", [&] {
            B.CreateCall(
                RtFns.getExternFnN("plang_err_str_length",
                                   llvm::Type::getVoidTy(Ctx), {I64Ty, I64Ty}),
                {len, i64c(n)});
        });
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

// Turbo string[N]'s own sibling of emitStrStore just above.  Same dispatch
// shape (literal / same-dialect-string / char-or-cstr fallback), but every
// runtime call is a plang_sstr_* one -- and, critically, TRUNCATING rather
// than erroring: unlike EP's string(N), ISO 10206 §6.9.2.2's capacity-error
// rule was never Turbo's, so a source longer than \p capDst is silently cut
// down rather than reported (see checkStringCapacity's own ShortString early
// return, SemaStmt.cpp, and plang_sstr_assign/plang_sstr.cpp).  No
// ExprIsCharStr arm: Sema's isAssignCompatible has no ShortString←
// fixed-string-type rule (out of this item's scope), so that combination
// never reaches here.
void StringCallMarshalling::emitSstrStore(llvm::Value* dst, llvm::Value* capDst,
                                           const ExprNode& src) {
    // A literal is already a run of bytes; going through emitExpr would
    // build a string temporary first and copy it twice -- the identical
    // shortcut emitStrStore's own literal arm takes.
    if (auto* sl = llvm::dyn_cast<StringLitExpr>(&src)) {
        Strings.emitSstrFromBytes(dst, capDst, Strings.internStrPtr(sl->Value),
                                  i64c(static_cast<int64_t>(sl->Value.size())));
        return;
    }
    // A ShortString source keeps its own one-byte length prefix; assigning
    // it is a truncating copy (plang_sstr_assign), never the error EP's
    // plang_str_assign raises for a source that doesn't fit.  emitStrAddr,
    // not EmitLValue: src may be a computed ShortString VALUE with no
    // storage of its own to take the lvalue of -- a concatenation result,
    // most concretely (`s := s + t` reaches here with src = the `s + t`
    // BinaryExpr, which emitLValue does not and cannot handle) -- and
    // emitStrAddr already knows to fall back to EmitExpr for exactly that
    // case, the same dispatch emitStrStore's own ExprIsVarStr arm relies on
    // via Schema.strAddrAndCap.
    if (ExprIsShortStr(src)) {
        auto* sp = emitStrAddr(src);
        if (!sp) codegenICE("ShortString assignment from an unlowerable expression");
        Strings.emitSstrAssign(dst, capDst, sp, i64c(ExprShortStrCap(src)));
        return;
    }
    auto* rhs = EmitExpr(src); // char → i8; other → ptr (cstr)
    if (!rhs) codegenICE("ShortString assignment from an unlowerable expression");
    if (rhs->getType()->isIntegerTy(8))
        Strings.emitSstrFromChar(dst, capDst, rhs);
    else
        Strings.emitSstrFromCStr(dst, capDst, rhs);
}
