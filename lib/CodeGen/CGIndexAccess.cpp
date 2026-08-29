#include "CGIndexAccess.h"

#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"

using namespace plang;

int64_t CGIndexAccess::arrayIndexLow(const ArrayTypeNode& n) const {
    auto R = Types.arrayIndexRange(n);
    if (!R) codegenICE("array has no first index that either its bounds or "
                       "Sema can give");
    return R->first;
}

llvm::Value* CGIndexAccess::emitConformantElemPtr(const IndexExpr& e) {
    // Walk down to the name being subscripted, collecting the subscripts on
    // the way so that they come back outermost first.
    std::vector<const ExprNode*> subs{e.Index.get()};
    // The array being subscripted at each level, so that a subscript past the
    // conformant dimensions can be given the bounds of the type it indexes.
    std::vector<const ExprNode*> arrs{e.Array.get()};
    const ExprNode* base = e.Array.get();
    while (auto* inner = llvm::dyn_cast<IndexExpr>(base)) {
        subs.push_back(inner->Index.get());
        arrs.push_back(inner->Array.get());
        base = inner->Array.get();
    }
    auto* id = llvm::dyn_cast<IdentExpr>(base);
    if (!id) return nullptr;
    const VarEntry* ve = SymTab.findVar(id->Name);
    if (!ve || !ve->isConformantArray) return nullptr;
    std::reverse(subs.begin(), subs.end());
    std::reverse(arrs.begin(), arrs.end());

    // The bounds are ordinary integer variables in this activation, put there
    // by the prologue from the hidden arguments.
    // By address, not by name.  The names are what the programmer wrote in the
    // parameter list, and any scope opened since can answer them: a record with
    // fields spelled `lo` and `hi` made every subscript inside `with r do`
    // adjust by the record's fields instead of the array's bounds and read
    // outside the block.
    auto boundAt = [&](llvm::Value* slot, const std::string& name) -> llvm::Value* {
        if (slot) return B.CreateLoad(I64Ty, slot, "conf.bound");
        auto* bv = SymTab.findVar(name);   // an interface-file conformant, with no prologue here
        return bv ? B.CreateLoad(I64Ty, bv->ptr, "conf.bound") : nullptr;
    };
    auto loOf = [&](size_t d) -> llvm::Value* {
        if (d >= ve->conformantDims.size()) return nullptr;
        return boundAt(d < ve->conformantDimPtrs.size()
                           ? ve->conformantDimPtrs[d].first : nullptr,
                       ve->conformantDims[d].first);
    };
    auto hiOf = [&](size_t d) -> llvm::Value* {
        if (d >= ve->conformantDims.size()) return nullptr;
        return boundAt(d < ve->conformantDimPtrs.size()
                           ? ve->conformantDimPtrs[d].second : nullptr,
                       ve->conformantDims[d].second);
    };
    auto extentOf = [&](size_t d) -> llvm::Value* {
        auto* lo = loOf(d);
        auto* hi = hiOf(d);
        if (!lo || !hi) return nullptr;
        return B.CreateAdd(B.CreateSub(hi, lo, "conf.span"),
                                 llvm::ConstantInt::get(I64Ty, 1), "conf.ext");
    };

    // The array is one flat block, so the subscripts fold together the way a
    // row-major layout reads: each one scales what came before it by the width
    // of its own dimension.
    llvm::Value* flat = llvm::ConstantInt::get(I64Ty, 0);
    const size_t dims  = ve->conformantDims.empty() ? 1 : ve->conformantDims.size();
    // Only the CONFORMANT dimensions fold into the flat index.  A subscript
    // past them indexes the element type, which has static bounds of its own,
    // and folding it in here treated it as another conformant dimension: with
    // `a: array[lo..hi: integer] of row` and `row = array[1..3] of integer`,
    // `a[1][2]` came out two whole rows along, which for a two-row actual is
    // the variable after the array.
    const size_t nflat = std::min(subs.size(), dims);
    for (size_t d = 0; d < nflat; ++d) {
        auto* idx = ToI64(EmitExpr(*subs[d]));
        if (d < ve->conformantDims.size()) {
            // EP §6.7.3.7: every other indexing path (plain arrays, schema
            // arrays, arrays through pointers) range-checks the subscript;
            // a conformant array's own bounds are only known at run time, so
            // this is the dynamic form -- checked against the RAW subscript,
            // before the lower-bound adjustment, so the reported value and
            // range are the ones the source actually wrote.
            auto* lo = loOf(d);
            auto* hi = hiOf(d);
            if (lo && hi)
                RangeGuards.emitRangeCheckDyn(idx, lo, hi, /*isIndex=*/true, e.Loc);
            if (lo)
                idx = B.CreateSub(idx, lo, "idx.adj.conf");
        }
        if (d > 0)
            if (auto* ext = extentOf(d))
                flat = B.CreateMul(flat, ext, "conf.row");
        flat = B.CreateAdd(flat, idx, "conf.off");
    }
    // A subscript short of the last conformant dimension names a row rather
    // than an element, and a row is as wide as the dimensions still to come.
    for (size_t d = nflat; d < dims; ++d)
        if (auto* ext = extentOf(d))
            flat = B.CreateMul(flat, ext, "conf.row");

    llvm::Type* elemTy = ve->conformantElemTy ? ve->conformantElemTy : I64Ty;
    llvm::Value* p = B.CreateGEP(elemTy, ve->ptr, {flat}, "elem.ptr");

    // The rest index the element type the ordinary way.
    auto* zero = llvm::ConstantInt::get(I64Ty, 0);
    for (size_t d = nflat; d < subs.size(); ++d) {
        const Type* at = (d < arrs.size() && arrs[d]) ? arrs[d]->ResolvedType.get()
                                                      : nullptr;
        // The element type of a conformant array's own element may itself be
        // a schema instantiation -- `row = vec(3)` -- and the bounds and
        // stride belong to what it underlies to, not to the immediate Kind.
        // Without this, `a[lo][2]` for such a `row` fell to the untyped i64
        // GEP below with no lower-bound adjustment: wrong element type AND
        // wrong offset, landing the write one element past where it belongs.
        if (at) at = schemaUnderlying(at);
        auto* idx = ToI64(EmitExpr(*subs[d]));
        if (at && at->Kind == TypeKind::Array) {
            // This dimension's bounds are fixed by the element type's own
            // declaration rather than by anything the caller passed at run
            // time, so -- like the plain-array path in emitIndexGEP -- the
            // check is against constants, before the lower-bound adjustment.
            if (at->IndexType)
                RangeGuards.emitRangeCheck(idx, at->IndexType->SubLo,
                                            at->IndexType->SubHi,
                                            /*isIndex=*/true, e.Loc);
            if (at->IndexType && at->IndexType->SubLo != 0)
                idx = B.CreateSub(
                    idx, llvm::ConstantInt::get(I64Ty, at->IndexType->SubLo),
                    "idx.adj");
            p = B.CreateGEP(Types.llvmTypeOfSemaType(*at), p, {zero, idx},
                                  "elem.ptr");
        } else {
            p = B.CreateGEP(I64Ty, p, {idx}, "elem.ptr");
        }
    }
    return p;
}

