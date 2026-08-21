// CodeGenSchema.cpp — EP §6.4.7 undiscriminated schema types.
//
// A schema is a family of types indexed by a tuple of discriminants.  When the
// discriminants are written out (`vec(4)`) the type is ordinary and lowers like
// any other.  This file covers the case where they are not: a formal parameter
// declared `v: vec`, and `p^` where `p: ^vec`.  In both, the discriminants are
// only known at run time, so they travel with the value:
//
//   * a schema formal parameter takes one extra i64 argument per discriminant,
//     immediately after the pointer to the body — the same shape as the
//     conformant-array bounds in EP §6.7.3.7;
//   * new(p, d1..ds) puts the discriminants in a header of s i64 slots in front
//     of the body, so p^ can recover them anywhere the pointer reaches.
//
// Everything that needs an extent — indexing, allocation — re-emits the body's
// bound expressions with the discriminants bound to those run-time values.

#include "CodeGenImpl.h"

#include <ranges>

// ---------------------------------------------------------------------------
// Schema definitions
// ---------------------------------------------------------------------------

void Codegen::Impl::registerSchemaDefs(const BlockNode& block) {
    schemaTypes_->registerSchemaDefs(block);
}

const SchemaTypeRegistry::SchemaDef*
Codegen::Impl::findSchemaDef(const std::string& name) const {
    return schemaTypes_->findSchemaDef(name);
}

namespace {
/// The array denoter of a schema body, looking through `packed`.
const ArrayTypeNode* arrayBodyOf(const TypeNode* body) {
    while (auto* pk = llvm::dyn_cast_or_null<PackedTypeNode>(body))
        body = pk->Inner.get();
    return llvm::dyn_cast_or_null<ArrayTypeNode>(body);
}
} // namespace

// ---------------------------------------------------------------------------
// Recovering the run-time view
// ---------------------------------------------------------------------------

/// Bytes of discriminant header in front of a schema body.
///
/// One question, one answer: emitNewSchema lays this out and schemaRefOf skips
/// it, and they were each spelling `SchemaDiscs.size() * 8` for themselves.
/// That is also the wrong number -- it aligns the body to 8 whatever the body
/// needs, so a body wanting 16 sat misaligned and the aligned vector stores
/// llvm emits at -O1 and above faulted on it.
std::optional<Codegen::Impl::SchemaRef>
Codegen::Impl::schemaRefOf(const ExprNode& e) {
    // A formal parameter: the body pointer and the discriminants are arguments.
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e)) {
        if (const auto* ve = findVar(id->Name); ve && ve->schemaTy)
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
        auto* base = emitExpr(*de->Pointer);
        if (!base) codegenICE("pointer to schema '" + T->SchemaName
                              + "' has no lowerable value");
        // Recovering the discriminants dereferences p just as surely as reading
        // the body does.  Every other route to a p^ emits this check; this one
        // did not, so `p^[i]` through a nil schema pointer read the header at
        // address 0 and took the process down instead of raising.
        if (base->getType()->isPointerTy()) emitNilCheck(base);
        SchemaRef ref;
        ref.semaTy = T;
        for (size_t i = 0; i < T->SchemaDiscs.size(); ++i) {
            auto* slot = builder.CreateGEP(i64Ty, base,
                {llvm::ConstantInt::get(i64Ty, i)}, "sch.disc.ptr");
            ref.discs.push_back(builder.CreateLoad(i64Ty, slot, "sch.disc"));
        }
        ref.data = builder.CreateGEP(i8Ty, base,
            {llvm::ConstantInt::get(i64Ty,
                static_cast<int64_t>(schemaHeaderBytes(*T)))}, "sch.data");
        return ref;
    }

    return std::nullopt;
}

