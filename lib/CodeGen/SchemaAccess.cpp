#include "SchemaAccess.h"

#include <ranges>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"

using namespace plang;

llvm::Constant* SchemaAccess::i64c(int64_t v) const {
    return llvm::ConstantInt::get(I64Ty, static_cast<uint64_t>(v), true);
}

/// Bytes of discriminant header in front of a schema body.
///
/// One question, one answer: emitNewSchema lays this out and schemaRefOf skips
/// it, and they were each spelling `SchemaDiscs.size() * 8` for themselves.
/// That is also the wrong number -- it aligns the body to 8 whatever the body
/// needs, so a body wanting 16 sat misaligned and the aligned vector stores
/// llvm emits at -O1 and above faulted on it.
std::optional<SchemaAccess::SchemaRef>
SchemaAccess::schemaRefOf(const ExprNode& e) {
    // A formal parameter: the body pointer and the discriminants are arguments.
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e)) {
        if (const auto* ve = SymTab.findVar(id->Name); ve && ve->schemaTy)
            return SchemaRef{ve->schemaTy, ve->ptr, ve->schemaDiscs};
        return std::nullopt;
    }

    // p^: the discriminants sit in the header emitNewSchema wrote.
    if (auto* de = llvm::dyn_cast<DerefExpr>(&e)) {
        // Ask the POINTER what it points at, not the dereference what it
        // became: for a body that is not an array, `p^` reads as the body --
        // a string, a record -- so its own type no longer says "schema" and
        // the header in front of it would go unread.
        const plang::Type* T = nullptr;
        if (const auto& PT = de->Pointer->ResolvedType;
                PT && PT->Kind == TypeKind::Pointer && PT->PointeeType
                && PT->PointeeType->Kind == TypeKind::Schema)
            T = PT->PointeeType.get();
        if (!T) return std::nullopt;
        auto* base = EmitExpr(*de->Pointer);
        if (!base) codegenICE("pointer to schema '" + T->SchemaName
                              + "' has no lowerable value");
        // Recovering the discriminants dereferences p just as surely as reading
        // the body does.  Every other route to a p^ emits this check; this one
        // did not, so `p^[i]` through a nil schema pointer read the header at
        // address 0 and took the process down instead of raising.
        if (base->getType()->isPointerTy()) RangeGuards.emitNilCheck(base);
        SchemaRef ref;
        ref.semaTy = T;
        for (size_t i = 0; i < T->SchemaDiscs.size(); ++i) {
            auto* slot = B.CreateGEP(I64Ty, base,
                {llvm::ConstantInt::get(I64Ty, i)}, "sch.disc.ptr");
            ref.discs.push_back(B.CreateLoad(I64Ty, slot, "sch.disc"));
        }
        ref.data = B.CreateGEP(I8Ty, base,
            {llvm::ConstantInt::get(I64Ty,
                static_cast<int64_t>(SchemaLayout.schemaHeaderBytes(*T)))}, "sch.data");
        return ref;
    }

    return std::nullopt;
}

std::pair<llvm::Value*, std::vector<llvm::Value*>>
SchemaAccess::schemaActual(const ExprNode& arg, unsigned discCount) {
    // A schematic actual passes its own discriminants straight through.
    if (auto ref = schemaRefOf(arg)) {
        if (ref->discs.size() != discCount)
            codegenICE("schema argument carries " + llvm::Twine(ref->discs.size())
                       + " discriminants where " + llvm::Twine(discCount)
                       + " are expected");
        return {ref->data, ref->discs};
    }

    // EP §6.4.3.3: a string(n) IS an instance of the `string` schema, and its
    // one discriminant is the capacity -- a constant here.  This is what lets
    // `procedure p(var s: string)` take a string of any capacity.
    const plang::Type* T = arg.ResolvedType.get();
    if (T && T->Kind == TypeKind::VarString && discCount == 1) {
        auto* data = EmitLValue(arg);
        if (!data) codegenICE("string argument for a schema parameter is not "
                              "addressable");
        return {data, {llvm::ConstantInt::get(I64Ty,
                           static_cast<uint64_t>(ExprStrCap(arg)),
                           /*isSigned=*/true)}};
    }

    // A discriminated instance knows them at compile time.
    if (!T || T->Kind != TypeKind::SchemaInstance
            || T->SchemaDiscs.size() != discCount)
        codegenICE("argument for a schema parameter is not schematic");

    std::vector<llvm::Value*> discs;
    for (const auto& d : T->SchemaDiscs)
        discs.push_back(llvm::ConstantInt::get(I64Ty,
                            static_cast<uint64_t>(d.Value), /*isSigned=*/true));
    auto* data = EmitLValue(arg);
    if (!data) codegenICE("schema argument '" + T->Name + "' is not addressable");
    return {data, discs};
}