llvm::Value* CGIndexAccess::emitIndexGEP(const IndexExpr& e) {
    // Turbo: `p[i]` on a PChar-like pointer -- Sema::checkIndex's own
    // Pointer arm (SemaExpr.cpp) is the only thing that resolves an
    // IndexExpr's Array to a Pointer type at all, and only under -std=turbo
    // for a pointer whose pointee is Char (isCharPointerType, Type.h), so no
    // further gating is needed here: reaching this branch already means
    // Sema accepted it.  Zero-based, no range check -- there is no declared
    // extent on the pointee to check against, matching real fpc.
    //
    // This is also what makes `p[0] := 'H'` a write: EmitLValue's IndexExpr
    // case (CGExprCore.cpp) calls straight through to this same function for
    // the address, with no load, so CGAssign's generic store path handles
    // the write with no changes of its own.
    if (e.Array->ResolvedType && e.Array->ResolvedType->Kind == TypeKind::Pointer
            && e.Array->ResolvedType->PointeeType
            && e.Array->ResolvedType->PointeeType->Kind == TypeKind::Char) {
        auto* base = EmitExpr(*e.Array); // the pointer VALUE, not its address
        auto* idx  = ToI64(EmitExpr(*e.Index));
        return B.CreateGEP(I8Ty, base, {idx}, "pchar.elem.ptr");
    }
    // EP §6.4.7: an array FIELD of a run-time-laid-out body has bounds the
    // discriminants fix, so they are re-emitted here rather than read off the
    // type -- which holds the probe's, and would check `q^.d[2]` against 1..1.
    // The address of the field itself already comes from the run-time offset.
    // An array whose extent a discriminant fixes, reached anywhere in a path:
    // its bounds and its stride are recomputed, and both come from the same
    // recursion so that `q^.d[i, j]` checks BOTH subscripts rather than only
    // the innermost.
    if (e.Array->ResolvedType && e.Array->ResolvedType->ExtentVaries)
        if (auto path = Schema.schemaPathOf(e)) return path->addr;
    // EP §6.4.7: an undiscriminated schema recomputes its bounds from the
    // discriminants it carries, then indexes like a conformant array.
    //
    // Only when its body actually IS an array.  `q^[1]` for a `^string` is a
    // string component, EP §6.5.3.2, and asking this branch for it went looking
    // for an array body on the string schema and killed the compiler -- the
    // string case below was never reached.  A record-bodied schema has no
    // subscript at all and must fall through to be diagnosed, not crash.
    if (auto ref = Schema.schemaRefOf(*e.Array);
            ref && ref->semaTy && ref->semaTy->SchemaBody
            && ref->semaTy->SchemaBody->Kind == TypeKind::Array) {
        auto [lo, hi] = Schema.schemaArrayBounds(*ref);
        auto* elemTy  = Schema.schemaStorageType(*ref);
        auto* idx     = ToI64(EmitExpr(*e.Index));
        RangeGuards.emitRangeCheckDyn(idx, lo, hi, /*isIndex=*/true, e.Loc);
        idx = B.CreateSub(idx, lo, "idx.adj.sch");
        return B.CreateGEP(elemTy, ref->data, {idx}, "elem.ptr");
    }

    // EP §6.5.3.2: s[i] selects the i'th character, counting from 1 and running
    // to the string's current length rather than to its capacity.
    if (ExprIsVarStr(*e.Array)) {
        auto* strPtr = StrCall.emitStrAddr(*e.Array);
        auto* idx    = ToI64(EmitExpr(*e.Index));
        if (RangeGuards.rangeChecksAt(e.Loc)) {
            auto* len   = Strings.strLoadLen(strPtr);
            auto* one   = llvm::ConstantInt::get(I64Ty, 1);
            auto* bad   = B.CreateOr(
                B.CreateICmpSLT(idx, one,  "str.rng.lo"),
                B.CreateICmpSGT(idx, len,  "str.rng.hi"), "str.rng.bad");
            RangeGuards.emitGuard(bad, "strbounds", [&] {
                B.CreateCall(
                    RtFns.getExternFnN("plang_err_str_index",
                                 llvm::Type::getVoidTy(Ctx), {I64Ty, I64Ty}),
                    {idx, len});
            });
        }
        auto* zeroBased = B.CreateSub(idx,
            llvm::ConstantInt::get(I64Ty, 1), "str.idx");
        return B.CreateGEP(I8Ty, Strings.strDataPtr(strPtr), {zeroBased},
                                 "str.elem.ptr");
    }

    // EP §6.7.3.7: conformant array.  Every dimension's bounds only exist at
    // run time, so the whole subscript chain is flattened together here rather
    // than one subscript at a time.
    if (auto* conf = emitConformantElemPtr(e)) return conf;

    auto* ve = [&]() -> const VarEntry* {
        if (auto* id = llvm::dyn_cast<IdentExpr>(e.Array.get()))
            return SymTab.findVar(id->Name);
        return nullptr;
    }();

    auto* idx = ToI64(EmitExpr(*e.Index));
    auto* arrPtr = ve ? ve->ptr : EmitLValue(*e.Array);

    llvm::Type* arrTy  = nullptr;
    llvm::Type* elemTy = I64Ty;
    // Try to get array type info from the typenode embedded in the ident.
    if (auto* id = llvm::dyn_cast<IdentExpr>(e.Array.get())) {
        auto* ve2 = SymTab.findVar(id->Name);
        if (ve2) {
            if (llvm::isa<llvm::ArrayType>(ve2->type)) {
                arrTy  = ve2->type;
                elemTy = llvm::cast<llvm::ArrayType>(arrTy)->getElementType();
            } else {
                elemTy = ve2->type;
            }
        }
    }
    // Subtract the lower bound so Pascal array [lo..hi] maps to LLVM [0..hi-lo].
    int64_t Low = 0;
    // The declaration is the better source of the bounds, but a name with no
    // variable behind it — a parameterless function returning an array — has
    // none, and then the Sema type is all there is.  Falling through was what
    // this used to do only for an operand that was not a name at all, so
    // `ramp[3]` indexed with no element type and no bounds at all.
    const VarEntry* declVe = nullptr;
    if (auto* id2 = llvm::dyn_cast<IdentExpr>(e.Array.get()))
        declVe = SymTab.findVar(id2->Name);
    if (declVe) {
        if (auto* atn = llvm::dyn_cast_or_null<ArrayTypeNode>(declVe->typeNode)) {
            Low = arrayIndexLow(*atn);
        } else if (auto* stn = llvm::dyn_cast_or_null<SchemaTypeNode>(declVe->typeNode)) {
            // EP §6.4.7: schema instance — read lower bound from resolved body type.
            if (stn->ResolvedBody && stn->ResolvedBody->SchemaBody
                    && stn->ResolvedBody->SchemaBody->Kind == TypeKind::Array
                    && stn->ResolvedBody->SchemaBody->IndexType)
                Low = stn->ResolvedBody->SchemaBody->IndexType->SubLo;
        } else if (auto* ntn = llvm::dyn_cast_or_null<NamedTypeNode>(declVe->typeNode)) {
            // Named type alias (e.g. var r: Row where Row = array[1..5] of ...).
            // Through the WHOLE chain of names, which is what denoterOf is for.
            // One hop left `type row = array[5..10] of integer; rowalias = row;`
            // with Low at zero while the array's *type* had been resolved
            // through every hop, so the index was never adjusted and the range
            // check ran against 0..n-1: a legal x[6] aborted, and with the
            // checks off the writes landed past the end of the array.
            if (auto* atn2 = llvm::dyn_cast_or_null<ArrayTypeNode>(DenoterOf(ntn)))
                Low = arrayIndexLow(*atn2);
        }
        // Sema's answer wins wherever it has one.  The routes above read the
        // declaration through typeAliases, which is rebuilt per procedure and
        // answers by SPELLING, so a nested procedure declaring its own `t`
        // handed an outer `a: array[0..4] of integer` the inner t's bound of
        // ten: a[0] wrote ten elements before the array and a[4] six before
        // it, silently, with the range check passing because it was checked
        // against 10..14 as well.
        //
        // Making Sema a fallback for a zero bound was not enough -- a WRONG
        // NON-ZERO bound never reached it.
        if (e.Array->ResolvedType) {
            const Type* T = e.Array->ResolvedType.get();
            T = schemaUnderlying(T);
            if (T->Kind == TypeKind::Array && T->IndexType)
                Low = T->IndexType->SubLo;
        }
    } else if (e.Array->ResolvedType) {
        // Nested indexing A[1][2], or anything else with no declaration to
        // read: use the Sema type for the element type and the lower bound.
        const Type* T = e.Array->ResolvedType.get();
        T = schemaUnderlying(T);
        if (T->Kind == TypeKind::Array) {
            if (T->ElemType && !T->ElemType->isError())
                elemTy = Types.llvmTypeOfSemaType(*T->ElemType);
            if (T->IndexType)
                Low = T->IndexType->SubLo;
            // The extent has to be recovered here as well, or the bounds check
            // below has nothing to test against.  Every dimension after the
            // first indexes an expression rather than a name, so leaving this
            // null let a[1][i] run off the end of the inner array unchecked —
            // and with a[i, j] abbreviating exactly that, it is the ordinary
            // way to reach a multi-dimensional array.
            arrTy = Types.llvmTypeOfSemaType(*T);
        }
    }
    // Check before the lower-bound adjustment so the reported value and range
    // are the ones the source actually wrote.
    if (auto* at = llvm::dyn_cast_or_null<llvm::ArrayType>(arrTy)) {
        auto n = static_cast<int64_t>(at->getNumElements());
        if (n > 0) RangeGuards.emitRangeCheck(idx, Low, Low + n - 1, /*isIndex=*/true, e.Loc);
    }
    if (Low != 0)
        idx = B.CreateSub(idx, llvm::ConstantInt::get(I64Ty, Low), "idx.adj");

    if (!arrTy) {
        auto* ep = B.CreateGEP(elemTy, arrPtr, {idx}, "elem.ptr");
        return ep;
    }

    auto* ep = B.CreateGEP(arrTy, arrPtr,
                   {llvm::ConstantInt::get(I64Ty, 0), idx}, "elem.ptr");
    return ep;
}

