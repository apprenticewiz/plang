// CGFunction.cpp — Codegen::Impl::CGFunction, the RAII guard bracketing one
// function-activation's per-activation-transient state (ISO §6.7.1
// nested-procedure ABI). See its declaration in CodeGenImpl.h for the full
// design rationale.
#include "CodeGenImpl.h"

Codegen::Impl::CGFunction::CGFunction(Impl& I)
    : I(I),
      SavedFunc(I.curFunc), SavedRetAlloca(I.curRetAlloca),
      SavedRetType(I.curRetType), SavedFuncName(I.curFuncName),
      SavedNamePrefix(I.namePrefix),
      SavedTypeAliases(I.typeAliases), SavedConsts(I.consts),
      SavedRequiredConsts(I.requiredConsts),
      SavedFuncScopeDepth(I.curFuncScopeDepth),
      SavedSchemaDefs(I.schemaTypes_->snapshotDefs()),
      SavedCurFn(I.curFn_) {
    I.curFn_ = this;
}

Codegen::Impl::CGFunction::~CGFunction() {
    I.curFunc      = SavedFunc;
    I.curRetAlloca = SavedRetAlloca;
    I.curRetType   = SavedRetType;
    I.curFuncName  = SavedFuncName;
    I.namePrefix   = SavedNamePrefix;
    I.typeAliases  = std::move(SavedTypeAliases);
    I.consts       = std::move(SavedConsts);
    I.requiredConsts = std::move(SavedRequiredConsts);
    I.curFuncScopeDepth = SavedFuncScopeDepth;
    I.schemaTypes_->restoreDefs(std::move(SavedSchemaDefs));
    I.curFn_       = SavedCurFn;
}
