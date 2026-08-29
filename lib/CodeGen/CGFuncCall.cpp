#include "CGFuncCall.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Sema/Sema.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"

using namespace plang;

llvm::Value* CGFuncCall::emitCallExpr(const CallExpr& e) {
    // ISO §6.2.2.10: a required function identifier may be redeclared, and
    // then it denotes what the program declared and not the required one.  The
    // chain below dispatches on spelling alone, so without this a program that
    // declares its own `abs` calls the required one and never reaches the
    // declared body.  Sema resolved the name in the scope it was written in
    // and is the only thing that knows which won.  This also settles a
    // functional parameter named after a required function, which the check at
    // the head of emitUserFuncCall would otherwise not be reached to make.
    if (e.ResolvedBuiltin == BuiltinID::None) return emitUserFuncCall(e);
    if (auto* v = emitBuiltinCall(e.Name, e.Args, e.Loc)) return v;
    return emitUserFuncCall(e);
}

llvm::Value* CGFuncCall::emitBuiltinCall(const std::string& Name,
        std::span<const std::unique_ptr<ExprNode>> Args, SourceLocation Loc) {
    std::string lo = toLower(Name);

    // ---- Math built-ins routed through plang_math.c ----
    if (lo == "sqrt" || lo == "sin" || lo == "cos" || lo == "exp"
        || lo == "ln"  || lo == "arctan") {
        auto* arg = EmitExpr(*Args[0]);
        // EP §6.7.6.2: dispatch to complex variant when argument is complex.
        if (arg->getType() == Complex.complexTy()) {
            // e.g. "sqrt" → "plang_csqrt_out"
            std::string cname = "plang_c" + lo + "_out";
            return Complex.callComplexUnary(cname, arg);
        }
        auto* darg = ToDouble(arg);
        return B.CreateCall(RtFns.getRTMathRR("plang_" + lo), {darg}, lo);
    }
    if (lo == "abs") {
        auto* v = EmitExpr(*Args[0]);
        // EP §6.7.6.2: abs(complex) → real = sqrt(re² + im²)
        if (v->getType() == Complex.complexTy()) {
            auto* re = B.CreateExtractValue(v, 0, "z.re");
            auto* im = B.CreateExtractValue(v, 1, "z.im");
            // plang_abs_cplx(re, im) → double
            auto* fn = RtFns.getExternFnN("plang_abs_cplx", DblTy, {DblTy, DblTy});
            return B.CreateCall(fn, {re, im}, "abs_cplx");
        }
        // isFloatingPointTy, not isDoubleTy: Turbo's Single (float) needs
        // the same real-valued plang_abs_real double already gets -- ToDouble
        // promotes it first, the same "promote rather than duplicate" shim
        // used throughout for Single (see e.g. BuiltinIO::emitWriteValue).
        // Without this, a Single argument fell through to the INTEGER path
        // below, which called ToI64 on a float value.
        if (v->getType()->isFloatingPointTy())
            return B.CreateCall(RtFns.getRTMathRR("plang_abs_real"), {ToDouble(v)}, "abs");
        return B.CreateCall(RtFns.getRTMathII("plang_abs_int"), {ToI64(v)}, "abs");
    }
    if (lo == "sqr") {
        auto* v = EmitExpr(*Args[0]);
        // EP §6.7.6.2: sqr(complex) → complex = z * z
        if (v->getType() == Complex.complexTy())
            return Complex.emitComplexMul(v, v);
        // See abs's identical comment just above.
        if (v->getType()->isFloatingPointTy())
            return B.CreateCall(RtFns.getRTMathRR("plang_sqr_real"), {ToDouble(v)}, "sqr");
        return B.CreateCall(RtFns.getRTMathII("plang_sqr_int"), {ToI64(v)}, "sqr");
    }
    // EP §6.7.6.3: cmplx(x, y) constructor
    if (lo == "cmplx") {
        auto* re = ToDouble(EmitExpr(*Args[0]));
        auto* im = ToDouble(EmitExpr(*Args[1]));
        return Complex.makeComplex(re, im);
    }
    // EP §6.7.6.3: polar(r, t) = r*cos(t) + i*r*sin(t)
    if (lo == "polar") {
        auto* r = ToDouble(EmitExpr(*Args[0]));
        auto* t = ToDouble(EmitExpr(*Args[1]));
        auto* cosTh = B.CreateCall(RtFns.getRTMathRR("plang_cos"), {t}, "cos_t");
        auto* sinTh = B.CreateCall(RtFns.getRTMathRR("plang_sin"), {t}, "sin_t");
        auto* re = B.CreateFMul(r, cosTh, "polar_re");
        auto* im = B.CreateFMul(r, sinTh, "polar_im");
        return Complex.makeComplex(re, im);
    }
    // EP §6.7.6.2: re(z), im(z), arg(z)
    if (lo == "re") {
        auto* z = EmitExpr(*Args[0]);
        if (z->getType() == Complex.complexTy())
            return B.CreateExtractValue(z, 0, "re");
        return ToDouble(z);
    }
    if (lo == "im") {
        auto* z = EmitExpr(*Args[0]);
        if (z->getType() == Complex.complexTy())
            return B.CreateExtractValue(z, 1, "im");
        return llvm::ConstantFP::get(DblTy, 0.0);
    }
    if (lo == "arg") {
        auto* z = EmitExpr(*Args[0]);
        if (z->getType() == Complex.complexTy()) {
            auto* re = B.CreateExtractValue(z, 0, "z.re");
            auto* im = B.CreateExtractValue(z, 1, "z.im");
            auto* fn = RtFns.getExternFnN("plang_arg", DblTy, {DblTy, DblTy});
            return B.CreateCall(fn, {re, im}, "arg");
        }
        // Real/int: arg is 0 for positive, π for negative
        auto* fn = RtFns.getExternFnN("plang_arg", DblTy, {DblTy, DblTy});
        return B.CreateCall(fn, {ToDouble(z),
            llvm::ConstantFP::get(DblTy, 0.0)}, "arg");
    }
    if (lo == "trunc") {
        auto* arg = ToDouble(EmitExpr(*Args[0]));
        return B.CreateCall(RtFns.getRTMathRI("plang_trunc"), {arg}, "trunc");
    }
    if (lo == "round") {
        auto* arg = ToDouble(EmitExpr(*Args[0]));
        return B.CreateCall(RtFns.getRTMathRI("plang_round"), {arg}, "round");
    }
    // ---- Boolean file-status built-ins ----
    if (lo == "eof") {
        if (!Args.empty() && FileVars.isFileVar(*Args[0])) {
            auto* fp  = FileVars.fileVarPtr(*Args[0]);
            auto* raw = B.CreateCall(
                RtFns.getExternFnN("plang_eof_file", I8Ty, {PtrTy}), {fp}, "eof.raw");
            return EnsureI1(raw);
        }
        auto* raw = B.CreateCall(
            RtFns.getRuntimeBoolFn("plang_eof_stdin"), {}, "eof.raw");
        return EnsureI1(raw);
    }
    if (lo == "eoln") {
        if (!Args.empty() && FileVars.isFileVar(*Args[0])) {
            auto* fp  = FileVars.fileVarPtr(*Args[0]);
            auto* raw = B.CreateCall(
                RtFns.getExternFnN("plang_eoln_file", I8Ty, {PtrTy}), {fp}, "eoln.raw");
            return EnsureI1(raw);
        }
        auto* raw = B.CreateCall(
            RtFns.getRuntimeBoolFn("plang_eoln_stdin"), {}, "eoln.raw");
        return EnsureI1(raw);
    }
    // EP §6.7.6.8: binding(f) → BindingType record
    if (lo == "binding" && !Args.empty()) {
        auto* fp  = FileVars.fileVarPtr(*Args[0]);
        auto* out = CreateEntryAlloca(Types.bindingStructType(), "binding.out");
        B.CreateStore(llvm::Constant::getNullValue(Types.bindingStructType()), out);
        auto* fn  = RtFns.getExternFnN("plang_binding",
                                  llvm::Type::getVoidTy(Ctx), {PtrTy, PtrTy});
        B.CreateCall(fn, {fp, out});
        return B.CreateLoad(Types.bindingStructType(), out, "binding");
    }

    // EP §6.7.6.9: date(t) / time(t) — format TimeStamp to string
    if ((lo == "date" || lo == "time") && !Args.empty()) {
        auto* tPtr = EmitLValue(*Args[0]);
        // 'time' maps to plang_time_ts to avoid clashing with the POSIX time() symbol.
        std::string fnName = (lo == "time") ? "plang_time_ts" : "plang_date";
        auto* fn = RtFns.getExternFnN(fnName, PtrTy, {PtrTy});
        return B.CreateCall(fn, {tPtr}, lo);
    }

    // EP §6.7.6.6: position(f) / lastposition(f).  Both report a value of
    // the file's declared INDEX TYPE -- "position(f) = succ(a, ...)", a
    // being the index type's smallest value -- not a 0-based component
    // count, so a `file[1..5]` fully written reports 4, not 3.
    if ((lo == "position" || lo == "lastposition") && !Args.empty()) {
        // Sema now refuses a non-file argument here (issue #417), the same
        // way it already refuses one for eof/eoln above -- this guard is
        // defense in depth, unreachable for a program that passed Sema.
        // Without it, FileVarHelpers::fileVarPtr's IdentExpr fast path
        // returns the wrong-typed variable's own storage address with no
        // check at all, and that reached plang_position/plang_lastposition
        // as a PascalFile*, segfaulting with no diagnostic.
        if (!FileVars.isFileVar(*Args[0]))
            return llvm::ConstantInt::get(I64Ty, 0);
        auto* fp  = FileVars.fileVarPtr(*Args[0]);
        int64_t esz = FileVars.getFileElemSize(*Args[0]);
        int64_t ilo = FileVars.getFileIndexLow(*Args[0]);
        auto* fn  = RtFns.getExternFnN("plang_" + lo, I64Ty, {PtrTy, I64Ty, I64Ty});
        return B.CreateCall(fn, {fp, llvm::ConstantInt::get(I64Ty, esz),
                                       llvm::ConstantInt::get(I64Ty, ilo)}, lo);
    }
    // EP §6.7.6.5: empty(f)
    if (lo == "empty" && !Args.empty()) {
        // Same defense-in-depth guard as position/lastposition just above
        // (issue #417).
        if (!FileVars.isFileVar(*Args[0]))
            return llvm::ConstantInt::getFalse(Ctx);
        auto* fp  = FileVars.fileVarPtr(*Args[0]);
        int64_t esz = FileVars.getFileElemSize(*Args[0]);
        auto* fn  = RtFns.getExternFnN("plang_empty", I8Ty, {PtrTy, I64Ty});
        auto* raw = B.CreateCall(fn, {fp, llvm::ConstantInt::get(I64Ty, esz)}, "empty.raw");
        return B.CreateICmpNE(raw, llvm::ConstantInt::get(I8Ty, 0), "empty");
    }

    // ---- Ordinal built-ins — simple enough to keep inline ----
    if (lo == "ord") {
        auto* v = EmitExpr(*Args[0]);
        if (v->getType()->isIntegerTy(64)) return v;
        return B.CreateZExt(v, I64Ty, "ord");
    }
    if (lo == "chr") {
        // ISO §6.6.6.4: chr(x) yields the value whose ordinal number is x,
        // and "it shall be an error if this value does not exist" -- checked
        // nowhere, so this went straight to CreateTrunc and chr(256) wrapped
        // silently into chr(0) (and chr(-1) into chr(255)) instead of being
        // reported.  Same convention as succ/pred below: guard the wide
        // value before it is narrowed.  Char's range is the fixed 0..255
        // ordinalRange gives it, not a lookup on the argument's type -- the
        // argument is whatever ordinal expression was passed, not a char.
        auto* v = ToI64(EmitExpr(*Args[0]));
        RangeGuards.emitRangeCheck(v, 0, 255, /*isIndex=*/false, Loc);
        return B.CreateTrunc(v, I8Ty, "chr");
    }
    if (lo == "odd") {
        auto* v   = ToI64(EmitExpr(*Args[0]));
        auto* bit = B.CreateAnd(v, llvm::ConstantInt::get(I64Ty, 1), "odd.bit");
        return B.CreateICmpNE(bit, llvm::ConstantInt::get(I64Ty, 0), "odd");
    }
    if (lo == "succ" || lo == "pred") {
        auto* arg = EmitExpr(*Args[0]);
        auto* v = ToI64(arg);
        auto* k = Args.size() > 1
            ? ToI64(EmitExpr(*Args[1]))
            : llvm::ConstantInt::get(I64Ty, 1);
        auto* r = lo == "succ" ? B.CreateAdd(v, k, "succ")
                               : B.CreateSub(v, k, "pred");
        // ISO §6.7.6.4: "The function shall yield a value whose ordinal
        // number is ord(x)+k, if such a value exists.  It shall be an error
        // if such a value does not exist" -- checked nowhere, so
        // succ(blue) one past the last value of a 3-member enum silently
        // wrapped into 3, an ordinal no value of the type has.  integer is
        // deliberately excluded (ordinalRange's own rule): it has no bounded
        // range to have walked off the end of.
        if (const auto& argTy = Args[0]->ResolvedType; argTy && !argTy->isError())
            if (auto range = ordinalRange(*argTy))
                RangeGuards.emitRangeCheck(r, range->first, range->second, /*isIndex=*/false,
                              Loc);
        // ISO §6.6.6.4: the result is of the argument's type.  The arithmetic
        // is done wide, so it has to come back in the width that type is held
        // in, or a boolean result is an i64 that write puts out as 1 and 0.
        return arg && arg->getType() != I64Ty
            ? B.CreateZExtOrTrunc(r, arg->getType(), lo)
            : r;
    }
    if (lo == "card") {
        // Cardinality of a set (bit population count).
        auto* v   = Sets.toSetWidth(EmitExpr(*Args[0]));
        auto* fn  = llvm::Intrinsic::getOrInsertDeclaration(
                        &Mod, llvm::Intrinsic::ctpop, {Sets.setTy()});
        auto* n   = B.CreateCall(fn, {v}, "card");
        return B.CreateZExtOrTrunc(n, I64Ty, "card.i64");
    }
    // TP-only: Assigned(p) -- p is a pointer or a procedural value (Sema's
    // dedicated arm already refused anything else), both of which lower to
    // a flat `ptr`, so this is just a not-nil comparison.
    if (lo == "assigned") {
        auto* v = EmitExpr(*Args[0]);
        return B.CreateICmpNE(v, llvm::ConstantPointerNull::get(PtrTy), "assigned");
    }

    // TP-only: SizeOf(T)/High(T)/Low(T).  Args[0]->ResolvedType is the type
    // this call answers about -- Sema::resolveTypeArgOrValue set it either
    // directly (Args[0] named a type) or via the ordinary checkExpr path
    // (Args[0] was a value expression) -- so nothing here needs to re-derive
    // "is this a type name or a value" a second time, and, since a Sema
    // type's size/range never depends on anything computed at run time,
    // EmitExpr(*Args[0]) is never called at all, for EITHER shape: a
    // type-name argument has no value to evaluate in the first place, and a
    // VALUE-expression argument (SizeOf(x), High(arr)) is only ever asked
    // for its static TYPE -- exactly the same unevaluated-operand rule C's
    // own sizeof follows.  This matters beyond just avoiding wasted work:
    // were Args[0] instead emitted, `SizeOf(arr[F])` or `High(SomeFunc())`
    // would run F/SomeFunc's side effects at run time for a question this
    // compiler (like every other Pascal/C implementation) answers entirely
    // at compile time.
    if (lo == "sizeof") {
        const auto& T = Args[0]->ResolvedType;
        const auto Sz = T ? Sema::byteSizeOf(*T) : std::nullopt;
        if (!Sz)
            codegenICE("SizeOf reached codegen with no resolvable size for '"
                       + (T ? T->Name : std::string("?")) + "'");
        return llvm::ConstantInt::get(I64Ty, *Sz);
    }
    if (lo == "high" || lo == "low") {
        std::shared_ptr<Type> RangeTy = Args[0]->ResolvedType;
        if (RangeTy && RangeTy->Kind == TypeKind::Array) RangeTy = RangeTy->IndexType;
        const auto Range = RangeTy ? ordinalRange(*RangeTy) : std::nullopt;
        if (!Range)
            codegenICE("High/Low reached codegen with no ordinal range for '"
                       + (RangeTy ? RangeTy->Name : std::string("?")) + "'");
        const int64_t V = (lo == "high") ? Range->second : Range->first;
        auto* resTy = llvm::cast<llvm::IntegerType>(Types.llvmTypeOfSemaType(*RangeTy));
        return llvm::ConstantInt::get(resTy, static_cast<uint64_t>(V), /*isSigned=*/true);
    }
    // FPC's size-aware Hi/Lo/Swap -- a DELIBERATE divergence from literal
    // Turbo Pascal 7, whose Hi/Lo/Swap only ever worked on a 16-bit value:
    // real TP7 code that Inc/Dec'd a 32-bit value and then called Hi/Lo/Swap
    // on it got the OLD 16-bit-only answer, and this compiler -- following
    // fpc -Mtp's own field practice, per this project's own "match field
    // compilers on ambiguity" milestone decision -- gives the NEW,
    // width-aware one instead.  See Sema::checkCallExpr's identical note.
    //
    // Sema has already required a real Integer-kind argument at least 16
    // bits wide, so EmitExpr's own LLVM type IS the argument's own width
    // (i16/i32/i64) with nothing further to coerce -- Turbo's Integer kind
    // is lowered at its own declared Width throughout codegen, unlike
    // ISO/EP's single always-64-bit integer, so no ToI64 round-trip is
    // needed (or wanted: it would have to be undone again below).
    if (lo == "hi" || lo == "lo" || lo == "swap") {
        auto* v    = EmitExpr(*Args[0]);
        auto* wTy  = llvm::cast<llvm::IntegerType>(v->getType());
        const unsigned Half = wTy->getBitWidth() / 2;
        if (lo == "swap") {
            // A rotate by half the width: at 16 bits this is a byte swap,
            // at 32 bits a word swap, at 64 bits a doubleword swap -- one
            // formula covers every width the sized-integer ladder has,
            // rather than a width-keyed byte-swap/word-swap branch.
            auto* half = llvm::ConstantInt::get(wTy, Half);
            auto* lo_  = B.CreateShl (v, half, "swap.lo");
            auto* hi_  = B.CreateLShr(v, half, "swap.hi");
            return B.CreateOr(lo_, hi_, "swap");
        }
        auto* halfTy = llvm::IntegerType::get(Ctx, Half);
        llvm::Value* part = (lo == "hi")
            ? B.CreateLShr(v, llvm::ConstantInt::get(wTy, Half), "hi.shr") : v;
        return B.CreateTrunc(part, halfTy, lo);
    }

    // ---- EP string functions (§6.7.6.7) ----
    // Return (ptr, cap) for a string argument using Sema-annotated type.
    auto getStrArgPtr = [&](int idx) -> std::pair<llvm::Value*, llvm::Value*> {
        if (Args.size() <= (size_t)idx) return {nullptr, nullptr};
        const auto& arg = *Args[idx];
        if (ExprIsVarStr(arg)) return Schema.strAddrAndCap(arg);
        // ISO §6.4.3.2's other string shape — a packed array[1..n] of char —
        // is the sibling comparison already widens for (exprIsStringLike,
        // above); length/substr/trim/index only asked exprIsVarStr and fell
        // through to a raw-strlen fallback that read the whole array as an
        // i64-sized value and passed it where a pointer was wanted, an LLVM
        // IR verifier failure, or (for substr/trim/index) to a runtime symbol
        // codegen never emits.
        if (ExprIsCharStr(arg))
            return {StrCall.emitCharStrAsStr(arg), i64c(ExprCharStrLen(arg))};
        // String literal — create a temp VarString.
        if (auto* sl = llvm::dyn_cast<StringLitExpr>(&arg)) {
            int64_t cap = (int64_t)sl->Value.size();
            auto* tmp = CreateEntryAlloca(Types.strStructType(cap), "str.arg");
            Strings.emitStrFromBytes(tmp, i64c(cap), Strings.internStrPtr(sl->Value),
                                     i64c(cap));
            return {tmp, i64c(cap)};
        }
        // A bare char (a variable, not a literal): EP §6.7.6.7 accepts
        // char-type as readily as a string-type for EQ/LT/GT/NE/LE/GE, and
        // this had no case for it at all -- Sema's own check (SemaExpr.cpp)
        // accepts a char argument, and nothing here built the temporary
        // VarString it needs to be compared as, so the call reached nothing
        // and fell through to a link failure exactly like an outright
        // rejected argument would have.
        if (arg.ResolvedType && arg.ResolvedType->Kind == TypeKind::Char) {
            auto* v   = EmitExpr(arg);
            auto* tmp = CreateEntryAlloca(Types.strStructType(1), "str.arg.chr");
            if (v) Strings.emitStrFromChar(tmp, i64c(1), v);
            return {tmp, i64c(1)};
        }
        return {nullptr, nullptr};
    };
    // For sizing a temporary, which needs a constant; see exprStrCapStatic.
    auto strArgCapStatic = [&](int idx) -> int64_t {
        if (Args.size() <= (size_t)idx) return 0;
        const auto& arg = *Args[idx];
        if (ExprIsVarStr(arg)) return ExprStrCapStatic(arg);
        if (ExprIsCharStr(arg)) return ExprCharStrLen(arg);
        if (auto* sl = llvm::dyn_cast<StringLitExpr>(&arg))
            return (int64_t)sl->Value.size();
        if (arg.ResolvedType && arg.ResolvedType->Kind == TypeKind::Char) return 1;
        return 0;
    };
    /// A result temporary as wide as the argument it is derived from.  Where a
    /// discriminant fixes that argument's capacity the width is not a constant,
    /// and exprStrCapStatic's 255 is the answer for a capacity nobody knows --
    /// so sizing by it cut substr and trim of a 400-capacity string to 255.
    auto strResultTemp = [&](int idx, llvm::Value* capV, const char* name)
            -> std::pair<llvm::Value*, llvm::Value*> {
        if (const auto& arg = *Args[idx];
                ExprIsVarStr(arg) && arg.ResolvedType->ExtentVaries)
            return {CreateDynStrAlloca(capV, name), capV};
        const int64_t c = strArgCapStatic(idx);
        return {CreateEntryAlloca(Types.strStructType(c), name), i64c(c)};
    };
    if (lo == "length") {
        auto [ptr, cap] = getStrArgPtr(0);
        if (ptr) {
            auto* fn = Strings.getStrFn("plang_str_length", I64Ty, {PtrTy, I64Ty});
            return B.CreateCall(fn, {ptr, cap}, "length");
        }
        // Fallback: strlen on a char*
        auto* s  = EmitExpr(*Args[0]);
        auto* fn = RtFns.getExternFnN("strlen", I64Ty, {PtrTy});
        return B.CreateCall(fn, {s}, "length");
    }
    if (lo == "index") {
        auto [sp, sc] = getStrArgPtr(0);
        auto [pp, pc] = getStrArgPtr(1);
        if (sp && pp) {
            auto* fn = Strings.getStrFn("plang_str_index", I64Ty,
                {PtrTy, I64Ty, PtrTy, I64Ty});
            return B.CreateCall(fn, {sp, sc, pp, pc}, "index");
        }
    }
    if (lo == "substr") {
        auto [sp, sc] = getStrArgPtr(0);
        if (sp) {
            // EP §6.7.5.4: the third argument is how many characters to take,
            // not where to stop.  Omitting it means the rest of the string.
            auto* i = ToI64(EmitExpr(*Args[1]));
            auto* n = Args.size() > 2
                ? ToI64(EmitExpr(*Args[2]))
                : B.CreateAdd(
                      B.CreateSub(Strings.strLoadLen(sp), i, "substr.rest"),
                      llvm::ConstantInt::get(I64Ty, 1), "substr.len");
            auto [resPtr, resCapV] = strResultTemp(0, sc, "substr.res");
            auto* fn     = Strings.getStrFn("plang_str_substr",
                llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty, PtrTy, I64Ty, I64Ty, I64Ty});
            B.CreateCall(fn, {resPtr, resCapV, sp, sc, i, n});
            return resPtr;
        }
    }
    if (lo == "trim") {
        auto [sp, sc] = getStrArgPtr(0);
        if (sp) {
            auto [resPtr, resCapV] = strResultTemp(0, sc, "trim.res");
            auto* fn     = Strings.getStrFn("plang_str_trim",
                llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty, PtrTy, I64Ty});
            B.CreateCall(fn, {resPtr, resCapV, sp, sc});
            return resPtr;
        }
    }
    // EP string comparison functions: EQ NE LT GT LE GE
    {
        static const std::unordered_map<std::string,const char*> strCmpFns = {
            {"eq","plang_str_eq"},{"ne","plang_str_ne"},{"lt","plang_str_lt"},
            {"gt","plang_str_gt"},{"le","plang_str_le"},{"ge","plang_str_ge"},
        };
        auto it = strCmpFns.find(lo);
        if (it != strCmpFns.end() && Args.size() >= 2) {
            auto [lp, lc] = getStrArgPtr(0);
            auto [rp, rc] = getStrArgPtr(1);
            if (lp && rp) {
                auto* fn  = Strings.getStrFn(it->second, I8Ty, {PtrTy, I64Ty, PtrTy, I64Ty});
                auto* raw = B.CreateCall(fn, {lp, lc, rp, rc}, lo);
                return EnsureI1(raw);
            }
        }
    }

    // Every Func-kind row in Builtins.def has a named arm above; reaching
    // here means ResolvedBuiltin was set to a spelling none of them matched,
    // which should not happen.  nullptr, not codegenICE: emitCallExpr's own
    // caller still has emitUserFuncCall(e) to fall back to (the ORIGINAL
    // behaviour, preserved exactly), and CGProcCall's caller -- with no
    // CallExpr of its own to fall back through -- reports the ICE instead.
    return nullptr;
}

