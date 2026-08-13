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

    // EP §6.7.5.3 lets the discriminants be expressions, so nothing before now
    // can tell that one is unusable.  An extent of zero or less sizes the
    // allocation from nonsense and puts every later access outside it, and it
    // was accepted silently.  Only a discriminant that actually fixes an extent
    // is checked: one used as a range bound may legitimately be anything.
    if (schema.SchemaBody && schema.SchemaBody->ExtentVaries)
        for (size_t i = 0; i < s; ++i) {
            auto* bad = builder.CreateICmpSLT(discs[i], i64c(1), "sch.extent.bad");
            if (auto* c = llvm::dyn_cast<llvm::ConstantInt>(bad); c && c->isZero())
                continue;   // a constant that is plainly fine costs nothing
            auto* nm = internStrPtr(i < schema.SchemaDiscs.size()
                                        ? schema.SchemaDiscs[i].Name : "?");
            emitGuard(bad, "schema.extent", [&] {
                builder.CreateCall(
                    getExternFnN("plang_err_schema_extent",
                                 llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty}),
                    {nm, discs[i]});
            });
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

    // A `with`-bound field carries the capacity recorded when it was bound;
    // by then it is an ordinary name with no path back to its object.
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e))
        if (const auto* ve = findVar(id->Name); ve && ve->strCapV)
            return ve->strCapV;

    // `q^` for a ^string: EP §6.4.3.3 makes the schema's one discriminant the
    // capacity, and it has no written declaration to walk.
    if (llvm::isa<DerefExpr>(&e))
        if (auto ref = schemaRefOf(e);
                ref && ref->semaTy && ref->semaTy->SchemaBody
                && ref->semaTy->SchemaBody->Kind == TypeKind::VarString
                && ref->discs.size() == 1)
            return ref->discs[0];

    // Anywhere else: the capacity is the expression the component was DECLARED
    // with, re-emitted against the discriminants its object carries.  Asked of
    // the PATH, so a string reached through a nested record or an array element
    // answers as readily as a direct field -- recognising only the shapes `q^`
    // and `p^.s` is what left every deeper one folding the probe's string(1).
    if (auto path = schemaPathOf(e))
        if (auto* st = llvm::dyn_cast_or_null<StringTypeNode>(path->decl)) {
            bindSchemaDiscs(path->root);
            auto* cap = toI64(emitExpr(*st->Capacity));
            popScope();
            if (cap) return cap;
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
        llvm::Value* off = rtWalkFields(rt->Fields, i64c(0), rt->Packed,
                                        /*stopAt=*/nullptr, nullptr);
        if (rt->Variant)
            off = rtVariantSize(*rt->Variant, off, rt->Packed);
        // A record is padded to its own alignment, as a struct is, so that an
        // array of them strides correctly.
        return alignUpV(off, rt->Packed ? 1 : rtAlignOfTypeNode(d));
    }
    codegenICE("a schema body denoter with no run-time layout");
    return nullptr;
}

/// Walk \p fields accumulating from \p off.  With \p stopAt set, returns the
/// offset of that field and sets *found; otherwise returns the offset one past
/// the last.  Size and offset come from ONE walk on purpose: worked out
/// separately, they drift, and that is how a field ends up outside the
/// allocation.
llvm::Value* Codegen::Impl::rtWalkFields(const std::vector<FieldDecl>& fields,
                                         llvm::Value* off, bool packed,
                                         const std::string* stopAt, bool* found) {
    for (const auto& fd : fields) {
        const uint64_t a = packed ? 1 : rtAlignOfTypeNode(fd.Type.get());
        for (const auto& nm : fd.Names) {
            off = alignUpV(off, a);
            if (stopAt && eqCI(nm, *stopAt)) { if (found) *found = true; return off; }
            off = builder.CreateAdd(off, rtSizeOfTypeNode(fd.Type.get()),
                                    "rec.off");
        }
    }
    return off;
}

/// §6.4.3.3: the alternatives of a variant part share one run of storage, so
/// the part is as big as the largest of them -- a max taken at run time here,
/// since an alternative's own size may depend on a discriminant.  The tag, if
/// there is one, is an ordinary field ahead of that run.
llvm::Value* Codegen::Impl::rtVariantSize(const VariantPart& vp,
                                          llvm::Value* off, bool packed) {
    if (vp.TagType) {
        const uint64_t a = packed ? 1 : rtAlignOfTypeNode(vp.TagType.get());
        off = alignUpV(off, a);
        off = builder.CreateAdd(off, rtSizeOfTypeNode(vp.TagType.get()), "tag.off");
    }
    off = alignUpV(off, packed ? 1 : rtVariantAlign(vp));
    llvm::Value* widest = i64c(0);
    for (const auto& vc : vp.Cases) {
        llvm::Value* sz = rtWalkFields(vc.Fields, i64c(0), packed, nullptr, nullptr);
        if (vc.NestedVariant) sz = rtVariantSize(*vc.NestedVariant, sz, packed);
        widest = builder.CreateSelect(builder.CreateICmpUGT(sz, widest),
                                      sz, widest, "variant.max");
    }
    return builder.CreateAdd(off, widest, "variant.end");
}

