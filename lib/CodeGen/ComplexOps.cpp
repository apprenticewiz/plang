#include "ComplexOps.h"

llvm::StructType* ComplexOps::complexTy() {
    if (!Ty) Ty = llvm::StructType::get(Ctx, {DblTy, DblTy});
    return Ty;
}

llvm::Value* ComplexOps::makeComplex(llvm::Value* re, llvm::Value* im) {
    auto* v  = llvm::UndefValue::get(complexTy());
    auto* v1 = B.CreateInsertValue(v,  re, 0, "cplx.re");
    return    B.CreateInsertValue(v1, im, 1, "cplx.im");
}

llvm::Value* ComplexOps::coerceToComplex(llvm::Value* v) {
    if (!v) return llvm::ConstantAggregateZero::get(complexTy());
    if (v->getType() == complexTy()) return v;
    auto* re = ToDouble(v);
    auto* im = llvm::ConstantFP::get(DblTy, 0.0);
    return makeComplex(re, im);
}

llvm::Value* ComplexOps::emitComplexAdd(llvm::Value* a, llvm::Value* b) {
    auto* ar = B.CreateExtractValue(a, 0, "a.re");
    auto* ai = B.CreateExtractValue(a, 1, "a.im");
    auto* br = B.CreateExtractValue(b, 0, "b.re");
    auto* bi = B.CreateExtractValue(b, 1, "b.im");
    return makeComplex(B.CreateFAdd(ar, br, "c.re"),
                       B.CreateFAdd(ai, bi, "c.im"));
}

llvm::Value* ComplexOps::emitComplexSub(llvm::Value* a, llvm::Value* b) {
    auto* ar = B.CreateExtractValue(a, 0, "a.re");
    auto* ai = B.CreateExtractValue(a, 1, "a.im");
    auto* br = B.CreateExtractValue(b, 0, "b.re");
    auto* bi = B.CreateExtractValue(b, 1, "b.im");
    return makeComplex(B.CreateFSub(ar, br, "c.re"),
                       B.CreateFSub(ai, bi, "c.im"));
}

llvm::Value* ComplexOps::emitComplexMul(llvm::Value* a, llvm::Value* b) {
    // (ar+ai*i)(br+bi*i) = (ar*br - ai*bi) + (ar*bi + ai*br)*i
    auto* ar = B.CreateExtractValue(a, 0, "a.re");
    auto* ai = B.CreateExtractValue(a, 1, "a.im");
    auto* br = B.CreateExtractValue(b, 0, "b.re");
    auto* bi = B.CreateExtractValue(b, 1, "b.im");
    auto* rr = B.CreateFMul(ar, br, "ar.br");
    auto* ii = B.CreateFMul(ai, bi, "ai.bi");
    auto* ri = B.CreateFMul(ar, bi, "ar.bi");
    auto* ir = B.CreateFMul(ai, br, "ai.br");
    return makeComplex(B.CreateFSub(rr, ii, "c.re"),
                       B.CreateFAdd(ri, ir, "c.im"));
}

llvm::Value* ComplexOps::emitComplexDiv(llvm::Value* a, llvm::Value* b) {
    // (ar+ai*i)/(br+bi*i) = ((ar*br+ai*bi) + (ai*br-ar*bi)*i) / (br^2+bi^2)
    auto* ar = B.CreateExtractValue(a, 0, "a.re");
    auto* ai = B.CreateExtractValue(a, 1, "a.im");
    auto* br = B.CreateExtractValue(b, 0, "b.re");
    auto* bi = B.CreateExtractValue(b, 1, "b.im");
    auto* denom = B.CreateFAdd(B.CreateFMul(br, br, "br2"),
                                      B.CreateFMul(bi, bi, "bi2"), "denom");
    auto* numRe = B.CreateFAdd(B.CreateFMul(ar, br, "ar.br"),
                                      B.CreateFMul(ai, bi, "ai.bi"), "num.re");
    auto* numIm = B.CreateFSub(B.CreateFMul(ai, br, "ai.br"),
                                      B.CreateFMul(ar, bi, "ar.bi"), "num.im");
    return makeComplex(B.CreateFDiv(numRe, denom, "c.re"),
                       B.CreateFDiv(numIm, denom, "c.im"));
}

llvm::Value* ComplexOps::callComplexUnary(const std::string& name, llvm::Value* z) {
    // Convention: plang_cXXX_out(double* re_out, double* im_out, double re, double im)
    auto* re_out = EntryAlloca(DblTy, name + ".re");
    auto* im_out = EntryAlloca(DblTy, name + ".im");
    auto* re_in  = B.CreateExtractValue(z, 0, "z.re");
    auto* im_in  = B.CreateExtractValue(z, 1, "z.im");
    auto* fn = RtFns.getExternFnN(name, llvm::Type::getVoidTy(Ctx),
                             {PtrTy, PtrTy, DblTy, DblTy});
    B.CreateCall(fn, {re_out, im_out, re_in, im_in});
    auto* re = B.CreateLoad(DblTy, re_out, "re");
    auto* im = B.CreateLoad(DblTy, im_out, "im");
    return makeComplex(re, im);
}

llvm::Value* ComplexOps::emitComplexPow(llvm::Value* a, llvm::Value* b) {
    // plang_cpow_out(re_out, im_out, are, aim, bre, bim)
    auto* re_out = EntryAlloca(DblTy, "cpow.re");
    auto* im_out = EntryAlloca(DblTy, "cpow.im");
    auto* ar = B.CreateExtractValue(a, 0, "a.re");
    auto* ai = B.CreateExtractValue(a, 1, "a.im");
    auto* br = B.CreateExtractValue(b, 0, "b.re");
    auto* bi = B.CreateExtractValue(b, 1, "b.im");
    auto* fn = RtFns.getExternFnN("plang_cpow_out", llvm::Type::getVoidTy(Ctx),
                             {PtrTy, PtrTy, DblTy, DblTy, DblTy, DblTy});
    B.CreateCall(fn, {re_out, im_out, ar, ai, br, bi});
    auto* re = B.CreateLoad(DblTy, re_out, "re");
    auto* im = B.CreateLoad(DblTy, im_out, "im");
    return makeComplex(re, im);
}
