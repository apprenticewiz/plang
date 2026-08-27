#include "CGStructuredValue.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"

#include "CodegenICE.h"
#include "ConstFold.h"

using namespace plang;

// The denoter written for a named field, looked for among the fixed fields
// and then through the variants, which declare fields of their own.
const TypeNode* CGStructuredValue::fieldDenoter(const RecordTypeNode& rtn,
                                                 std::string_view name) {
    auto inSections = [&](const std::vector<FieldDecl>& fields) -> const TypeNode* {
        for (const auto& fd : fields)
            for (const auto& n : fd.Names)
                if (toLower(n) == toLower(std::string(name))) return fd.Type.get();
        return nullptr;
    };
    if (auto* t = inSections(rtn.Fields)) return t;
    for (const VariantPart* vp = rtn.Variant.get(); vp; ) {
        const VariantPart* next = nullptr;
        for (const auto& c : vp->Cases) {
            if (auto* t = inSections(c.Fields)) return t;
            if (!next) next = c.NestedVariant.get();
        }
        vp = next;
    }
    return nullptr;
}

llvm::Value* CGStructuredValue::emitStructuredValue(const StructuredValueExpr& e,
                                                      const TypeNode* denoter) {
    if (!e.ResolvedType) return llvm::ConstantInt::get(I64Ty, 0);

    // EP §6.8.7.1: written with a type name, that name says which declaration
    // gives the bounds and the fields; written without one — as a
    // component-value is — the denoter it stands for was handed in.
    //
    // `denoter` is reached by recursing into a FOREIGN declaration -- a
    // record's `fd.Type.get()` (fieldDenoter), an array's `atn->Element.get()`
    // -- so the names in it were written in that declaration's scope and not
    // in the procedure being lowered.  denoterOf walks `typeAliases`: flat,
    // keyed by spelling, rebuilt per procedure, so a homonym in the procedure
    // being lowered supplied the shape instead: a `comp` field typed as an
    // array by its own declaration, read through a nested procedure with its
    // own unrelated `comp`, cast the wrong TypeNode to ArrayTypeNode and
    // ICE'd ("array constructor has no array declaration").  initialStateShapeOf
    // is the sibling fix already applied to this same foreign-node pattern
    // (see its own comment) -- it follows NamedTypeNode::Denotes, which Sema
    // recorded in the scope the name was actually written in.
    const TypeNode* shape = InitialStateShapeOf(denoter);
    if (!e.TypeName.empty())
        if (auto it = TypeAliases.find(toLower(e.TypeName)); it != TypeAliases.end())
            shape = InitialStateShapeOf(it->second);

    // ---- Set constructor with type prefix: emit as bitmask ----
    if (e.ResolvedType->Kind == TypeKind::Set) {
        const int64_t base = Sets.setBaseOf(e);
        const auto declaredRange = Sets.declaredRangeOf(e);
        llvm::Value* result = llvm::ConstantInt::get(Sets.setTy(), 0);
        for (const auto& arm : e.Arms) {
            for (const auto& lbl : arm.Labels) {
                llvm::Value* bits = nullptr;
                if (auto* rng = llvm::dyn_cast<SetRangeExpr>(lbl.get()))
                    bits = Sets.emitSetRange(EmitExpr(*rng->Low), EmitExpr(*rng->High),
                                        base, declaredRange, rng->Loc);
                else
                    bits = Sets.emitSetSingleton(EmitExpr(*lbl), base, declaredRange, lbl->Loc);
                if (bits) result = B.CreateOr(result, bits, "set");
            }
            // Arms with Values in a set constructor are unusual but tolerated.
            if (arm.Value) (void)EmitExpr(*arm.Value);
        }
        return result;
    }

    // ---- Array constructor ----
    if (e.ResolvedType->Kind == TypeKind::Array) {
        // Handing back an integer for an array used to be the answer here, and
        // an array of no elements the answer below: between them every value
        // written in the constructor was dropped and the result was a
        // zero-filled aggregate of the wrong shape.
        auto* atn = llvm::dyn_cast_or_null<ArrayTypeNode>(shape);
        if (!atn)
            codegenICE("array constructor has no array declaration to take its "
                       "bounds and element type from");

        auto range = Types.arrayIndexRange(*atn);
        if (!range)
            codegenICE("array constructor has bounds that neither folded nor "
                       "Sema can give");
        const int64_t lo    = range->first;
        const int64_t hi    = range->second;
        const int64_t count = (hi >= lo) ? (hi - lo + 1) : 0;

        auto* arrTy = llvm::ArrayType::get(Types.llvmTypeOfNode(*atn->Element),
                                            static_cast<uint64_t>(count));
        auto* elemTy = arrTy->getElementType();
        auto* alloca = CreateEntryAlloca(arrTy, "arr.ctor");
        B.CreateStore(llvm::Constant::getNullValue(arrTy), alloca);

        // Helper: store val at element index idx (0-based after adjusting by lo).
        auto storeAt = [&](int64_t idx, llvm::Value* val) {
            if (idx < lo || idx > hi) return;
            auto* gep = B.CreateGEP(arrTy, alloca,
                {llvm::ConstantInt::get(I64Ty, 0),
                 llvm::ConstantInt::get(I64Ty, static_cast<uint64_t>(idx - lo))},
                "arr.ctor.e");
            // An element that is itself a structure arrives as the address of
            // one, there being no register that holds it.
            if (elemTy->isAggregateType() && val->getType()->isPointerTy()) {
                B.CreateMemCpy(gep, llvm::MaybeAlign(),
                                     val, llvm::MaybeAlign(),
                                     Mod.getDataLayout().getTypeAllocSize(elemTy));
                return;
            }
            B.CreateStore(CoerceToType(val, elemTy), gep);
        };

        // EP §6.8.7.2: 'otherwise' fills all unspecified indices.
        // Process 'otherwise' arms first (default values), then explicit arms
        // override them.  This matches the spec even when 'otherwise' appears
        // before explicit arms in the source.
        // A component-value of an element names no type either, so the
        // element's denoter goes with it.
        auto emitArmValue = [&](const ExprNode& v) {
            if (auto* sv = llvm::dyn_cast<StructuredValueExpr>(&v);
                    sv && sv->TypeName.empty())
                return emitStructuredValue(*sv, atn->Element.get());
            return EmitExpr(v);
        };

        for (const auto& arm : e.Arms) {
            if (!arm.IsOtherwise) continue;
            auto* val = emitArmValue(*arm.Value);
            for (int64_t i = lo; i <= hi; ++i) storeAt(i, val);
        }
        for (const auto& arm : e.Arms) {
            if (arm.IsOtherwise) continue;
            auto* val = emitArmValue(*arm.Value);
            // EP §6.8.7.2: an index in a constructor is a constant, so one that
            // will not fold is not an index this can place.  Standing in a
            // bound for it put the value at the end of the array, or outside it
            // where storeAt drops it and the element keeps the zero it started
            // with — either way somewhere the source never said.
            auto labelIndex = [&](const ExprNode& lbl) {
                auto v = tryEvalConstInt(lbl, &Consts);
                if (!v) codegenICE("array constructor has an index that is not "
                                   "a constant this can work out");
                return *v;
            };
            for (const auto& lbl : arm.Labels) {
                if (auto* rng = llvm::dyn_cast<SetRangeExpr>(lbl.get())) {
                    const int64_t rlo = labelIndex(*rng->Low);
                    const int64_t rhi = labelIndex(*rng->High);
                    for (int64_t i = rlo; i <= rhi; ++i) storeAt(i, val);
                } else {
                    storeAt(labelIndex(*lbl), val);
                }
            }
        }
        return alloca; // caller uses memcpy or memcpy-like assign
    }

    // ---- Record constructor ----
    if (e.ResolvedType->Kind == TypeKind::Record) {
        auto* rtn = llvm::dyn_cast_or_null<RecordTypeNode>(shape);
        if (!rtn)
            codegenICE("record constructor has no record declaration to take "
                       "its fields from");

        // The layout, rather than a map built here from the fixed fields: it
        // covers the tag and the variants too, which were silently dropped.
        const auto& L      = Types.layoutOf(*rtn);
        auto*       st     = L.Ty;
        auto*       alloca = CreateEntryAlloca(st, "rec.ctor");
        B.CreateStore(llvm::Constant::getNullValue(st), alloca);

        for (const auto& arm : e.Arms) {
            for (const auto& lbl : arm.Labels) {
                auto* id = llvm::dyn_cast<IdentExpr>(lbl.get());
                if (!id) continue;
                auto fit = L.Fields.find(toLower(id->Name));
                if (fit == L.Fields.end()) continue;
                const auto& P = fit->second;
                if (P.Index >= st->getNumElements()) continue;
                // A field's own value is written bare as well, so the field's
                // denoter is what says what shape it has.
                llvm::Value* val = nullptr;
                if (auto* sv = llvm::dyn_cast<StructuredValueExpr>(arm.Value.get());
                        sv && sv->TypeName.empty())
                    val = emitStructuredValue(*sv, fieldDenoter(*rtn, id->Name));
                else
                    val = EmitExpr(*arm.Value);
                llvm::Value* gep = B.CreateGEP(st, alloca,
                    {llvm::ConstantInt::get(I32Ty, 0),
                     llvm::ConstantInt::get(I32Ty, P.Index)},
                    "rec.ctor.f");
                if (P.InVariant && P.Offset != 0)
                    gep = B.CreateConstGEP1_64(I8Ty, gep, P.Offset,
                                                      "rec.ctor.f");
                if (P.Ty->isAggregateType() && val->getType()->isPointerTy()) {
                    B.CreateMemCpy(gep, llvm::MaybeAlign(),
                                         val, llvm::MaybeAlign(),
                                         Mod.getDataLayout().getTypeAllocSize(P.Ty));
                    continue;
                }
                B.CreateStore(CoerceToType(val, P.Ty), gep);
            }
        }
        return alloca;
    }

    return llvm::ConstantInt::get(I64Ty, 0); // fallback
}