llvm::Value* CGFuncCall::emitUserFuncCall(const CallExpr& e) {
    // ISO §6.6.3.1: a functional parameter is called through the pair it
    // arrived as, so there is no name to resolve.
    if (auto* pve = SymTab.findVar(e.Name); pve && pve->isProcParam)
        return ClosureAbi.emitProcParamCall(*pve, e.Args);

    // Turbo procedural VALUES: 'f(...)' where f is an ordinary VARIABLE of
    // procedural type -- an indirect call through whatever routine f
    // currently holds, its flat pointer loaded and called through directly
    // (no frame: see VarEntry::isProcVar's own comment).
    if (auto* pve = SymTab.findVar(e.Name); pve && pve->isProcVar)
        return ClosureAbi.emitProcVarCall(*pve, e.Args);

    // User-defined function — walk the nesting hierarchy.
    std::string mangledName = Linkage.findMangledProc(e.Name);
    auto* callee = Mod.getFunction(mangledName);
    if (!callee) {
        // The function is not defined in this compilation unit; it must come
        // from a separately compiled module.  Create an external declaration
        // using the LLVM types derived from the Sema-resolved call-site types.
        llvm::Type* retLLVMTy = llvm::Type::getVoidTy(Ctx);
        if (e.ResolvedType && !e.ResolvedType->isError()) {
            retLLVMTy = Types.llvmTypeOfSemaType(*e.ResolvedType);
        }
        std::vector<llvm::Type*> paramTys;
        for (const auto& Arg : e.Args) {
            if (Arg && Arg->ResolvedType && !Arg->ResolvedType->isError())
                paramTys.push_back(Types.llvmTypeOfSemaType(*Arg->ResolvedType));
            else
                paramTys.push_back(I64Ty); // safe fallback
        }
        auto* fnTy = llvm::FunctionType::get(retLLVMTy, paramTys, false);
        callee = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage,
                                        mangledName, &Mod);
    }

    std::vector<llvm::Value*> args;

    // Nested function call: build the callee's static-link frame from its
    // recorded outer variable list, so the slot order matches the definition.
    if (auto* frame = BuildStaticLinkFrame(mangledName)) args.push_back(frame);

    // EP §6.7.3.7: look up conformant param dimensions for this callee.
    // ConformantDimsOf(mangledName, astArgIdx) is the dimension count for the
    // i-th AST argument position.  0 means the param is not conformant.
    size_t pi = args.size();
    for (size_t astArgIdx = 0; astArgIdx < e.Args.size(); ++astArgIdx) {
        const auto& arg = e.Args[astArgIdx];

        // ISO §6.6.3.1: procedural param — entry point plus its frame.
        if (const auto* pt = ProcParamArg(mangledName, astArgIdx)) {
            ClosureAbi.pushProcParamArgs(args, *arg, *pt);
            pi = args.size();
            continue;
        }

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
    auto* ret = B.CreateCall(callee, args, "call");
    // A string result comes back as the whole { length, bytes } struct, but
    // every consumer of a string expression expects its address.  Spill the
    // returned value so the result of f reads like any other string.
    if (ExprIsVarStr(e) && ret->getType()->isStructTy()) {
        auto* tmp = CreateEntryAlloca(ret->getType(), "str.ret");
        B.CreateStore(ret, tmp);
        return tmp;
    }
    // Turbo string[N]: a ShortString RESULT comes back the same way -- the
    // whole packed <{i8,[N]}> struct by value -- and needs the identical
    // spill-to-a-temporary treatment so its consumers see an address the
    // way every other string expression does (see exprIsShortStr's own doc
    // comment, CodeGenImpl.h).  A SEPARATE branch rather than widening the
    // VarString check just above with an `||`: spilling any struct return
    // to an addressable temporary happens to be identical plumbing for
    // both dialects, but the two runtimes it feeds into downstream are not,
    // and this file's whole ShortString policy is to never let one
    // condition quietly serve both.
    if (ExprIsShortStr(e) && ret->getType()->isStructTy()) {
        auto* tmp = CreateEntryAlloca(ret->getType(), "sstr.ret");
        B.CreateStore(ret, tmp);
        return tmp;
    }
    return ret;
}