unsigned SchemaAccess::schemaArgDiscs(const std::string& mangledName,
                                       size_t astArgIdx) const {
    return SchemaArgDiscCountOf(mangledName, astArgIdx);
}

void SchemaAccess::pushSchemaArgs(std::vector<llvm::Value*>& args,
                                   const ExprNode& arg, unsigned discCount) {
    auto [data, discs] = schemaActual(arg, discCount);
    args.push_back(data);
    args.insert(args.end(), discs.begin(), discs.end());
}

void SchemaAccess::emitSchemaDiscMatch(const SchemaRef& dst,
                                        const SchemaRef& src) {
    const auto& names = dst.semaTy->SchemaDiscs;
    for (size_t i = 0; i < dst.discs.size() && i < src.discs.size(); ++i) {
        // A constant-folded comparison costs nothing when both sides came from
        // discriminated instances.
        auto* differ = B.CreateICmpNE(dst.discs[i], src.discs[i],
                                      "sch.disc.ne");
        if (auto* c = llvm::dyn_cast<llvm::ConstantInt>(differ); c && c->isZero())
            continue;
        auto* nameStr = Strings.internStrPtr(i < names.size() ? names[i].Name : "?");
        RangeGuards.emitGuard(differ, "schema.disc", [&] {
            B.CreateCall(
                RtFns.getExternFnN("plang_err_schema_disc", llvm::Type::getVoidTy(Ctx),
                                   {PtrTy, I64Ty, I64Ty}),
                {nameStr, dst.discs[i], src.discs[i]});
        });
    }
}

std::pair<llvm::Value*, llvm::Value*>
SchemaAccess::schemaArrayBounds(const SchemaRef& ref) {
    // R3: the closed forms, where Sema could build them.
    if (ref.semaTy && ref.semaTy->SchemaLowForm && ref.semaTy->SchemaHighForm)
        return {SchemaLayout.emitExtentForm(*ref.semaTy->SchemaLowForm,  ref.discs),
                SchemaLayout.emitExtentForm(*ref.semaTy->SchemaHighForm, ref.discs)};

    // Likewise: the top-level body's bounds come from the schema type's own
    // forms above, or not at all.  Measured at 0 tests before removal.
    codegenICE("schema '" + ref.semaTy->SchemaName
               + "' has no closed form for its array bounds");
    return {nullptr, nullptr};
}

llvm::Type* SchemaAccess::schemaStorageType(const SchemaRef& ref) {
    if (!ref.semaTy->SchemaBody || ref.semaTy->SchemaBody->isError())
        codegenICE("schema '" + ref.semaTy->SchemaName + "' has no resolved body");
    const plang::Type* body = schemaUnderlying(ref.semaTy->SchemaBody.get());
    if (body->Kind == TypeKind::Array && body->ElemType)
        return Types.llvmTypeOfSemaType(*body->ElemType);
    return Types.llvmTypeOfSemaType(*body);
}

