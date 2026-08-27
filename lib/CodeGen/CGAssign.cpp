#include "CGAssign.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"

using namespace plang;

void CGAssign::emitAssign(const AssignStmt& s) {
    // EP §6.4.7: a whole schematic variable copies its body, whose length only
    // the discriminants know.  It has to run before emitLValue because for p^
    // the body starts past the discriminant header.
    //
    // EITHER side may be the undiscriminated one.  Only the target was handled
    // here, so both halves of the pair took the compiler down: `v := q^` fell
    // through to the ordinary path and asked for the LLVM type of a schema,
    // which by construction has none, and `q^ := v` reached for run-time
    // discriminants that a discriminated instance does not carry.  Both are
    // legal EP.
    {
        const plang::Type* tt = s.Target->ResolvedType.get();
        const plang::Type* vt = s.Value->ResolvedType.get();
        auto isSchema   = [](const plang::Type* T) {
            return T && T->Kind == TypeKind::Schema; };
        auto isInstance = [](const plang::Type* T) {
            return T && T->Kind == TypeKind::SchemaInstance; };

        // A schema whose body is a STRING is a string, and assigning to one is
        // a string store rather than a whole-body copy: the target may be a
        // `var s: string` formal, whose capacity arrives with the actual, and
        // the value may be a literal that is not schematic at all.  Sending it
        // here produced "assignment between schematic variables that codegen
        // cannot locate" for `s := 'zz'` -- the string branch further down is
        // the one that can do it.
        const bool targetIsString = ExprIsVarStr(*s.Target);

        // Two discriminated instances are ordinary values with a static layout
        // and keep the ordinary path; this is only for a pair where at least
        // one side knows its discriminants no earlier than run time.
        if (!targetIsString && (isSchema(tt) || (isInstance(tt) && isSchema(vt)))) {
            if (!isSchema(vt) && !isInstance(vt))
                codegenICE("assignment between schematic variables that codegen "
                           "cannot locate");
            // The undiscriminated side names the schema and carries the
            // discriminant NAMES; a discriminated instance knows the VALUES at
            // compile time.  schemaActual answers for both shapes, so neither
            // side has to know which the other is -- and once the two agree,
            // either one sizes the copy.
            const plang::Type& schemaTy = isSchema(tt) ? *tt : *vt;
            const auto n = static_cast<unsigned>(schemaTy.SchemaDiscs.size());
            auto [dstData, dstDiscs] = Schema.schemaActual(*s.Target, n);
            auto [srcData, srcDiscs] = Schema.schemaActual(*s.Value,  n);
            SchemaAccess::SchemaRef dst{&schemaTy, dstData, dstDiscs};
            SchemaAccess::SchemaRef src{&schemaTy, srcData, srcDiscs};
            Schema.emitSchemaDiscMatch(dst, src);
            B.CreateMemCpy(dstData, llvm::MaybeAlign(),
                                 srcData, llvm::MaybeAlign(),
                                 Schema.schemaBodySize(schemaTy, dstDiscs));
            return;
        }
    }

    // EP §6.4.6(f): a string-type value assigned to a char VARIABLE takes
    // that value's first (only, if it is well-formed) character -- Sema's
    // isAssignCompatible now allows this direction, and the ordinary scalar
    // path below cannot: emitExpr for a VarString returns the STRUCT'S
    // ADDRESS, not a loaded value, since every other caller wants a pointer
    // for the string runtime, so falling through would have stored a pointer
    // where a char belongs.
    if (s.Target->ResolvedType && s.Target->ResolvedType->Kind == TypeKind::Char
            && (ExprIsVarStr(*s.Value) || ExprIsCharStr(*s.Value)
                || llvm::isa<StringLitExpr>(s.Value.get()))) {
        llvm::Value* dataPtr = nullptr;
        if (ExprIsVarStr(*s.Value)) {
            auto [addr, cap] = Schema.strAddrAndCap(*s.Value);
            dataPtr = addr ? Strings.strDataPtr(addr) : nullptr;
            // EP §6.4.6(f): a string value is assignment-compatible with a
            // char VARIABLE only when its length is exactly 1 -- char's own
            // capacity, per §6.4.3.3.1.  Sema's isAssignCompatible allows the
            // assignment on the strength of the TYPE alone (see
            // SemaExpr.cpp), because unlike a char-array source, whose
            // length the declared array size already answers at compile
            // time, a string(n)'s length is a mutable run-time field of the
            // value -- `s := 'xy'` on a string(5) leaves it at 2, not 1 --
            // so this is the one check no earlier phase could make.
            if (addr) {
                auto* len = Strings.strLoadLen(addr);
                auto* bad = B.CreateICmpNE(len, i64c(1), "charofstr.len.bad");
                RangeGuards.emitGuard(bad, "charofstr", [&] {
                    B.CreateCall(
                        Strings.getStrFn("plang_err_str_length",
                                         llvm::Type::getVoidTy(Ctx), {I64Ty, I64Ty}),
                        {len, i64c(1)});
                });
            }
        } else if (ExprIsCharStr(*s.Value)) {
            dataPtr = EmitLValue(*s.Value);
        } else {
            dataPtr = Strings.internStrPtr(llvm::cast<StringLitExpr>(*s.Value).Value);
        }
        if (!dataPtr) codegenICE("string value assigned to a char has no address");
        auto* ch = B.CreateLoad(I8Ty, dataPtr, "char.of.str");
        auto* dstAddr = EmitLValue(*s.Target);
        if (!dstAddr) codegenICE("assignment target is not addressable");
        B.CreateStore(ch, dstAddr);
        return;
    }

    // EP §6.5.6: assigning to a substring replaces those characters and leaves
    // the rest of the string as it was, so it cannot go through the ordinary
    // string store, which would replace the whole value.
    if (auto* sub = llvm::dyn_cast<SubstringExpr>(s.Target.get())) {
        // R5: address and capacity from ONE walk of the destination's access
        // path, or every subscript on the way to it is emitted twice.
        auto [dst, dstCap] = Schema.strAddrAndCap(*sub->Str);
        if (!dst) codegenICE("assignment to a substring of a non-addressable string");
        // Sizing a temporary needs a constant; what the runtime is told about
        // the destination is the capacity it really has.
        const int64_t cap = ExprStrCapStatic(*sub->Str);
        auto* low  = ToI64(EmitExpr(*sub->Low));
        auto* high = ToI64(EmitExpr(*sub->High));
        auto* n    = B.CreateAdd(
            B.CreateSub(high, low, "substr.span"),
            llvm::ConstantInt::get(I64Ty, 1), "substr.len");
        // The value can be written any way a string value can, so it is built
        // where a string belongs and then copied in.
        auto* src = CreateEntryAlloca(Types.strStructType(cap), "substr.src");
        StrCall.emitStrStore(src, i64c(cap), *s.Value);
        auto* capV = dstCap;
        B.CreateCall(
            Strings.getStrFn("plang_str_substr_assign", llvm::Type::getVoidTy(Ctx),
                     {PtrTy, I64Ty, I64Ty, I64Ty, PtrTy, I64Ty}),
            {dst, capV, low, n, src, capV});
        return;
    }

    // EP §6.4.7: a string whose capacity a discriminant fixes needs both an
    // address and a capacity, and each of emitLValue and exprStrCapV resolves
    // the access path from scratch -- so every subscript along the way was
    // emitted twice, and a side-effecting one in `q^.a[next].s := v` ran twice.
    // One walk, both answers.
    if (ExprIsVarStr(*s.Target) && s.Target->ResolvedType->ExtentVaries)
        if (auto path = Schema.schemaPathOf(*s.Target))
            if (auto* cap = Schema.strCapFromPath(*path)) {
                StrCall.emitStrStore(path->addr, cap, *s.Value);
                return;
            }

    // EP §6.4.7: a whole-value copy of a COMPONENT whose extent a discriminant
    // fixes is as big as the INSTANCE says, not as big as the probe's type
    // says.  Falling through to the ordinary typed store took dstTy from the
    // probe-resolved annotation, so
    //
    //     type r(lo: integer) = record a: array[lo..3] of integer; k: integer end;
    //     new(p, 3); new(q, 3);  q^.a := p^.a
    //
    // loaded and stored [3 x i64] -- the probe's lo=1 -- into a one-element
    // array, writing 8 bytes past a 24-byte allocation and over-reading the
    // source by 16.  glibc aborts.  With the probe count SMALLER it silently
    // under-copies instead, which no value oracle sees.
    //
    // The whole-object case above already memcpy'd a run-time size; this is the
    // same statement one component down, and it had no branch of its own.
    // A component whose type is a schema INSTANTIATION -- `e: ent(n)` inside
    // `t(n)` -- is as much a run-time-laid-out aggregate as an array or record
    // is, and asking only for those two kinds sent `q^.e := p^.e` to the
    // ordinary typed store, which copied the probe's ent(1): sixteen bytes of a
    // nine-element record, silently, exit 0.
    //
    // Unlike the whole-object case, this branch sized the memcpy from the
    // TARGET's discriminants alone and applied it to the source address with
    // no check the two actually agree -- `r^.x := q^.x` with r sized from
    // n=100000000 and q from n=1 read gigabytes past q's one-element
    // allocation.  dpath->root/spath->root are each the enclosing schema
    // instance's own SchemaRef (per schemaPathOf's descent, the SAME schema
    // type on both sides whenever this branch is even reached), so the same
    // emitSchemaDiscMatch call the whole-object case already makes catches it
    // here too.
    if (const auto& tt = s.Target->ResolvedType;
            tt && tt->ExtentVaries
            && (tt->Kind == TypeKind::Array || tt->Kind == TypeKind::Record
                || tt->Kind == TypeKind::SchemaInstance
                || tt->Kind == TypeKind::Schema))
        if (auto dpath = Schema.schemaPathOf(*s.Target))
            if (auto spath = Schema.schemaPathOf(*s.Value)) {
                Schema.emitSchemaDiscMatch(dpath->root, spath->root);
                llvm::Value* sz = nullptr;
                {
                    SchemaLayoutEngine::RtDiscScope disc(SchemaLayout, dpath->root.discs);
                    sz = SchemaLayout.rtSizeOfTypeNode(dpath->decl);
                }
                B.CreateMemCpy(dpath->addr, llvm::MaybeAlign(),
                                     spath->addr, llvm::MaybeAlign(), sz);
                return;
            }

    auto* addr = EmitLValue(*s.Target);
    if (!addr) codegenICE("assignment to a non-addressable target");

    // EP VarString assignment — dispatch on the Sema-annotated types.
    if (ExprIsVarStr(*s.Target)) {
        StrCall.emitStrStore(addr, Schema.exprStrCapV(*s.Target), *s.Value);
        return;
    }

    // ISO §6.4.3.2: a packed array[1..n] of char takes a string value, which
    // may be a literal or a string(n) and so is not an array to load and store.
    if (ExprIsCharStr(*s.Target)) {
        StrCall.emitCharStrStore(addr, ExprCharStrLen(*s.Target), *s.Value);
        return;
    }

    auto* rhs = EmitExpr(*s.Value);
    if (!rhs) codegenICE("assignment from an unlowerable expression");

    // EP §6.8.7: structured value constructor for arrays/records returns a ptr
    // to a temporary alloca — use memcpy to copy it into the destination.
    if (auto* sve = llvm::dyn_cast<StructuredValueExpr>(s.Value.get())) {
        if (sve->ResolvedType &&
            (sve->ResolvedType->Kind == TypeKind::Array ||
             sve->ResolvedType->Kind == TypeKind::Record)) {
            llvm::Type* ty = Types.llvmTypeOfSemaType(*sve->ResolvedType);
            auto& dl = Mod.getDataLayout();
            B.CreateMemCpy(addr, llvm::MaybeAlign(),
                                 rhs,  llvm::MaybeAlign(),
                                 dl.getTypeAllocSize(ty));
            return;
        }
    }

    // ISO §6.4.2.4: the value assigned to a subrange must lie within it.
    if (const auto& tt = s.Target->ResolvedType;
        tt && tt->Kind == TypeKind::Subrange && rhs->getType()->isIntegerTy()) {
        // EP §6.4.7: `record k: 1..n end` -- the discriminant fixes the range k
        // is checked against, not any storage.  The recorded bounds are the
        // probe's, so the check is re-emitted from the declaration against the
        // discriminants the object carries.
        bool checked = false;
        if (tt->ExtentVaries)
            if (auto path = Schema.schemaPathOf(*s.Target)) {
                const TypeNode* d = path->decl;
                while (auto* pk = llvm::dyn_cast_or_null<PackedTypeNode>(d))
                    d = pk->Inner.get();
                if (auto* sr = llvm::dyn_cast_or_null<SubrangeTypeNode>(d)) {
                    // R3: the form, against THIS object's discriminants.  The
                    // bounds used to be re-emitted here as expressions, which
                    // resolved the declaration's names in the assigning
                    // procedure -- so a `const n` in the bound was answered by
                    // any unrelated `var n` in scope at the assignment.
                    auto b = SchemaLayout.boundsOfDenoter(*sr, path->root.discs);
                    auto* lo = b ? b->first  : nullptr;
                    auto* hi = b ? b->second : nullptr;
                    if (lo && hi) {
                        RangeGuards.emitRangeCheckDyn(rhs, lo, hi, /*isIndex=*/false, s.Loc);
                        checked = true;
                    }
                }
            }
        // Lo == Hi is a legal singleton subrange (ISO §6.4.7 puts no floor on
        // the interval's width), not a sentinel for "no real bounds" -- Kind
        // == Subrange above already guarantees these bounds are real, so a
        // `SubLo != SubHi` guard here would just exempt `5..5` from the very
        // check this block exists to perform.
        if (!checked)
            RangeGuards.emitRangeCheck(rhs, tt->SubLo, tt->SubHi, /*isIndex=*/false, s.Loc);
    }

    // What the destination holds, not what the source produced: an array
    // element and a record field are just as much a real as a bare variable
    // is, and taking the source's type here stores the integer bit pattern.
    llvm::Type* dstTy = nullptr;
    if (auto* id = llvm::dyn_cast<IdentExpr>(s.Target.get()))
        if (auto* ve = SymTab.findVar(id->Name)) dstTy = ve->type;
    if (!dstTy && s.Target->ResolvedType)
        dstTy = Types.llvmTypeOfSemaType(*s.Target->ResolvedType);
    if (!dstTy) dstTy = rhs->getType();

    // A set crossing into a type whose base begins elsewhere moves with it.
    if (const auto& tt = s.Target->ResolvedType;
        tt && tt->Kind == TypeKind::Set)
        rhs = Sets.alignSet(rhs, Sets.setBaseOf(*s.Value), Sets.setBaseOf(*s.Target));

    // EP §6.4.2.2: integer/real → complex widening.
    if (dstTy == Complex.complexTy() && rhs->getType() != Complex.complexTy())
        rhs = Complex.coerceToComplex(rhs);
    // Implicit integer-to-real widening.
    if (dstTy->isDoubleTy() && rhs->getType()->isIntegerTy())
        rhs = B.CreateSIToFP(rhs, DblTy, "widen");
    // Integer width mismatch, either direction: i64 -> i8 (e.g. an integer
    // expression stored into a char variable) narrows, but i8/i1 -> i64 (a
    // char or boolean value stored into a subrange slot -- every subrange is
    // i64 regardless of its host type; see llvmTypeOfSemaTypeImpl) needs to
    // widen just as much.  This used to only trunc, so a narrower rhs was
    // stored with a store narrower than the destination slot, leaving
    // whatever was already in the slot's upper bytes untouched -- silently
    // for a freshly zero-initialized variable, wrong for a variant-record
    // field whose shared storage was last written through a wider
    // alternative (issue #229). Zero-extension is the correct widening: the
    // narrow ordinals plang has (char, boolean) are all non-negative, the
    // same reasoning coerceToType's identical int/int branch uses for every
    // other value-consuming path (call arguments, constructors, ...).
    if (dstTy->isIntegerTy() && rhs->getType()->isIntegerTy()
            && rhs->getType() != dstTy)
        rhs = B.CreateZExtOrTrunc(rhs, dstTy, "conv");

    auto* st = B.CreateStore(rhs, addr);
    // A field of a packed record is at a byte offset that need not satisfy its
    // own type's alignment; see packedAccessAlign.
    if (auto A = PackedAccessAlign(*s.Target)) st->setAlignment(*A);
}
