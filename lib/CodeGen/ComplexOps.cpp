#include "ComplexOps.h"

#include "llvm/IR/Intrinsics.h"

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
    // The textbook (ar+ai*i)/(br+bi*i) = ((ar*br+ai*bi) + (ai*br-ar*bi)*i) /
    // (br^2+bi^2) squares the divisor's components before anything else, so
    // it silently hands back a garbage-but-finite answer wherever br^2+bi^2
    // over- or underflows double range even though the true quotient is
    // perfectly representable (e.g. dividing by 1e155+1e155*i: 1e155 alone
    // is fine, but its square overflows to +Inf, and a finite numerator
    // divided by an infinite denominator silently rounds to 0).
    //
    // Smith's algorithm (the same approach glibc's __divdc3 uses) avoids
    // squaring the larger-magnitude divisor component at all: it scales by
    // the *ratio* of the divisor's components, which stays in [-1, 1] and
    // so never overflows, and only ever multiplies that ratio back against
    // values already known to be in-range.
    auto* ar = B.CreateExtractValue(a, 0, "a.re");
    auto* ai = B.CreateExtractValue(a, 1, "a.im");
    auto* br = B.CreateExtractValue(b, 0, "b.re");
    auto* bi = B.CreateExtractValue(b, 1, "b.im");

    auto* absBr = B.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, br, nullptr, "b.re.abs");
    auto* absBi = B.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, bi, nullptr, "b.im.abs");
    auto* brGE  = B.CreateFCmpOGE(absBr, absBi, "b.re.ge.im");

    // |br| >= |bi|: r = bi/br (|r| <= 1), den = br + bi*r.
    auto* rBr   = B.CreateFDiv(bi, br, "r.brge");
    auto* denBr = B.CreateFAdd(br, B.CreateFMul(bi, rBr, "bi.r.brge"), "den.brge");
    auto* reBr  = B.CreateFDiv(
        B.CreateFAdd(ar, B.CreateFMul(ai, rBr, "ai.r.brge"), "num.re.brge"),
        denBr, "c.re.brge");
    auto* imBr  = B.CreateFDiv(
        B.CreateFSub(ai, B.CreateFMul(ar, rBr, "ar.r.brge"), "num.im.brge"),
        denBr, "c.im.brge");

    // |bi| > |br|: r = br/bi (|r| < 1), den = bi + br*r.
    auto* rBi   = B.CreateFDiv(br, bi, "r.bigt");
    auto* denBi = B.CreateFAdd(bi, B.CreateFMul(br, rBi, "br.r.bigt"), "den.bigt");
    auto* reBi  = B.CreateFDiv(
        B.CreateFAdd(B.CreateFMul(ar, rBi, "ar.r.bigt"), ai, "num.re.bigt"),
        denBi, "c.re.bigt");
    auto* imBi  = B.CreateFDiv(
        B.CreateFSub(B.CreateFMul(ai, rBi, "ai.r.bigt"), ar, "num.im.bigt"),
        denBi, "c.im.bigt");

    return makeComplex(B.CreateSelect(brGE, reBr, reBi, "c.re"),
                       B.CreateSelect(brGE, imBr, imBi, "c.im"));
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