llvm::Value* SchemaAccess::schemaBodySize(const plang::Type& schema,
                                           const std::vector<llvm::Value*>& discs) {
    if (!schema.SchemaBody || schema.SchemaBody->isError())
        codegenICE("schema '" + schema.SchemaName + "' has no resolved body");
    // The body may itself be another schema instantiation (EP §6.4.7); the
    // question below -- string capacity, array vs fixed extent -- is about
    // what it ultimately denotes, not the immediate hop.
    const plang::Type* body = schemaUnderlying(schema.SchemaBody.get());

    // EP §6.4.3.3's string schema has no declaration to walk -- it is not
    // written in the program -- and its one discriminant IS the capacity, so
    // the size is the header plus that.  Reading the probe-lowered struct here
    // asked for a string(1) and allocated 16 bytes for a `new(q, 20)`, which
    // the first assignment then wrote past.
    if (body->Kind == TypeKind::VarString && body->ExtentVaries
            && discs.size() == 1) {
        // EP §6.7.5.3 lets a discriminant be an expression, so a capacity that
        // describes no string is only detectable here.  Checked on the EXTENT
        // rather than on the discriminants: ExtentVaries is one flag for the
        // whole body and does not say WHICH discriminant sizes anything, so
        // testing them all rejected `new(v, 0, 4)` for an `array[lo..hi]`,
        // whose lower bound is legitimately zero.
        auto* bad = B.CreateICmpSLT(discs[0], i64c(0), "str.cap.bad");
        if (auto* c = llvm::dyn_cast<llvm::ConstantInt>(bad); !c || !c->isZero()) {
            auto* nm = Strings.internStrPtr(schema.SchemaDiscs.empty()
                                        ? "capacity" : schema.SchemaDiscs[0].Name);
            RangeGuards.emitGuard(bad, "schema.extent", [&] {
                B.CreateCall(
                    RtFns.getExternFnN("plang_err_schema_extent",
                                       llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty}),
                    {nm, discs[0]});
            });
        }
        return SchemaLayout.alignUpV(B.CreateAdd(i64c(8), discs[0], "str.size"), 8);
    }

    // Any other body whose extent a discriminant fixes is measured by walking
    // the declaration with the discriminants bound.  An ARRAY body is left to
    // the path below: it already recovered its extent from the discriminants
    // before any of this existed, and routing it through here changed nothing
    // but which code computed the same number.
    // An array body whose BOUNDS vary keeps the path below: it recovered its
    // extent from the discriminants before any of this existed.  One whose
    // ELEMENT varies does not -- that path takes the element size from the
    // probe-lowered type, so `array[1..3] of string(n)` was allocated three
    // string(1)s wide while every store was told the real capacity, which
    // overlapped the elements and ran off the end of the block.
    const bool elemVaries = body->ElemType && body->ElemType->ExtentVaries;
    if (body->ExtentVaries && (body->Kind != TypeKind::Array || elemVaries)) {
        if (const TypeNode* bodyNode = SchemaTypes.schemaBodyNodeOf(schema); bodyNode) {
            SchemaRef ref{&schema, nullptr, discs};
            SchemaLayoutEngine::RtDiscScope disc(SchemaLayout, ref.discs);
            return SchemaLayout.rtSizeOfTypeNode(bodyNode);
        }
    }

    // A fixed layout is sized straight from the body type.
    if (schema.SchemaFixedLayout || body->Kind != TypeKind::Array) {
        auto* bodyTy = Types.llvmTypeOfSemaType(*body);
        return llvm::ConstantInt::get(I64Ty,
                   Mod.getDataLayout().getTypeAllocSize(bodyTy));
    }

    SchemaRef ref{&schema, nullptr, discs};
    auto [lo, hi] = schemaArrayBounds(ref);
    auto* count = B.CreateAdd(B.CreateSub(hi, lo),
                              llvm::ConstantInt::get(I64Ty, 1), "sch.count");
    // An empty range still needs a valid allocation, not a zero-byte one.
    count = B.CreateSelect(
        B.CreateICmpSLT(count, llvm::ConstantInt::get(I64Ty, 1)),
        llvm::ConstantInt::get(I64Ty, 1), count, "sch.count.min");
    auto* elemTy = body->ElemType ? Types.llvmTypeOfSemaType(*body->ElemType) : I64Ty;
    auto  elemSz = Mod.getDataLayout().getTypeAllocSize(elemTy);

    // count is a run-time value with no upper bound of its own -- Sema checks
    // a discriminant against Integer's own domain, not against what this
    // multiply can survive -- while elemSz is a small host-side constant.
    // `new(p, 2305843009213693953)` on `array[1..n] of real` makes count
    // 2^61+1 and elemSz 8: count*elemSz wraps past 2^64 and lands back on a
    // small positive i64 (8), which is worse than going negative, since
    // plang_new's own check (the last line of defense before calloc) only
    // rejects negative sizes.  The wrapped allocation would then succeed at a
    // fraction of its real size while every index up to the original,
    // unwrapped bound still range-checks against the full declared extent --
    // a silent heap buffer overflow on the first out-of-range-but-in-bound
    // store.  elemSz is known here, at codegen time, so the threshold count
    // must stay under is a compile-time constant too: no overflow intrinsic
    // needed, just count > INT64_MAX / elemSz, guarded the same way the
    // string-capacity check above guards its own runtime value.
    const int64_t maxCount = static_cast<int64_t>(elemSz > 0
        ? (static_cast<uint64_t>(INT64_MAX) / elemSz)
        : static_cast<uint64_t>(INT64_MAX));
    auto* overflow = B.CreateICmpSGT(count, llvm::ConstantInt::get(I64Ty, maxCount),
                                     "sch.count.overflow");
    RangeGuards.emitGuard(overflow, "schema.size", [&] {
        B.CreateCall(
            RtFns.getExternFnN("plang_err_bad_alloc_size",
                               llvm::Type::getVoidTy(Ctx), {I64Ty}),
            {count});
    });

    return B.CreateMul(count, llvm::ConstantInt::get(I64Ty, elemSz),
                       "sch.bytes");
}