uint64_t Codegen::Impl::rtVariantAlign(const VariantPart& vp) {
    uint64_t a = 1;
    for (const auto& vc : vp.Cases) {
        for (const auto& fd : vc.Fields)
            a = std::max(a, rtAlignOfTypeNode(fd.Type.get()));
        if (vc.NestedVariant) a = std::max(a, rtVariantAlign(*vc.NestedVariant));
    }
    return a;
}

llvm::Value* Codegen::Impl::rtFieldOffset(const RecordTypeNode& rt,
                                          const std::string& field) {
    bool found = false;
    llvm::Value* off = rtWalkFields(rt.Fields, i64c(0), rt.Packed, &field, &found);
    if (found) return off;
    if (rt.Variant) {
        if (auto* v = rtVariantFieldOffset(*rt.Variant, off, rt.Packed, field))
            return v;
    }
    codegenICE("record has no field named '" + field + "'");
    return nullptr;
}

/// The offset of \p field within a variant part, or null if it is not in one.
/// Every alternative starts at the same place, which is what sharing storage
/// means, so the alternative the field is in is the only one walked.
llvm::Value* Codegen::Impl::rtVariantFieldOffset(const VariantPart& vp,
                                                 llvm::Value* off, bool packed,
                                                 const std::string& field) {
    if (vp.TagType) {
        const uint64_t a = packed ? 1 : rtAlignOfTypeNode(vp.TagType.get());
        off = alignUpV(off, a);
        if (!vp.TagField.empty() && eqCI(vp.TagField, field)) return off;
        off = builder.CreateAdd(off, rtSizeOfTypeNode(vp.TagType.get()), "tag.off");
    }
    off = alignUpV(off, packed ? 1 : rtVariantAlign(vp));
    for (const auto& vc : vp.Cases) {
        bool found = false;
        llvm::Value* v = rtWalkFields(vc.Fields, off, packed, &field, &found);
        if (found) return v;
        if (vc.NestedVariant) {
            llvm::Value* end = rtWalkFields(vc.Fields, off, packed, nullptr, nullptr);
            if (auto* n = rtVariantFieldOffset(*vc.NestedVariant, end, packed, field))
                return n;
        }
    }
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

std::optional<Codegen::Impl::SchemaPath>
Codegen::Impl::schemaPathOf(const ExprNode& e) {
    // Root: the object itself, whose header carries the discriminants.
    if (llvm::isa<DerefExpr>(&e) || llvm::isa<IdentExpr>(&e)) {
        auto ref = schemaRefOf(e);
        if (!ref || !ref->semaTy) return std::nullopt;
        const SchemaDef* def = findSchemaDef(ref->semaTy->SchemaName);
        if (!def || !def->body) return std::nullopt;
        return SchemaPath{*ref, ref->data, def->body};
    }

    if (auto* fe = llvm::dyn_cast<FieldExpr>(&e)) {
        auto base = schemaPathOf(*fe->Record);
        if (!base) return std::nullopt;
        auto* rt = llvm::dyn_cast_or_null<RecordTypeNode>(peel(base->decl));
        if (!rt) return std::nullopt;
        bindSchemaDiscs(base->root);
        auto* off = rtFieldOffset(*rt, fe->Field);
        popScope();
        const TypeNode* fieldDecl = fieldDenoterOf(*rt, fe->Field);
        return SchemaPath{base->root,
                          builder.CreateGEP(i8Ty, base->addr, {off}, "path.fld"),
                          fieldDecl};
    }

    if (auto* ie = llvm::dyn_cast<IndexExpr>(&e)) {
        auto base = schemaPathOf(*ie->Array);
        if (!base) return std::nullopt;
        auto* at = llvm::dyn_cast_or_null<ArrayTypeNode>(peel(base->decl));
        if (!at) return std::nullopt;
        bindSchemaDiscs(base->root);
        auto* lo     = toI64(emitExpr(*at->Low));
        auto* hi     = toI64(emitExpr(*at->High));
        auto* stride = alignUpV(rtSizeOfTypeNode(at->Element.get()),
                                rtAlignOfTypeNode(at->Element.get()));
        popScope();
        auto* idx = toI64(emitExpr(*ie->Index));
        emitRangeCheckDyn(idx, lo, hi, /*isIndex=*/true, ie->Loc);
        auto* off = builder.CreateMul(builder.CreateSub(idx, lo), stride,
                                      "path.idx");
        return SchemaPath{base->root,
                          builder.CreateGEP(i8Ty, base->addr, {off}, "path.elem"),
                          at->Element.get()};
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
