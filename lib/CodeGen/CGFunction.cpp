// CGFunction.cpp — Codegen::Impl::CGFunction, the RAII guard bracketing one
// function-activation's per-activation-transient state (ISO §6.7.1
// nested-procedure ABI). See its declaration in CodeGenImpl.h for the full
// design rationale.
#include "CodeGenImpl.h"

Codegen::Impl::CGFunction::CGFunction(Impl& I)
    : I(I),
      SavedFunc(I.curFunc), SavedRetAlloca(I.curRetAlloca),
      SavedRetType(I.curRetType), SavedFuncName(I.curFuncName),
      SavedNamePrefix(I.namePrefix), SavedCurFn(I.curFn_) {
    I.curFn_ = this;
}

Codegen::Impl::CGFunction::~CGFunction() {
    I.curFunc      = SavedFunc;
    I.curRetAlloca = SavedRetAlloca;
    I.curRetType   = SavedRetType;
    I.curFuncName  = SavedFuncName;
    I.namePrefix   = SavedNamePrefix;
    I.curFn_       = SavedCurFn;
}