void SchemaAccess::emitNewSchema(const ExprNode& ptrArg,
                                  const plang::Type& schema,
                                  std::span<const std::unique_ptr<ExprNode>> discArgs) {
    const size_t s = schema.SchemaDiscs.size();
    if (discArgs.size() != s)
        codegenICE("new() for schema '" + schema.SchemaName
                   + "' was given the wrong number of discriminants");

    std::vector<llvm::Value*> discs;
    discs.reserve(s);
    for (const auto& a : discArgs) {
        auto* v = ToI64(EmitExpr(*a));
        if (!v) codegenICE("discriminant of new() for schema '" + schema.SchemaName
                           + "' is not an integer value");
        discs.push_back(v);
    }

    const uint64_t hdrBytes = SchemaLayout.schemaHeaderBytes(schema);
    auto* bytes = B.CreateAdd(llvm::ConstantInt::get(I64Ty, hdrBytes),
                              schemaBodySize(schema, discs), "sch.alloc");
    auto* base  = B.CreateCall(RtFns.getRuntimeNewFn(), {bytes}, "sch.new");

    for (size_t i = 0; i < s; ++i) {
        auto* slot = B.CreateGEP(I64Ty, base,
            {llvm::ConstantInt::get(I64Ty, i)}, "sch.disc.ptr");
        B.CreateStore(discs[i], slot);
    }

    auto* addr = EmitLValue(ptrArg);
    if (!addr) codegenICE("new() target is not addressable");
    B.CreateStore(base, addr);
}