std::pair<llvm::Value*, std::vector<llvm::Value*>>
Codegen::Impl::schemaActual(const ExprNode& arg, unsigned discCount) {
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
        auto* data = emitLValue(arg);
        if (!data) codegenICE("string argument for a schema parameter is not "
                              "addressable");
        return {data, {llvm::ConstantInt::get(i64Ty,
                           static_cast<uint64_t>(exprStrCap(arg)),
                           /*isSigned=*/true)}};
    }

    // A discriminated instance knows them at compile time.
    if (!T || T->Kind != TypeKind::SchemaInstance
            || T->SchemaDiscs.size() != discCount)
        codegenICE("argument for a schema parameter is not schematic");

    std::vector<llvm::Value*> discs;
    for (const auto& d : T->SchemaDiscs)
        discs.push_back(llvm::ConstantInt::get(i64Ty,
                            static_cast<uint64_t>(d.Value), /*isSigned=*/true));
    auto* data = emitLValue(arg);
    if (!data) codegenICE("schema argument '" + T->Name + "' is not addressable");
    return {data, discs};
}

unsigned Codegen::Impl::schemaArgDiscs(const std::string& mangledName,
                                       size_t astArgIdx) const {
    auto it = paramMeta_.find(mangledName);
    if (it == paramMeta_.end() || astArgIdx >= it->second.size()) return 0;
    return it->second[astArgIdx].schemaDiscCount;
}

void Codegen::Impl::pushSchemaArgs(std::vector<llvm::Value*>& args,
                                   const ExprNode& arg, unsigned discCount) {
    auto [data, discs] = schemaActual(arg, discCount);
    args.push_back(data);
    args.insert(args.end(), discs.begin(), discs.end());
}

void Codegen::Impl::emitSchemaDiscMatch(const SchemaRef& dst,
                                        const SchemaRef& src) {
    const auto& names = dst.semaTy->SchemaDiscs;
    for (size_t i = 0; i < dst.discs.size() && i < src.discs.size(); ++i) {
        // A constant-folded comparison costs nothing when both sides came from
        // discriminated instances.
        auto* differ = builder.CreateICmpNE(dst.discs[i], src.discs[i],
                                            "sch.disc.ne");
        if (auto* c = llvm::dyn_cast<llvm::ConstantInt>(differ); c && c->isZero())
            continue;
        auto* nameStr = internStrPtr(i < names.size() ? names[i].Name : "?");
        emitGuard(differ, "schema.disc", [&] {
            builder.CreateCall(
                getExternFnN("plang_err_schema_disc", llvm::Type::getVoidTy(ctx),
                             {ptrTy, i64Ty, i64Ty}),
                {nameStr, dst.discs[i], src.discs[i]});
        });
    }
}

// ---------------------------------------------------------------------------
// Extents
// ---------------------------------------------------------------------------

/// The body denoter of a schema, from the type itself where it carries one.
/// The name-keyed map is the fallback for a synthetic schema type that does
/// not -- a formal parameter's, whose discriminants are only known at run
/// time (see the file's own opening comment) -- found by the one thing such
/// a type still has: the name it was declared under.
const TypeNode* Codegen::Impl::schemaBodyNodeOf(const plang::Type& T) const {
    return schemaTypes_->schemaBodyNodeOf(T);
}

std::pair<llvm::Value*, llvm::Value*>
Codegen::Impl::schemaArrayBounds(const SchemaRef& ref) {
    // R3: the closed forms, where Sema could build them.
    if (ref.semaTy && ref.semaTy->SchemaLowForm && ref.semaTy->SchemaHighForm)
        return {emitExtentForm(*ref.semaTy->SchemaLowForm,  ref.discs),
                emitExtentForm(*ref.semaTy->SchemaHighForm, ref.discs)};

    // Likewise: the top-level body's bounds come from the schema type's own
    // forms above, or not at all.  Measured at 0 tests before removal.
    codegenICE("schema '" + ref.semaTy->SchemaName
               + "' has no closed form for its array bounds");
    return {nullptr, nullptr};
}

