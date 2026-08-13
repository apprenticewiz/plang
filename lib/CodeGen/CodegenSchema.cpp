// CodegenSchema.cpp — EP §6.4.7 undiscriminated schema types.
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

#include "CodegenImpl.h"

#include <ranges>

// ---------------------------------------------------------------------------
// Schema definitions
// ---------------------------------------------------------------------------

void Codegen::Impl::registerSchemaDefs(const BlockNode& block) {
    for (const auto& td : block.Types) {
        if (td.SchemaParams.empty() || !td.Type) continue;
        SchemaDef def;
        for (const auto& grp : td.SchemaParams)
            for (const auto& nm : grp.Names)
                def.discNames.push_back(nm);
        def.body = td.Type.get();
        schemaDefs_[toLower(td.Name)] = std::move(def);
    }
}

const Codegen::Impl::SchemaDef*
Codegen::Impl::findSchemaDef(const std::string& name) const {
    auto it = schemaDefs_.find(toLower(name));
    return it != schemaDefs_.end() ? &it->second : nullptr;
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
        ref.data = builder.CreateGEP(i64Ty, base,
            {llvm::ConstantInt::get(i64Ty, T->SchemaDiscs.size())}, "sch.data");
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

    // A discriminated instance knows them at compile time.
    const plang::Type* T = arg.ResolvedType.get();
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
    auto it = schemaParamDiscs_.find(mangledName);
    if (it == schemaParamDiscs_.end() || astArgIdx >= it->second.size()) return 0;
    return it->second[astArgIdx];
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

void Codegen::Impl::bindSchemaDiscs(const SchemaRef& ref) {
    // The body's bound and capacity expressions are written in terms of the
    // formal discriminant names, so they are bound as ordinary variables and
    // the expressions re-emitted.  The caller pops the scope.
    pushScope();
    const SchemaDef* def = findSchemaDef(ref.semaTy->SchemaName);
    if (!def) return;
    for (size_t i = 0; i < def->discNames.size() && i < ref.discs.size(); ++i) {
        auto* slot = createEntryAlloca(i64Ty, "disc." + def->discNames[i]);
        builder.CreateStore(ref.discs[i], slot);
        defVar(def->discNames[i], slot, i64Ty);
    }
}

std::pair<llvm::Value*, llvm::Value*>
Codegen::Impl::schemaArrayBounds(const SchemaRef& ref) {
    const SchemaDef* def = findSchemaDef(ref.semaTy->SchemaName);
    const ArrayTypeNode* atn = def ? arrayBodyOf(def->body) : nullptr;
    if (!atn)
        codegenICE("schema '" + ref.semaTy->SchemaName
                   + "' has no array body to index");

    // The bound expressions are written in terms of the formal discriminants,
    // so bind those names to the run-time values and emit them as ordinary
    // expressions.
    pushScope();
    for (size_t i = 0; i < def->discNames.size() && i < ref.discs.size(); ++i) {
        auto* slot = createEntryAlloca(i64Ty, "disc." + def->discNames[i]);
        builder.CreateStore(ref.discs[i], slot);
        defVar(def->discNames[i], slot, i64Ty);
    }
    auto* lo = toI64(emitExpr(*atn->Low));
    auto* hi = toI64(emitExpr(*atn->High));
    popScope();
    if (!lo || !hi)
        codegenICE("schema '" + ref.semaTy->SchemaName
                   + "' has bounds that cannot be evaluated at run time");
    return {lo, hi};
}

llvm::Type* Codegen::Impl::schemaStorageType(const SchemaRef& ref) {
    const plang::Type* body = ref.semaTy->SchemaBody.get();
    if (!body || body->isError())
        codegenICE("schema '" + ref.semaTy->SchemaName + "' has no resolved body");
    if (body->Kind == TypeKind::Array && body->ElemType)
        return llvmTypeOfSemaType(*body->ElemType);
    return llvmTypeOfSemaType(*body);
}

llvm::Value* Codegen::Impl::schemaBodySize(const plang::Type& schema,
                                           const std::vector<llvm::Value*>& discs) {
    const plang::Type* body = schema.SchemaBody.get();
    if (!body || body->isError())
        codegenICE("schema '" + schema.SchemaName + "' has no resolved body");

    // EP §6.4.3.3's string schema has no declaration to walk -- it is not
    // written in the program -- and its one discriminant IS the capacity, so
    // the size is the header plus that.  Reading the probe-lowered struct here
    // asked for a string(1) and allocated 16 bytes for a `new(q, 20)`, which
    // the first assignment then wrote past.
    if (body->Kind == TypeKind::VarString && body->ExtentVaries
            && discs.size() == 1)
        return alignUpV(builder.CreateAdd(i64c(8), discs[0], "str.size"), 8);

    // Any other body whose extent a discriminant fixes is measured by walking
    // the declaration with the discriminants bound.  An ARRAY body is left to
    // the path below: it already recovered its extent from the discriminants
    // before any of this existed, and routing it through here changed nothing
    // but which code computed the same number.
    if (body->ExtentVaries && body->Kind != TypeKind::Array) {
        if (const SchemaDef* def = findSchemaDef(schema.SchemaName);
                def && def->body) {
            SchemaRef ref{&schema, nullptr, discs};
            bindSchemaDiscs(ref);
            auto* sz = rtSizeOfTypeNode(def->body);
            popScope();
            return sz;
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

    const uint64_t hdrBytes = s * 8;
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
    if (!exprIsVarStr(e) || !e.ResolvedType->ExtentVaries)
        return i64c(exprStrCap(e));

    // `q^` for a ^string: the schema's one discriminant IS the capacity.
    if (auto ref = schemaRefOf(e);
            ref && ref->semaTy && ref->semaTy->SchemaBody
            && ref->semaTy->SchemaBody->Kind == TypeKind::VarString
            && ref->discs.size() == 1)
        return ref->discs[0];

    // `p^.s` where s is `string(cap)` in a record body: the capacity is the
    // expression the field was declared with, re-emitted against the
    // discriminants this object carries.  Folding StrCapacity here would check
    // against the probe's string(1).
    if (auto* fe = llvm::dyn_cast<FieldExpr>(&e)) {
        if (auto ref = schemaRefOf(*fe->Record)) {
            // The same look-through recordTypeOf does; it is static to
            // CodegenExprs.cpp, and the schema case is the only one reachable.
            const Type* RecTy = fe->Record->ResolvedType.get();
            if (RecTy && (RecTy->Kind == TypeKind::Schema
                          || RecTy->Kind == TypeKind::SchemaInstance)
                    && RecTy->SchemaBody)
                RecTy = RecTy->SchemaBody.get();
            if (RecTy && RecTy->Kind == TypeKind::Record && RecTy->RecordDecl) {
                for (const auto& fd : RecTy->RecordDecl->Fields) {
                    if (!std::ranges::any_of(fd.Names, [&](const std::string& n) {
                            return eqCI(n, fe->Field); }))
                        continue;
                    const TypeNode* d = fd.Type.get();
                    while (auto* pk = llvm::dyn_cast_or_null<PackedTypeNode>(d))
                        d = pk->Inner.get();
                    if (auto* st = llvm::dyn_cast_or_null<StringTypeNode>(d)) {
                        bindSchemaDiscs(*ref);
                        auto* cap = toI64(emitExpr(*st->Capacity));
                        popScope();
                        if (cap) return cap;
                    }
                    break;
                }
            }
        }
    }
    return i64c(exprStrCap(e));
}

// ---------------------------------------------------------------------------
// Run-time layout
//
// EP §6.4.7: the body of a schema used without its discriminants cannot be one
// LLVM struct, because layoutOf specialises a struct per discriminant tuple and
// there is no tuple until run time.  So a body with a discriminant-fixed extent
// is laid out here instead, by walking the declaration and accumulating.
//
// What makes that affordable is that ALIGNMENT is static even when size is not:
// a string(cap) is i64-aligned for every cap, and an array is aligned as its
// element.  So an offset is alignUp(running, staticAlign) with only the running
// sum dynamic, and a subtree that reads no discriminant folds to a constant
// straight from the DataLayout -- which is also what keeps the two paths
// agreeing about the fixed fields.
// ---------------------------------------------------------------------------

namespace {
/// The denoter under any `packed`, and whether one was there.
const TypeNode* peelPacked(const TypeNode* tn, bool* wasPacked = nullptr) {
    while (auto* pk = llvm::dyn_cast_or_null<PackedTypeNode>(tn)) {
        if (wasPacked) *wasPacked = true;
        tn = pk->Inner.get();
    }
    return tn;
}
/// Whether what \p tn denotes has an extent fixed by a discriminant.  Sema
/// marks it; see Type::ExtentVaries.
bool nodeExtentVaries(const TypeNode* tn) {
    return tn && tn->ResolvedType && tn->ResolvedType->ExtentVaries;
}
} // namespace

uint64_t Codegen::Impl::rtAlignOfTypeNode(const TypeNode* tn) {
    bool packed = false;
    const TypeNode* d = peelPacked(tn, &packed);
    if (packed) return 1;
    if (!nodeExtentVaries(d))
        return mod->getDataLayout().getABITypeAlign(llvmTypeOfNode(*d)).value();
    // A varying string is { i64 len, bytes } whatever the capacity is.
    if (llvm::isa<StringTypeNode>(d)) return 8;
    if (auto* at = llvm::dyn_cast<ArrayTypeNode>(d))
        return rtAlignOfTypeNode(at->Element.get());
    if (auto* rt = llvm::dyn_cast<RecordTypeNode>(d)) {
        uint64_t a = 1;
        for (const auto& fd : rt->Fields)
            a = std::max(a, rtAlignOfTypeNode(fd.Type.get()));
        return a;
    }
    return 8;
}

llvm::Value* Codegen::Impl::alignUpV(llvm::Value* v, uint64_t align) {
    if (align <= 1) return v;
    auto* mask = i64c(static_cast<int64_t>(align - 1));
    auto* sum  = builder.CreateAdd(v, mask, "align.add");
    return builder.CreateAnd(sum, builder.CreateNot(mask), "align.up");
}

llvm::Value* Codegen::Impl::rtSizeOfTypeNode(const TypeNode* tn) {
    const TypeNode* d = peelPacked(tn);
    // Nothing in it reads a discriminant, so the static answer is the answer --
    // and using the DataLayout here is what keeps a fixed field at the offset
    // an ordinary load of it expects.
    if (!nodeExtentVaries(d))
        return i64c(static_cast<int64_t>(
            mod->getDataLayout().getTypeAllocSize(llvmTypeOfNode(*d))));

    if (auto* st = llvm::dyn_cast<StringTypeNode>(d)) {
        auto* cap = toI64(emitExpr(*st->Capacity));
        if (!cap) codegenICE("a schema string capacity that cannot be evaluated");
        return alignUpV(builder.CreateAdd(i64c(8), cap, "str.size"), 8);
    }
    if (auto* at = llvm::dyn_cast<ArrayTypeNode>(d)) {
        auto* lo = toI64(emitExpr(*at->Low));
        auto* hi = toI64(emitExpr(*at->High));
        if (!lo || !hi) codegenICE("a schema array bound that cannot be evaluated");
        auto* count = builder.CreateAdd(builder.CreateSub(hi, lo), i64c(1),
                                        "arr.count");
        count = builder.CreateSelect(
            builder.CreateICmpSLT(count, i64c(1)), i64c(1), count, "arr.count.min");
        auto* stride = alignUpV(rtSizeOfTypeNode(at->Element.get()),
                                rtAlignOfTypeNode(at->Element.get()));
        return builder.CreateMul(count, stride, "arr.size");
    }
    if (auto* rt = llvm::dyn_cast<RecordTypeNode>(d)) {
        if (rt->Variant)
            codegenICE("a schema record body with a variant part has no run-time layout");
        llvm::Value* off = i64c(0);
        for (const auto& fd : rt->Fields) {
            const uint64_t a = rt->Packed ? 1 : rtAlignOfTypeNode(fd.Type.get());
            for (size_t i = 0; i < fd.Names.size(); ++i) {
                off = alignUpV(off, a);
                off = builder.CreateAdd(off, rtSizeOfTypeNode(fd.Type.get()),
                                        "rec.off");
            }
        }
        // A record is padded to its own alignment, as a struct is, so that an
        // array of them strides correctly.
        return alignUpV(off, rt->Packed ? 1 : rtAlignOfTypeNode(d));
    }
    codegenICE("a schema body denoter with no run-time layout");
    return nullptr;
}

llvm::Value* Codegen::Impl::rtFieldOffset(const RecordTypeNode& rt,
                                          const std::string& field) {
    if (rt.Variant)
        codegenICE("a schema record body with a variant part has no run-time layout");
    // The same walk as the size above, stopped at the field.  Same walk on
    // purpose: an offset worked out one way and a size the other is how the
    // last field ends up outside the allocation.
    llvm::Value* off = i64c(0);
    for (const auto& fd : rt.Fields) {
        const uint64_t a = rt.Packed ? 1 : rtAlignOfTypeNode(fd.Type.get());
        for (const auto& nm : fd.Names) {
            off = alignUpV(off, a);
            if (eqCI(nm, field)) return off;
            off = builder.CreateAdd(off, rtSizeOfTypeNode(fd.Type.get()),
                                    "rec.off");
        }
    }
    codegenICE("record has no field named '" + field + "'");
    return nullptr;
}

const ArrayTypeNode* Codegen::Impl::varyingArrayFieldOf(const FieldExpr& fe) {
    const Type* RecTy = fe.Record->ResolvedType.get();
    if (RecTy && (RecTy->Kind == TypeKind::Schema
                  || RecTy->Kind == TypeKind::SchemaInstance) && RecTy->SchemaBody)
        RecTy = RecTy->SchemaBody.get();
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