llvm::Value* SchemaAccess::exprStrCapV(const ExprNode& e) {
    // ISO §6.4.3.2's other string shape, `packed array[1..n] of char`, has a
    // capacity too -- exprStrCap answers 0 for it, being VarString-only, so a
    // substr/trim chained off a char-string argument (below) capped its
    // result at zero characters instead of n.
    if (ExprIsCharStr(e)) return i64c(ExprCharStrLen(e));
    if (!ExprIsVarStr(e) || !e.ResolvedType->ExtentVaries)
        return i64c(ExprStrCap(e));

    // A `with`-bound field carries the capacity recorded when it was bound;
    // by then it is an ordinary name with no path back to its object.
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e))
        if (const auto* ve = SymTab.findVar(id->Name); ve && ve->strCapV)
            return ve->strCapV;

    // A schema whose body is a string: EP §6.4.3.3 makes its one discriminant
    // the capacity, and there is no written declaration to walk for it.
    //
    // This required a DerefExpr, so it answered for `q^` and not for a
    // `var s: string` formal -- whose capacity travels in exactly the same
    // place, as the discriminant beside the pointer.  The formal fell through
    // to the probe's string(1), and `s := 'zz'` raised "string of length 2
    // assigned to a string(1)" against an actual with room for ten.
    // schemaRefOf answers for a formal parameter as readily as for a
    // dereference; asking it for both is the whole change.  The body may
    // itself be another schema instantiation (EP §6.4.7) whose own
    // discriminant is bound to this one -- `C(n) = B(n)` for `B(m) =
    // string(m)` -- so the underlying kind is what answers, not the
    // immediate hop.
    if (auto ref = schemaRefOf(e);
            ref && ref->semaTy && ref->semaTy->SchemaBody
            && schemaUnderlying(ref->semaTy->SchemaBody.get())->Kind == TypeKind::VarString
            && ref->discs.size() == 1)
        return ref->discs[0];

    // Anywhere else: the capacity is the expression the component was DECLARED
    // with, re-emitted against the discriminants its object carries.  Asked of
    // the PATH, so a string reached through a nested record or an array element
    // answers as readily as a direct field -- recognising only the shapes `q^`
    // and `p^.s` is what left every deeper one folding the probe's string(1).
    if (auto path = schemaPathOf(e))
        if (auto* cap = strCapFromPath(*path)) return cap;

    // substr and trim are typed as the SAME Type object as their argument, so
    // a result over a discriminant-sized string carries ExtentVaries with the
    // probe's capacity of 1 -- and a CallExpr matches none of the shapes above,
    // so it fell through to exactly that 1.  Every later operation was then
    // told the string could hold one character: `substr(q^.s,1,99) + 'Z'` came
    // out two characters long on a q^ of capacity 100.  The result is as wide
    // as what it was taken from, which is a question the argument can answer.
    if (auto* call = llvm::dyn_cast<CallExpr>(&e)) {
        const std::string fn = toLower(call->Name);
        if ((fn == "substr" || fn == "trim") && !call->Args.empty())
            return exprStrCapV(*call->Args[0]);
    }
    return i64c(ExprStrCap(e));
}

/// R5: the address AND the capacity of a string from ONE walk of its access
/// path.  ISO §6.8.2.2 and §6.9.1 evaluate a variable-access once, and the
/// idiom this replaces -- `{emitStrAddr(x), exprStrCapV(x)}` -- resolves it
/// twice, because each of those starts from the expression and walks down.
/// Measured on `q^.a[next].s` with a counting `next`: comparison, write,
/// length, whole-value assignment and substring assignment each called it
/// THREE times.
///
/// The fallback is not a second walk.  A string whose capacity does not vary
/// answers exprStrCapV from a constant without touching the path at all, so
/// only the varying case ever had two to collapse.
std::pair<llvm::Value*, llvm::Value*>
SchemaAccess::strAddrAndCap(const ExprNode& e) {
    // R6: a substr/trim call primed below, answered instead of re-walked --
    // see pendingArgExpr_'s own comment for why this exists and why a bare
    // pointer compare is enough to key it.
    if (pendingArgExpr_ == &e) return pendingArgVal_;
    if (ExprIsVarStr(e) && e.ResolvedType && e.ResolvedType->ExtentVaries)
        if (auto path = schemaPathOf(e))
            if (auto* cap = strCapFromPath(*path))
                return {path->addr, cap};
    // R6: substr and trim's result carries its ARGUMENT's capacity (the
    // CallExpr branch of exprStrCapV, below), not one of its own -- so
    // asking this function for the call's (address, capacity) is really two
    // questions about Args[0]: what the call's own evaluation marshals it
    // as, and what its capacity is.  Walking Args[0] for the second after
    // EmitStrAddr already walked it once for the first, inside the call's
    // argument marshalling, repeated whatever side effect sits in that
    // path.  Args[0] is walked here, ONCE, and handed to that nested
    // marshalling through pendingArgExpr_ instead.
    if (auto* call = llvm::dyn_cast<CallExpr>(&e)) {
        const std::string fn = toLower(call->Name);
        if ((fn == "substr" || fn == "trim") && !call->Args.empty()) {
            auto argAddrCap = strAddrAndCap(*call->Args[0]);
            pendingArgExpr_ = call->Args[0].get();
            pendingArgVal_  = argAddrCap;
            auto* addr = EmitStrAddr(e);
            pendingArgExpr_ = nullptr;
            return {addr, argAddrCap.second};
        }
    }
    auto* addr = EmitStrAddr(e);
    return {addr, exprStrCapV(e)};
}