llvm::Type* Codegen::Impl::schemaStorageType(const SchemaRef& ref) {
    if (!ref.semaTy->SchemaBody || ref.semaTy->SchemaBody->isError())
        codegenICE("schema '" + ref.semaTy->SchemaName + "' has no resolved body");
    const plang::Type* body = schemaUnderlying(ref.semaTy->SchemaBody.get());
    if (body->Kind == TypeKind::Array && body->ElemType)
        return llvmTypeOfSemaType(*body->ElemType);
    return llvmTypeOfSemaType(*body);
}

llvm::Value* Codegen::Impl::schemaBodySize(const plang::Type& schema,
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
        auto* bad = builder.CreateICmpSLT(discs[0], i64c(0), "str.cap.bad");
        if (auto* c = llvm::dyn_cast<llvm::ConstantInt>(bad); !c || !c->isZero()) {
            auto* nm = internStrPtr(schema.SchemaDiscs.empty()
                                        ? "capacity" : schema.SchemaDiscs[0].Name);
            emitGuard(bad, "schema.extent", [&] {
                builder.CreateCall(
                    getExternFnN("plang_err_schema_extent",
                                 llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty}),
                    {nm, discs[0]});
            });
        }
        return alignUpV(builder.CreateAdd(i64c(8), discs[0], "str.size"), 8);
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
        if (const TypeNode* bodyNode = schemaBodyNodeOf(schema); bodyNode) {
            SchemaRef ref{&schema, nullptr, discs};
            RtDiscScope disc(*this, ref.discs);
            return rtSizeOfTypeNode(bodyNode);
        }
    }

    // A fixed layout is sized straight from the body type.
    if (schema.SchemaFixedLayout || body->Kind != TypeKind::Array) {
        auto* bodyTy = llvmTypeOfSemaType(*body);
        return llvm::ConstantInt::get(i64Ty,
                   mod->getDataLayout().getTypeAllocSize(bodyTy));
    }

    SchemaRef ref{&schema, nullptr, discs};
    auto [lo, hi] = schemaArrayBounds(ref);
    auto* count = builder.CreateAdd(builder.CreateSub(hi, lo),
                                    llvm::ConstantInt::get(i64Ty, 1), "sch.count");
    // An empty range still needs a valid allocation, not a zero-byte one.
    count = builder.CreateSelect(
        builder.CreateICmpSLT(count, llvm::ConstantInt::get(i64Ty, 1)),
        llvm::ConstantInt::get(i64Ty, 1), count, "sch.count.min");
    auto* elemTy = body->ElemType ? llvmTypeOfSemaType(*body->ElemType) : i64Ty;
    auto  elemSz = mod->getDataLayout().getTypeAllocSize(elemTy);
    return builder.CreateMul(count, llvm::ConstantInt::get(i64Ty, elemSz),
                             "sch.bytes");
}

// ---------------------------------------------------------------------------
// new(p, d1..ds)
// ---------------------------------------------------------------------------

void Codegen::Impl::emitNewSchema(const ExprNode& ptrArg,
                                  const plang::Type& schema,
                                  std::span<const std::unique_ptr<ExprNode>> discArgs) {
    const size_t s = schema.SchemaDiscs.size();
    if (discArgs.size() != s)
        codegenICE("new() for schema '" + schema.SchemaName
                   + "' was given the wrong number of discriminants");

    std::vector<llvm::Value*> discs;
    discs.reserve(s);
    for (const auto& a : discArgs) {
        auto* v = toI64(emitExpr(*a));
        if (!v) codegenICE("discriminant of new() for schema '" + schema.SchemaName
                           + "' is not an integer value");
        discs.push_back(v);
    }

    const uint64_t hdrBytes = schemaHeaderBytes(schema);
    auto* bytes = builder.CreateAdd(llvm::ConstantInt::get(i64Ty, hdrBytes),
                                    schemaBodySize(schema, discs), "sch.alloc");
    auto* base  = builder.CreateCall(getRuntimeNewFn(), {bytes}, "sch.new");

    for (size_t i = 0; i < s; ++i) {
        auto* slot = builder.CreateGEP(i64Ty, base,
            {llvm::ConstantInt::get(i64Ty, i)}, "sch.disc.ptr");
        builder.CreateStore(discs[i], slot);
    }

    auto* addr = emitLValue(ptrArg);
    if (!addr) codegenICE("new() target is not addressable");
    builder.CreateStore(base, addr);
}