llvm::Value* CGIndexAccess::emitIndexLoad(const IndexExpr& e) {
    auto* ptr = emitIndexGEP(e);
    // EP §6.5.3.2: a string component is a char.
    if (ExprIsVarStr(*e.Array))
        return B.CreateLoad(I8Ty, ptr, "str.elem");
    // Turbo: `p[i]` on a PChar-like pointer -- see emitIndexGEP's identical
    // guard just above for why no further gating belongs here.  The pointee
    // is Char, one byte, the same as a string component.
    if (e.Array->ResolvedType && e.Array->ResolvedType->Kind == TypeKind::Pointer
            && e.Array->ResolvedType->PointeeType
            && e.Array->ResolvedType->PointeeType->Kind == TypeKind::Char)
        return B.CreateLoad(I8Ty, ptr, "pchar.elem");
    llvm::Type* elemTy = I64Ty;
    // EP §6.4.7: the element type of a schematic array comes from Sema; the
    // variable entry holds only the untyped body pointer.
    if (e.Array->ResolvedType && e.Array->ResolvedType->Kind == TypeKind::Schema) {
        const plang::Type* T = e.ResolvedType.get();
        if (!T || T->isError())
            codegenICE("indexing a schematic variable produced no element type");
        return B.CreateLoad(Types.llvmTypeOfSemaType(*T), ptr, "elem");
    }
    if (auto* id = llvm::dyn_cast<IdentExpr>(e.Array.get())) {
        auto* ve = SymTab.findVar(id->Name);
        if (ve) {
            // EP §6.7.3.7: for conformant arrays, use conformantElemTy.
            if (ve->isConformantArray && ve->conformantElemTy)
                elemTy = ve->conformantElemTy;
            else if (llvm::isa<llvm::ArrayType>(ve->type))
                elemTy = llvm::cast<llvm::ArrayType>(ve->type)->getElementType();
            else
                elemTy = ve->type;
        }
    } else if (e.ResolvedType) {
        // Non-IdentExpr array (e.g. nested A[1][2]): use Sema-annotated element type.
        const Type* T = e.ResolvedType.get();
        T = schemaUnderlying(T);
        if (!T->isError())
            elemTy = Types.llvmTypeOfSemaType(*T);
    }
    auto* ld = B.CreateLoad(elemTy, ptr, "elem");
    // Issue #192: a[i] into an array field of a packed record sits at a byte
    // offset elemTy's ABI alignment does not fix; see packedAccessAlign
    // (CGFieldAccess.cpp), which this recurses into via e.Array.
    if (auto A = PackedAccessAlign(e)) ld->setAlignment(*A);
    return ld;
}