/// The capacity of an already-resolved path, so a caller that has one need not
/// resolve it again.  Resolving twice re-emits every subscript along the way,
/// which for `q^.a[next].s := v` called `next` once for the address and once
/// for the capacity.
llvm::Value* SchemaAccess::strCapFromPath(const SchemaPath& path) {
    auto* st = llvm::dyn_cast_or_null<StringTypeNode>(path.decl);
    if (!st) return nullptr;
    // The form first: it is arithmetic over the discriminants by index, with
    // every other leaf already folded where the declaration was written, so
    // there is no name in it for this procedure's scope to capture.
    if (st->ExtentLow && !path.root.discs.empty())
        if (auto* cap = SchemaLayout.emitExtentForm(*st->ExtentLow, path.root.discs))
            return cap;
    // No expression fallback: a capacity with no form would be re-resolved in
    // whatever procedure is doing the access, which is the archetype this work
    // exists to delete.  Measured at 0 tests before removal.
    codegenICE("a schema string capacity with no closed form to evaluate");
}

const ArrayTypeNode* SchemaAccess::varyingArrayFieldOf(const FieldExpr& fe) {
    const Type* RecTy = fe.Record->ResolvedType.get();
    if (RecTy && (RecTy->Kind == TypeKind::Schema
                  || RecTy->Kind == TypeKind::SchemaInstance) && RecTy->SchemaBody)
        RecTy = schemaUnderlying(RecTy);
    if (!RecTy || RecTy->Kind != TypeKind::Record || !RecTy->RecordDecl)
        return nullptr;
    for (const auto& fd : RecTy->RecordDecl->Fields) {
        if (!std::ranges::any_of(fd.Names, [&](const std::string& n) {
                return eqCI(n, fe.Field); }))
            continue;
        const TypeNode* d = fd.Type.get();
        while (auto* pk = llvm::dyn_cast_or_null<PackedTypeNode>(d))
            d = pk->Inner.get();
        return llvm::dyn_cast_or_null<ArrayTypeNode>(d);
    }
    return nullptr;
}

namespace {
const TypeNode* peel(const TypeNode* tn) {
    while (auto* pk = llvm::dyn_cast_or_null<PackedTypeNode>(tn)) tn = pk->Inner.get();
    return tn;
}
} // namespace

/// R3: a component of a schema body may itself be a schema INSTANTIATION whose
/// discriminants are arithmetic over the enclosing ones -- the standard's own
/// `matrix(m,n) = array[1..m] of vector(n)`, and equally `outer(n) = record x:
/// inner(n) ... end`.  Descends into it: evaluates that instantiation's
/// discriminants from the outer ones and returns its BODY's denoter with a root
/// carrying them, so everything below is measured against the instance rather
/// than against the probe.
///
/// This was written into the index arm of schemaPathOf and not the field arm,
/// so `q^[i][j]` was right and `q^.x.k` was wrong -- the field arm saw a
/// SchemaTypeNode where it wanted a record, gave up, and the whole access fell
/// back to the probe layout.  `q^.x.k` then landed on `q^.x.a[2]`'s bytes.  One
/// descent, used by both, because two copies is how the arms came to disagree.
std::pair<SchemaAccess::SchemaRef, const TypeNode*>
SchemaAccess::descendIntoInstantiation(const SchemaRef& root, llvm::Value* addr,
                                        const TypeNode* decl) {
    // As many levels as there are.  This was written as a single step, in the
    // commit that fixed a dozen OTHER sites by making the same descent a loop
    // -- `type A(k) = array[1..k]; B(n) = A(n*2+1); C(n) = record b: B(n) end`
    // stopped at B and indexed b against the probe's 1..3 instead of 1..7.
    //
    // Each level's discriminants are arithmetic over the level above it, so
    // they are evaluated against the discriminants worked out so far rather
    // than against the outermost object's.
    SchemaRef       cur = root;
    const TypeNode* d   = peel(decl);
    for (int Hops = 0; Hops < 16; ++Hops) {
        auto* sn = llvm::dyn_cast_or_null<SchemaTypeNode>(d);
        if (!sn || sn->ActualForms.empty() || !sn->ResolvedBody) break;
        const TypeNode* body = SchemaTypes.schemaBodyNodeOf(*sn->ResolvedBody);
        if (!body) break;
        std::vector<llvm::Value*> inner;
        inner.reserve(sn->ActualForms.size());
        for (const auto& F : sn->ActualForms)
            inner.push_back(SchemaLayout.emitExtentForm(F, cur.discs));
        cur = SchemaRef{sn->ResolvedBody.get(), addr, inner};
        d   = peel(body);
    }
    return {cur, d};
}

