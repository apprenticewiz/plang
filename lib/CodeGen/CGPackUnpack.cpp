#include "CGPackUnpack.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"

#include "CodegenICE.h"

using namespace plang;

void CGPackUnpack::emitPackUnpack(const CallStmt& s, bool isPack) {
    const ExprNode& aExpr = isPack ? *s.Args[0] : *s.Args[1];
    const ExprNode& zExpr = isPack ? *s.Args[2] : *s.Args[0];
    const ExprNode& iExpr = isPack ? *s.Args[1] : *s.Args[2];

    const std::string what = isPack ? "pack" : "unpack";
    const auto& zTy = zExpr.ResolvedType;
    if (!zTy || zTy->Kind != TypeKind::Array || !zTy->IndexType)
        codegenICE(what + " has no packed array to transfer");

    const int64_t count = zTy->IndexType->SubHi - zTy->IndexType->SubLo + 1;
    if (count <= 0) return;

    // Where the unpacked array starts and ends.  A conformant array parameter
    // is an array like any other to ISO §6.6.5.4, but its bounds arrived with
    // it as hidden arguments and are values in this activation rather than
    // numbers in its type, so both ends are Values and the check on the
    // starting index is made at run time.
    llvm::Value* aPtr   = nullptr;
    llvm::Type*  elemTy = nullptr;
    llvm::Value* aLo    = nullptr;
    llvm::Value* aHi    = nullptr;

    const VarEntry* conf = nullptr;
    if (auto* id = llvm::dyn_cast<IdentExpr>(&aExpr))
        if (const auto* ve = SymTab.findVar(id->Name); ve && ve->isConformantArray)
            conf = ve;

    if (conf) {
        auto boundOf = [&](const std::string& nm) -> llvm::Value* {
            const auto* bv = SymTab.findVar(nm);
            return bv ? B.CreateLoad(I64Ty, bv->ptr, "conf.bound") : nullptr;
        };
        aPtr   = conf->ptr;
        elemTy = conf->conformantElemTy;
        aLo    = boundOf(conf->conformantLoName);
        aHi    = boundOf(conf->conformantHiName);
        if (!aPtr || !elemTy || !aLo || !aHi)
            codegenICE(what + " has a conformant array whose bounds did not "
                              "arrive with it");
    } else if (auto path = Schema.schemaPathOf(aExpr);
               path && llvm::isa_and_nonnull<ArrayTypeNode>(
                           PeelPackedNode(path->decl))) {
        // EP §6.4.7: the bounds of a schema array are not in its type.  Sema
        // holds the PROBE's, so reading them from the type checked
        // `pack(q^.a, 3, z)` against "1..-2" -- one minus the width of z, off a
        // probe upper bound of 1 -- and refused a legal program with a bound
        // that describes nothing.  Re-emitted here against the discriminants
        // the object carries, like every other extent in a schema body.
        auto* at = llvm::cast<ArrayTypeNode>(PeelPackedNode(path->decl));
        SchemaLayoutEngine::RtDiscScope disc(SchemaLayout, path->root.discs);
        auto  bounds = SchemaLayout.rtIndexBounds(*at);
        auto* elemSz = SchemaLayout.rtSizeOfTypeNode(at->Element.get());
        if (!bounds)
            codegenICE(what + " has a schema array whose bounds cannot be "
                              "evaluated at run time");
        // The transfer strides by a constant element size, so ask the layout
        // walk whether this element has one -- rather than asking the node's
        // annotation, which belongs to whichever instantiation was resolved
        // last and is not this walk's to trust.  Loud rather than wrong.
        if (!llvm::isa_and_nonnull<llvm::ConstantInt>(elemSz))
            codegenICE(what + " on an array whose element size a discriminant "
                              "fixes");
        aPtr   = path->addr;
        elemTy = Types.llvmTypeOfNode(*at->Element);
        aLo    = bounds->first;
        aHi    = bounds->second;
    } else {
        const auto& aTy = aExpr.ResolvedType;
        if (!aTy || aTy->Kind != TypeKind::Array || !aTy->IndexType
                || !aTy->ElemType)
            codegenICE(what + " has no unpacked array to transfer");
        elemTy = Types.llvmTypeOfSemaType(*aTy->ElemType);
        aPtr   = EmitLValue(aExpr);
        aLo = llvm::ConstantInt::get(I64Ty,
                  static_cast<uint64_t>(aTy->IndexType->SubLo), /*isSigned=*/true);
        aHi = llvm::ConstantInt::get(I64Ty,
                  static_cast<uint64_t>(aTy->IndexType->SubHi), /*isSigned=*/true);
    }

    auto* zPtr = EmitLValue(zExpr);
    if (!aPtr || !zPtr)
        codegenICE(what + " operand is not an addressable array");

    // The unpacked array must hold `count` components from index i onwards.
    auto* idx  = ToI64(EmitExpr(iExpr));
    auto* last = B.CreateSub(
        aHi, llvm::ConstantInt::get(I64Ty, static_cast<uint64_t>(count - 1)),
        "pack.last");
    RangeGuards.emitRangeCheckDyn(idx, aLo, last, /*isIndex=*/true, iExpr.Loc);

    auto* off = B.CreateSub(idx, aLo, "pack.off");
    auto* aElem = B.CreateGEP(elemTy, aPtr, {off}, "pack.a");

    const auto  elemSize = Mod.getDataLayout().getTypeAllocSize(elemTy);
    auto* bytes = llvm::ConstantInt::get(I64Ty, elemSize * static_cast<uint64_t>(count));
    const llvm::Align align = Mod.getDataLayout().getABITypeAlign(elemTy);
    if (isPack) B.CreateMemCpy(zPtr, align, aElem, align, bytes);
    else        B.CreateMemCpy(aElem, align, zPtr, align, bytes);
}
