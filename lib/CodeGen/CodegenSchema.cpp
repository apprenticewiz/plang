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
    if (exprIsVarStr(e) && e.ResolvedType->ExtentVaries)
        if (auto ref = schemaRefOf(e);
                ref && ref->semaTy && ref->semaTy->SchemaBody
                && ref->semaTy->SchemaBody->Kind == TypeKind::VarString
                && ref->discs.size() == 1)
            return ref->discs[0];
    return i64c(exprStrCap(e));
}
