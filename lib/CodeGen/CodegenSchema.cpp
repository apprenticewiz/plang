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

/// Bytes of discriminant header in front of a schema body.
///
/// One question, one answer: emitNewSchema lays this out and schemaRefOf skips
/// it, and they were each spelling `SchemaDiscs.size() * 8` for themselves.
/// That is also the wrong number -- it aligns the body to 8 whatever the body
/// needs, so a body wanting 16 sat misaligned and the aligned vector stores
/// llvm emits at -O1 and above faulted on it.
uint64_t Codegen::Impl::schemaHeaderBytes(const plang::Type& schema) {
    const uint64_t raw = schema.SchemaDiscs.size() * 8;
    uint64_t a = 8;
    if (const TypeNode* body = schemaBodyNodeOf(schema))
        a = std::max(a, rtAlignOfTypeNode(body));
    return (raw + a - 1) / a * a;
}

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

/// The body denoter of a schema, from the type itself rather than from its
/// name.  The name-keyed map remains only for the discriminant NAMES, which a
/// declaration carries and a synthetic schema does not.
const TypeNode* Codegen::Impl::schemaBodyNodeOf(const plang::Type& T) const {
    if (T.SchemaBodyNode) return T.SchemaBodyNode;
    const SchemaDef* def = findSchemaDef(T.SchemaName);
    return def ? def->body : nullptr;
}