llvm::Value* Codegen::Impl::exprStrCapV(const ExprNode& e) {
    // ISO §6.4.3.2's other string shape, `packed array[1..n] of char`, has a
    // capacity too -- exprStrCap answers 0 for it, being VarString-only, so a
    // substr/trim chained off a char-string argument (below) capped its
    // result at zero characters instead of n.
    if (exprIsCharStr(e)) return i64c(exprCharStrLen(e));
    if (!exprIsVarStr(e) || !e.ResolvedType->ExtentVaries)
        return i64c(exprStrCap(e));

    // A `with`-bound field carries the capacity recorded when it was bound;
    // by then it is an ordinary name with no path back to its object.
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e))
        if (const auto* ve = findVar(id->Name); ve && ve->strCapV)
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
    return i64c(exprStrCap(e));
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
Codegen::Impl::strAddrAndCap(const ExprNode& e) {
    if (exprIsVarStr(e) && e.ResolvedType && e.ResolvedType->ExtentVaries)
        if (auto path = schemaPathOf(e))
            if (auto* cap = strCapFromPath(*path))
                return {path->addr, cap};
    auto* addr = emitStrAddr(e);
    return {addr, exprStrCapV(e)};
}

/// The capacity of an already-resolved path, so a caller that has one need not
/// resolve it again.  Resolving twice re-emits every subscript along the way,
/// which for `q^.a[next].s := v` called `next` once for the address and once
/// for the capacity.
llvm::Value* Codegen::Impl::strCapFromPath(const SchemaPath& path) {
    auto* st = llvm::dyn_cast_or_null<StringTypeNode>(path.decl);
    if (!st) return nullptr;
    // The form first: it is arithmetic over the discriminants by index, with
    // every other leaf already folded where the declaration was written, so
    // there is no name in it for this procedure's scope to capture.
    if (st->ExtentLow && !path.root.discs.empty())
        if (auto* cap = emitExtentForm(*st->ExtentLow, path.root.discs))
            return cap;
    // No expression fallback: a capacity with no form would be re-resolved in
    // whatever procedure is doing the access, which is the archetype this work
    // exists to delete.  Measured at 0 tests before removal.
    codegenICE("a schema string capacity with no closed form to evaluate");
}

// ---------------------------------------------------------------------------
// Run-time layout
//
// EP §6.4.7's run-time layout walk (the differential-oracle counterpart to
// CodeGenTypes.cpp's static layout) now lives in SchemaLayoutEngine; Impl's
// rt*/alignUpV methods are inline forwarders in CodeGenImpl.h.
// ---------------------------------------------------------------------------

const ArrayTypeNode* Codegen::Impl::varyingArrayFieldOf(const FieldExpr& fe) {
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

// ---------------------------------------------------------------------------
// Resolving an access path into a run-time-laid-out object
//
// The special cases this replaces -- p^, then p^.f, then p^.f[i] -- were each
// written as its own branch, so anything one level deeper fell off the end and
// silently got the probe layout.  Matching on the SHAPE of an access is the
// same mistake as matching on a name: `q^.inner.k` and `q^.a[i].s` are the same
// question asked twice more.  This answers it once, by recursion.
// ---------------------------------------------------------------------------

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
std::pair<Codegen::Impl::SchemaRef, const TypeNode*>
Codegen::Impl::descendIntoInstantiation(const SchemaRef& root, llvm::Value* addr,
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
        const TypeNode* body = schemaBodyNodeOf(*sn->ResolvedBody);
        if (!body) break;
        std::vector<llvm::Value*> inner;
        inner.reserve(sn->ActualForms.size());
        for (const auto& F : sn->ActualForms)
            inner.push_back(emitExtentForm(F, cur.discs));
        cur = SchemaRef{sn->ResolvedBody.get(), addr, inner};
        d   = peel(body);
    }
    return {cur, d};
}