std::optional<SchemaAccess::SchemaPath>
SchemaAccess::schemaPathOf(const ExprNode& e) {
    // A `with`-bound component resumes the path it was bound from: it is an
    // ordinary name by now, and without this anything reached through it --
    // `with p^ do d[i]` -- would be indexed against the probe's bounds.
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e))
        if (const auto* ve = SymTab.findVar(id->Name); ve && ve->pathDecl && ve->pathRootTy)
            return SchemaPath{SchemaRef{ve->pathRootTy, ve->ptr, ve->pathDiscs},
                              ve->ptr, ve->pathDecl};

    // Root: the object itself, whose header carries the discriminants.
    if (llvm::isa<DerefExpr>(&e) || llvm::isa<IdentExpr>(&e)) {
        auto ref = schemaRefOf(e);
        if (!ref || !ref->semaTy) return std::nullopt;
        const TypeNode* bodyNode = SchemaTypes.schemaBodyNodeOf(*ref->semaTy);
        if (!bodyNode) return std::nullopt;
        // The body may itself be another schema instantiation --
        // `outer(n) = inner(n)` -- so the FieldExpr and IndexExpr arms below
        // both descend through it before asking what the declaration IS.
        // The root case did not, so `q^` for a `^outer` handed back a
        // SchemaTypeNode where a RecordTypeNode (or array, or string) was
        // wanted: `with q^ do` could not cast it to a record and ICE'd
        // ("'with' on a non-record operand"), regardless of nesting depth.
        auto [root, decl] = descendIntoInstantiation(*ref, ref->data, bodyNode);
        return SchemaPath{root, ref->data, decl};
    }

    if (auto* fe = llvm::dyn_cast<FieldExpr>(&e)) {
        auto base = schemaPathOf(*fe->Record);
        if (!base) return std::nullopt;
        auto [root, decl] =
            descendIntoInstantiation(base->root, base->addr, base->decl);
        auto* rt = llvm::dyn_cast_or_null<RecordTypeNode>(decl);
        if (!rt) return std::nullopt;
        auto* off = [&] {
            SchemaLayoutEngine::RtDiscScope disc(SchemaLayout, root.discs);
            return SchemaLayout.rtFieldOffset(*rt, fe->Field);
        }();
        const TypeNode* fieldDecl = fieldDenoterOf(*rt, fe->Field);
        auto* fldAddr = B.CreateGEP(I8Ty, base->addr, {off}, "path.fld");
        // The FIELD's own denoter may be an instantiation too -- `e: ent(n)`
        // inside `t(n)` -- so descend here, where the path is BUILT, rather
        // than leaving each consumer to remember.  They did not remember: the
        // capacity of a string field, a whole-value copy, `with`, and passing
        // the component as a schema formal each took the probe's discriminants,
        // as four separate defects with one cause.
        auto [fRoot, fDecl] = descendIntoInstantiation(root, fldAddr, fieldDecl);
        return SchemaPath{fRoot, fldAddr, fDecl};
    }

    if (auto* ie = llvm::dyn_cast<IndexExpr>(&e)) {
        auto base = schemaPathOf(*ie->Array);
        if (!base) return std::nullopt;
        // R3: the component may itself be a schema INSTANTIATION whose
        // discriminants are arithmetic over the enclosing ones -- indexing
        // `q^[i][j]` for `matrix(m,n) = array[1..m] of vector(n)` lands here
        // with a SchemaTypeNode where an array was wanted.  Evaluate that
        // instantiation's discriminants from the outer ones and carry on
        // inside it; the bounds and the stride are then its own, not the
        // probe's, which is what made the inner index check read 1..1.
        auto [root, decl] =
            descendIntoInstantiation(base->root, base->addr, base->decl);
        auto* at = llvm::dyn_cast_or_null<ArrayTypeNode>(decl);
        if (!at) return std::nullopt;
        llvm::Value *lo = nullptr, *hi = nullptr, *stride = nullptr;
        {
            SchemaLayoutEngine::RtDiscScope disc(SchemaLayout, root.discs);
            auto bounds = SchemaLayout.rtIndexBounds(*at);
            if (!bounds) return std::nullopt;
            lo     = bounds->first;
            hi     = bounds->second;
            stride = SchemaLayout.alignUpV(SchemaLayout.rtSizeOfTypeNode(at->Element.get()),
                              SchemaLayout.rtAlignOfTypeNode(at->Element.get()));
        }
        auto* idx = ToI64(EmitExpr(*ie->Index));
        RangeGuards.emitRangeCheckDyn(idx, lo, hi, /*isIndex=*/true, ie->Loc);
        auto* off = B.CreateMul(B.CreateSub(idx, lo), stride,
                                "path.idx");
        // Likewise an ELEMENT: `array[1..n] of ent(cap)`.
        auto* elemAddr = B.CreateGEP(I8Ty, base->addr, {off}, "path.elem");
        auto [eRoot, eDecl] =
            descendIntoInstantiation(root, elemAddr, at->Element.get());
        return SchemaPath{eRoot, elemAddr, eDecl};
    }

    return std::nullopt;
}