void Codegen::Impl::bindSchemaDiscs(const SchemaRef& ref) {
    // R3: the values a closed extent form is evaluated against.  The names
    // bound below are only for the older route, which re-emits the
    // declaration's expressions; a form needs no names at all.
    rtDiscs_ = &ref.discs;
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

// R3: evaluate a closed extent form against the discriminants this object
// carries.  There is no name in it to resolve, which is the whole point: the
// older route re-emitted the DECLARATION's bound expressions here, at the
// allocation site, where an unrelated local of the right spelling captured the
// constant they were written against.
llvm::Value* Codegen::Impl::emitExtentForm(const plang::ExtentForm& F,
                                           const std::vector<llvm::Value*>& discs) {
    using Op = plang::Type::ExtentForm::Op;
    const auto arg = [&](size_t I) { return emitExtentForm(F.Args[I], discs); };
    switch (F.Kind) {
    case Op::Const: return llvm::ConstantInt::get(i64Ty, F.Value, /*isSigned=*/true);
    case Op::Disc:
        if (F.Value < 0 || static_cast<size_t>(F.Value) >= discs.size())
            codegenICE("schema extent names discriminant "
                       + llvm::Twine(F.Value) + " of "
                       + llvm::Twine(discs.size()));
        return discs[static_cast<size_t>(F.Value)];
    case Op::Neg: return builder.CreateNeg(arg(0), "ext.neg");
    case Op::Add: return builder.CreateAdd(arg(0), arg(1), "ext.add");
    case Op::Sub: return builder.CreateSub(arg(0), arg(1), "ext.sub");
    case Op::Mul: return builder.CreateMul(arg(0), arg(1), "ext.mul");
    case Op::Div: case Op::Mod: {
        auto* L = arg(0);
        auto* R = arg(1);
        // A zero divisor in a bound is diagnosed where the expression is
        // checked; guarding here keeps the emitted code from trapping if one
        // ever reaches this far.
        auto* Safe = builder.CreateSelect(
            builder.CreateICmpEQ(R, llvm::ConstantInt::get(i64Ty, 0)), llvm::ConstantInt::get(i64Ty, 1), R, "ext.div.safe");
        return F.Kind == Op::Div ? builder.CreateSDiv(L, Safe, "ext.div")
                                 : builder.CreateSRem(L, Safe, "ext.mod");
    }
    case Op::Pow: {
        // EP §6.8.3.2 with an integer base: a small loop rather than a call,
        // and it folds away entirely when both sides are constants.
        auto* fn = getExternFnN("plang_ipow", i64Ty, {i64Ty, i64Ty});
        return builder.CreateCall(fn, {arg(0), arg(1)}, "ext.pow");
    }
    }
    codegenICE("a schema extent form with no case");
    return nullptr;
}

std::pair<llvm::Value*, llvm::Value*>
Codegen::Impl::schemaArrayBounds(const SchemaRef& ref) {
    // R3: the closed forms, where Sema could build them.
    if (ref.semaTy && ref.semaTy->SchemaLowForm && ref.semaTy->SchemaHighForm)
        return {emitExtentForm(*ref.semaTy->SchemaLowForm,  ref.discs),
                emitExtentForm(*ref.semaTy->SchemaHighForm, ref.discs)};

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
    // The bounds are emitted with only the discriminants visible.  Without
    // this the allocating procedure's own variables shadowed the constants the
    // body was written against.
    llvm::Value *lo = nullptr, *hi = nullptr;
    {
        DeclarationScopeOnly declOnly(*this);
        lo = toI64(emitExpr(*atn->Low));
        hi = toI64(emitExpr(*atn->High));
    }
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
            bindSchemaDiscs(ref);
            auto* sz = rtSizeOfTypeNode(bodyNode);
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

/// The capacity of an already-resolved path, so a caller that has one need not
/// resolve it again.  Resolving twice re-emits every subscript along the way,
/// which for `q^.a[next].s := v` called `next` once for the address and once
/// for the capacity.
llvm::Value* Codegen::Impl::strCapFromPath(const SchemaPath& path) {
    auto* st = llvm::dyn_cast_or_null<StringTypeNode>(path.decl);
    if (!st) return nullptr;
    bindSchemaDiscs(path.root);
    auto* cap = toI64(emitExpr(*st->Capacity));
    popScope();
    return cap;
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
    // `packed` is peeled and then IGNORED here, because llvmTypeOfNode ignores
    // it too: a PackedTypeNode wrapper lowers to its inner type unchanged, so
    // `packed array[1..3] of integer` is [3 x i64] and aligned like one.
    // Answering 1 for it made this walk disagree with the layout it has to
    // reproduce.  Where `packed` really does pack is a RECORD, below.
    const TypeNode* d = peelPacked(tn);
    // Alignment is static even where size is not -- a string(cap) is
    // i64-aligned for every cap, and an array is aligned as its element is --
    // so the three denoters that can hold a varying extent are answered
    // STRUCTURALLY, without asking for a type they may not have.
    //
    // Not from the node's annotation, which is what this used to consult.  One
    // declaration serves every instantiation and carries whichever was resolved
    // last, so inside a run-time layout walk that annotation is some other
    // instance's and not this one's.  Nothing here reads it any more.
    if (llvm::isa<StringTypeNode>(d)) return 8;
    if (auto* at = llvm::dyn_cast<ArrayTypeNode>(d))
        return rtAlignOfTypeNode(at->Element.get());
    if (auto* rt = llvm::dyn_cast<RecordTypeNode>(d)) {
        // `packed record ... end` is a RecordTypeNode carrying Packed, NOT a
        // PackedTypeNode wrapper -- the parser only wraps `packed <name>`.  So
        // the peel above never saw it, this returned the widest member's
        // alignment, and layoutOf built a StructType with packed=true and
        // alignment 1 for the very same declaration.  rtSizeOfTypeNode has
        // always honoured rt->Packed for the SIZE, so the file disagreed with
        // itself about one flag.
        if (rt->Packed) return 1;
        uint64_t a = 1;
        for (const auto& fd : rt->Fields)
            a = std::max(a, rtAlignOfTypeNode(fd.Type.get()));
        // The variant part counts too.  Omitting it under-aligned a record
        // whose only strictly-aligned member is inside a variant -- the size
        // walk pads to this alignment, so the two disagreed about where the
        // record ends as well as about where it may start.
        if (rt->Variant) a = std::max(a, rtVariantAlign(*rt->Variant));
        return a;
    }
    // A nested instantiation is aligned as its body is; the discriminants
    // change its size and not its alignment.
    if (auto* sn = llvm::dyn_cast<SchemaTypeNode>(d); sn && sn->ResolvedBody)
        if (const TypeNode* body = schemaBodyNodeOf(*sn->ResolvedBody))
            return rtAlignOfTypeNode(body);
    // Everything else -- a name, a subrange, an enumeration, a set, a file --
    // has no extent written in it and is aligned as the DataLayout says.
    return mod->getDataLayout().getABITypeAlign(llvmTypeOfNode(*d)).value();
}

llvm::Value* Codegen::Impl::alignUpV(llvm::Value* v, uint64_t align) {
    if (align <= 1) return v;
    auto* mask = i64c(static_cast<int64_t>(align - 1));
    auto* sum  = builder.CreateAdd(v, mask, "align.add");
    return builder.CreateAnd(sum, builder.CreateNot(mask), "align.up");
}

std::optional<std::pair<llvm::Value*, llvm::Value*>>
Codegen::Impl::rtIndexBounds(const ArrayTypeNode& at) {
    // Bounds written as expressions may read a discriminant, so they are
    // emitted against whatever is bound now rather than folded.
    // R3: the closed forms first.  They name the discriminants by index and
    // hold no identifier, so nothing in the scope doing the allocating can
    // capture anything; re-emitting at.Low/at.High below resolves their names
    // here, which is the defect 0.1.6 shipped a scope barrier to guard.
    if (auto* lo = extentOf(at.ExtentLow))
        if (auto* hi = extentOf(at.ExtentHigh))
            return std::pair{lo, hi};
    if (at.Low && at.High) {
        auto* lo = toI64(emitExpr(*at.Low));
        auto* hi = toI64(emitExpr(*at.High));
        if (!lo || !hi) return std::nullopt;
        return std::pair{lo, hi};
    }
    // ISO §6.4.3.2: an index named by its ordinal type has no bound
    // expressions at all -- `array[colour]` leaves Low and High null, and
    // dereferencing them here is what crashed the compiler.  The extent is the
    // whole of that type and cannot vary, so ask the same helper the static
    // layout asks.  One question, one answer, and the two walks agree by
    // construction rather than by my having written the arithmetic twice.
    if (auto r = arrayIndexRange(at))
        return std::pair<llvm::Value*, llvm::Value*>{i64c(r->first),
                                                     i64c(r->second)};
    return std::nullopt;
}

llvm::Value* Codegen::Impl::rtSizeOfTypeNode(const TypeNode* tn) {
    const TypeNode* d = peelPacked(tn);
    // This used to ask the node whether its extent varies and take the
    // DataLayout's answer when it said no.  That question has no reliable
    // answer here: one declaration serves every instantiation and carries the
    // annotation of whichever Sema resolved last, so in a program with both
    // `^t` and `t(20)` the probe walk read the INSTANCE's `string(20)` field,
    // decided it was fixed, and sized it from syntax the discriminants are not
    // bound in -- 264 bytes for a 32-byte field.  The record's own fields then
    // sat past the end of it and a whole-value copy read 272 bytes out of 40.
    //
    // So the syntax is read wherever the syntax is where the extent is written,
    // with the discriminants bound.  A fixed extent emits its constant and
    // folds to exactly what the DataLayout would have said, so this agrees with
    // the static layout for everything that has one, and is simply the answer
    // for everything that does not.

    // A subrange is as wide as its host ordinal whatever its bounds are, so a
    // discriminant in them changes the CHECK and not the storage.
    if (llvm::isa<SubrangeTypeNode>(d))
        return i64c(static_cast<int64_t>(
            mod->getDataLayout().getTypeAllocSize(llvmTypeOfNode(*d))));
    if (auto* st = llvm::dyn_cast<StringTypeNode>(d)) {
        // R3: the capacity's closed form.  The expression fallback that used
        // to sit here is gone: replacing it with an internal error and running
        // the suite showed it taken by NO test, while the array-bound fallback
        // beside it is taken by 45 -- so this one was dead and that one is not.
        //
        // Deleting it rather than leaving it is the point of the phase.  What
        // it did was re-emit the declaration's capacity expression at the use
        // site, which is the defect this work exists to remove; leaving a
        // second path that can still do it means the class is bypassed rather
        // than gone.  A capacity that reaches here without a form is a hole in
        // the compiler, and says so.
        auto* cap = extentOf(st->ExtentLow);
        if (!cap)
            codegenICE("a schema string capacity with no closed form to "
                       "evaluate against the discriminants");
        if (!cap) codegenICE("a schema string capacity that cannot be evaluated");
        return alignUpV(builder.CreateAdd(i64c(8), cap, "str.size"), 8);
    }
    if (auto* at = llvm::dyn_cast<ArrayTypeNode>(d)) {
        auto bounds = rtIndexBounds(*at);
        if (!bounds) codegenICE("a schema array bound that cannot be evaluated");
        auto* count = builder.CreateAdd(
            builder.CreateSub(bounds->second, bounds->first), i64c(1),
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
    // A denoter with no extent written in it -- a name, an enumeration, a set,
    // a file.  Nothing in one of those can depend on a discriminant, so the
    // static answer is the answer.  A denoter that SAYS it varies and still
    // reached here is a node kind whose extent nothing knows how to recover,
    // which is an internal error rather than a size: keeping that loud is the
    // whole reason this is a check and not a fallthrough.
    // R3: a schema instantiated INSIDE another schema's body.  Its own
    // discriminants are arithmetic over the enclosing ones -- the standard's
    // own `matrix(m,n) = array[1..m] of vector(n)` is exactly this -- so they
    // are evaluated first and the inner body is then walked against them.
    // Without this the instantiation was laid out from the probe, and the
    // allocation came out one element wide in every instance.
    if (auto* sn = llvm::dyn_cast<SchemaTypeNode>(d);
            sn && !sn->ActualForms.empty() && rtDiscs_ && sn->ResolvedBody) {
        std::vector<llvm::Value*> inner;
        inner.reserve(sn->ActualForms.size());
        for (const auto& F : sn->ActualForms)
            inner.push_back(emitExtentForm(F, *rtDiscs_));
        if (const TypeNode* body = schemaBodyNodeOf(*sn->ResolvedBody)) {
            // Bound as a schema in its own right: the inner body's extents are
            // forms over ITS discriminants, and where a form could not be built
            // the fallback needs the inner names in scope.  Resolving the inner
            // body happens while the OUTER probe is in force, so its bounds are
            // never forms over the outer names -- which is why binding, rather
            // than only swapping the values, is what this needs.
            const auto* saved = rtDiscs_;
            SchemaRef iref{sn->ResolvedBody.get(), nullptr, inner};
            bindSchemaDiscs(iref);
            auto* sz = rtSizeOfTypeNode(body);
            popScope();
            rtDiscs_ = saved;
            return sz;
        }
    }
    if (nodeExtentVaries(d))
        codegenICE("a schema body denoter with no run-time layout");
    return i64c(static_cast<int64_t>(
        mod->getDataLayout().getTypeAllocSize(llvmTypeOfNode(*d))));
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
                                          llvm::Value* off, bool packed,
                                          bool nested) {
    // ISO §6.4.3.3 makes the tag-field OPTIONAL: `case boolean of` selects on a
    // type with no field to store it in.  This gated on TagType alone while the
    // static layout gates on the field having a NAME, so a tagless selector
    // reserved eight bytes for a tag that does not exist and put every
    // alternative that far past where an ordinary access looks.  Three lines
    // below, the field-name match already tests TagField.empty(); the storage
    // three lines above did not.
    if (vp.TagType && !vp.TagField.empty()) {
        const uint64_t a = packed ? 1 : rtAlignOfTypeNode(vp.TagType.get());
        off = alignUpV(off, a);
        off = builder.CreateAdd(off, rtSizeOfTypeNode(vp.TagType.get()), "tag.off");
    }
    // Only the OUTERMOST run is pre-aligned, because in the static layout only
    // the outermost run is a struct element of its own and so gets the part's
    // alignment from LLVM.  A nested part lives inside that element, where
    // layoutVariantCase places its fields by their own alignment and nothing
    // else -- so pre-aligning a nested run here put `k` four bytes past where
    // an ordinary read of it looked, and a whole-value copy between `q^` and a
    // discriminated instance quietly swapped a character for a space.
    if (!nested) off = alignUpV(off, packed ? 1 : rtVariantAlign(vp));
    // Walked from off rather than from zero and added on afterwards.  Those two
    // are the same number only when off is already aligned to the widest field
    // in the part, which is exactly what the pre-align used to guarantee and
    // what a nested run does not have.  rtVariantFieldOffset has always walked
    // absolutely; now this does too, so the size and the offset are one walk
    // and cannot drift apart again.
    llvm::Value* widest = off;
    for (const auto& vc : vp.Cases) {
        llvm::Value* sz = rtWalkFields(vc.Fields, off, packed, nullptr, nullptr);
        if (vc.NestedVariant)
            sz = rtVariantSize(*vc.NestedVariant, sz, packed, /*nested=*/true);
        widest = builder.CreateSelect(builder.CreateICmpUGT(sz, widest),
                                      sz, widest, "variant.max");
    }
    return widest;
}

uint64_t Codegen::Impl::rtVariantAlign(const VariantPart& vp) {
    // The tag is a member of the part as much as any alternative's field is,
    // and the size walk aligns to it, so leaving it out here made the two
    // walks disagree for a variant whose tag is wider than its alternatives.
    // A tag with no field name occupies nothing, so it constrains nothing.
    uint64_t a = (vp.TagType && !vp.TagField.empty())
                     ? rtAlignOfTypeNode(vp.TagType.get()) : 1;
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
                                                 const std::string& field,
                                                 bool nested) {
    // ISO §6.4.3.3 makes the tag-field OPTIONAL: `case boolean of` selects on a
    // type with no field to store it in.  This gated on TagType alone while the
    // static layout gates on the field having a NAME, so a tagless selector
    // reserved eight bytes for a tag that does not exist and put every
    // alternative that far past where an ordinary access looks.  Three lines
    // below, the field-name match already tests TagField.empty(); the storage
    // three lines above did not.
    if (vp.TagType && !vp.TagField.empty()) {
        const uint64_t a = packed ? 1 : rtAlignOfTypeNode(vp.TagType.get());
        off = alignUpV(off, a);
        if (!vp.TagField.empty() && eqCI(vp.TagField, field)) return off;
        off = builder.CreateAdd(off, rtSizeOfTypeNode(vp.TagType.get()), "tag.off");
    }
    // Outermost only; see rtVariantSize.
    if (!nested) off = alignUpV(off, packed ? 1 : rtVariantAlign(vp));
    for (const auto& vc : vp.Cases) {
        bool found = false;
        llvm::Value* v = rtWalkFields(vc.Fields, off, packed, &field, &found);
        if (found) return v;
        if (vc.NestedVariant) {
            llvm::Value* end = rtWalkFields(vc.Fields, off, packed, nullptr, nullptr);
            if (auto* n = rtVariantFieldOffset(*vc.NestedVariant, end, packed,
                                               field, /*nested=*/true))
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
        return SchemaPath{*ref, ref->data, bodyNode};
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
        // R3: the component may itself be a schema INSTANTIATION whose
        // discriminants are arithmetic over the enclosing ones -- indexing
        // `q^[i][j]` for `matrix(m,n) = array[1..m] of vector(n)` lands here
        // with a SchemaTypeNode where an array was wanted.  Evaluate that
        // instantiation's discriminants from the outer ones and carry on
        // inside it; the bounds and the stride are then its own, not the
        // probe's, which is what made the inner index check read 1..1.
        SchemaRef       root = base->root;
        const TypeNode* decl = peel(base->decl);
        if (auto* sn = llvm::dyn_cast_or_null<SchemaTypeNode>(decl);
                sn && !sn->ActualForms.empty() && sn->ResolvedBody) {
            std::vector<llvm::Value*> inner;
            inner.reserve(sn->ActualForms.size());
            for (const auto& F : sn->ActualForms)
                inner.push_back(emitExtentForm(F, base->root.discs));
            if (const TypeNode* body = schemaBodyNodeOf(*sn->ResolvedBody)) {
                decl = peel(body);
                root = SchemaRef{sn->ResolvedBody.get(), base->addr, inner};
            }
        }
        auto* at = llvm::dyn_cast_or_null<ArrayTypeNode>(decl);
        if (!at) return std::nullopt;
        bindSchemaDiscs(root);
        auto bounds = rtIndexBounds(*at);
        if (!bounds) { popScope(); return std::nullopt; }
        auto* lo     = bounds->first;
        auto* hi     = bounds->second;
        auto* stride = alignUpV(rtSizeOfTypeNode(at->Element.get()),
                                rtAlignOfTypeNode(at->Element.get()));
        popScope();
        auto* idx = toI64(emitExpr(*ie->Index));
        emitRangeCheckDyn(idx, lo, hi, /*isIndex=*/true, ie->Loc);
        auto* off = builder.CreateMul(builder.CreateSub(idx, lo), stride,
                                      "path.idx");
        return SchemaPath{root,
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