std::optional<Codegen::Impl::SchemaPath>
Codegen::Impl::schemaPathOf(const ExprNode& e) {
    // A `with`-bound component resumes the path it was bound from: it is an
    // ordinary name by now, and without this anything reached through it --
    // `with p^ do d[i]` -- would be indexed against the probe's bounds.
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e))
        if (const auto* ve = findVar(id->Name); ve && ve->pathDecl && ve->pathRootTy)
            return SchemaPath{SchemaRef{ve->pathRootTy, ve->ptr, ve->pathDiscs},
                              ve->ptr, ve->pathDecl};

    // Root: the object itself, whose header carries the discriminants.
    if (llvm::isa<DerefExpr>(&e) || llvm::isa<IdentExpr>(&e)) {
        auto ref = schemaRefOf(e);
        if (!ref || !ref->semaTy) return std::nullopt;
        const TypeNode* bodyNode = schemaBodyNodeOf(*ref->semaTy);
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
            RtDiscScope disc(*this, root.discs);
            return rtFieldOffset(*rt, fe->Field);
        }();
        const TypeNode* fieldDecl = fieldDenoterOf(*rt, fe->Field);
        auto* fldAddr = builder.CreateGEP(i8Ty, base->addr, {off}, "path.fld");
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
            RtDiscScope disc(*this, root.discs);
            auto bounds = rtIndexBounds(*at);
            if (!bounds) return std::nullopt;
            lo     = bounds->first;
            hi     = bounds->second;
            stride = alignUpV(rtSizeOfTypeNode(at->Element.get()),
                              rtAlignOfTypeNode(at->Element.get()));
        }
        auto* idx = toI64(emitExpr(*ie->Index));
        emitRangeCheckDyn(idx, lo, hi, /*isIndex=*/true, ie->Loc);
        auto* off = builder.CreateMul(builder.CreateSub(idx, lo), stride,
                                      "path.idx");
        // Likewise an ELEMENT: `array[1..n] of ent(cap)`.
        auto* elemAddr = builder.CreateGEP(i8Ty, base->addr, {off}, "path.elem");
        auto [eRoot, eDecl] =
            descendIntoInstantiation(root, elemAddr, at->Element.get());
        return SchemaPath{eRoot, elemAddr, eDecl};
    }

    return std::nullopt;
}

/// The declaration denoter of \p field, including one inside a variant part,
/// which is where its extent expressions are written.
const TypeNode* Codegen::Impl::fieldDenoterOf(const RecordTypeNode& rt,
                                              const std::string& field) {
    for (const auto& fd : rt.Fields)
        if (std::ranges::any_of(fd.Names, [&](const std::string& n) {
                return eqCI(n, field); }))
            return fd.Type.get();
    if (rt.Variant) return variantFieldDenoterOf(*rt.Variant, field);
    return nullptr;
}

const TypeNode* Codegen::Impl::variantFieldDenoterOf(const VariantPart& vp,
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

/// Whether \p e names a component of a run-time-laid-out object, so that its
/// address, extent and capacity all have to be computed rather than folded.
bool Codegen::Impl::isRuntimeLaidOut(const ExprNode& e) {
    return e.ResolvedType && e.ResolvedType->ExtentVaries;
}

void Codegen::Impl::setVarStrCap(const std::string& name, llvm::Value* cap) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto f = it->find(toLower(name));
        if (f != it->end()) { f->second.strCapV = cap; return; }
    }
}

void Codegen::Impl::setVarSchemaPath(const std::string& name,
                                     const SchemaRef& root,
                                     const TypeNode* decl) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto f = it->find(toLower(name));
        if (f != it->end()) {
            f->second.pathRootTy = root.semaTy;
            f->second.pathDiscs  = root.discs;
            f->second.pathDecl   = decl;
            return;
        }
    }
}