/// The declaration denoter of \p field, including one inside a variant part,
/// which is where its extent expressions are written.
const TypeNode* SchemaAccess::fieldDenoterOf(const RecordTypeNode& rt,
                                              const std::string& field) {
    for (const auto& fd : rt.Fields)
        if (std::ranges::any_of(fd.Names, [&](const std::string& n) {
                return eqCI(n, field); }))
            return fd.Type.get();
    if (rt.Variant) return variantFieldDenoterOf(*rt.Variant, field);
    return nullptr;
}

const TypeNode* SchemaAccess::variantFieldDenoterOf(const VariantPart& vp,
                                                     const std::string& field) {
    if (!vp.TagField.empty() && eqCI(vp.TagField, field)) return vp.TagType.get();
    for (const auto& vc : vp.Cases) {
        for (const auto& fd : vc.Fields)
            if (std::ranges::any_of(fd.Names, [&](const std::string& n) {
                    return eqCI(n, field); }))
                return fd.Type.get();
        if (vc.NestedVariant)
            if (auto* t = variantFieldDenoterOf(*vc.NestedVariant, field)) return t;
    }
    return nullptr;
}

void SchemaAccess::setVarStrCap(const std::string& name, llvm::Value* cap) {
    for (auto it = Scopes.rbegin(); it != Scopes.rend(); ++it) {
        auto f = it->find(toLower(name));
        if (f != it->end()) { f->second.strCapV = cap; return; }
    }
}

void SchemaAccess::setVarSchemaPath(const std::string& name,
                                     const SchemaRef& root,
                                     const TypeNode* decl) {
    for (auto it = Scopes.rbegin(); it != Scopes.rend(); ++it) {
        auto f = it->find(toLower(name));
        if (f != it->end()) {
            f->second.pathRootTy = root.semaTy;
            f->second.pathDiscs  = root.discs;
            f->second.pathDecl   = decl;
            return;
        }
    }
}
