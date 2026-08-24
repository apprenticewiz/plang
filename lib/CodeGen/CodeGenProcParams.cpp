// CodeGenProcParams.cpp — ISO §6.7.1 static-link frame construction.
//
// The procedural-parameter ABI (the {entry point, frame} pair, its thunk,
// and calling through it) and EP §6.7.3.7 conformant-array marshalling
// moved to ClosureAndCallABI. What's left here is buildStaticLinkFrame,
// which resolves the closure-capture loop's own state
// (nestedFunctions_/funcOuterVarNames_/funcOuterVarDepths_/
// outerVarBindings/findVarInFunctionScope) -- this project's standing
// extra-caution zone -- so it stays a plain Impl method, reached from
// ClosureAndCallABI only through a narrow closure.

#include "CodeGenImpl.h"

llvm::Value* Codegen::Impl::buildStaticLinkFrame(const std::string& mangledName) {
    if (!nestedFunctions_.count(mangledName)) return nullptr;

    const auto& varNames = funcOuterVarNames_.at(mangledName);
    std::vector<llvm::Type*> ptrFields(varNames.size(), ptrTy);
    auto* frameTy     = llvm::StructType::get(ctx, ptrFields);
    auto* frameAlloca = createEntryAlloca(frameTy, "frame");
    auto* zero        = llvm::ConstantInt::get(i32Ty, 0);

    // Each slot is for a particular VARIABLE, and a name does not name one:
    // two nesting levels may declare the same one.  The depth recorded beside
    // the name does, being fixed by the nesting and the same number in every
    // activation.
    //
    // Resolving by name alone was wrong twice over.  With `b` and `c` both
    // nested in `a` and `c` declaring its own `n`, `c` calling `b` handed b the
    // address of c's n.  And where the callee's frame had two slots spelled
    // alike, both were filled with the innermost binding, so the outer variable
    // never travelled at all: three levels deep, a procedure reading its
    // grandparent's `n` saw its parent's.
    //
    // A depth this activation has no binding for is one declared HERE, and
    // findVar answers it.
    const auto DIt = funcOuterVarDepths_.find(mangledName);
    const std::vector<size_t>* varDepths =
        DIt != funcOuterVarDepths_.end() ? &DIt->second : nullptr;

    for (size_t fi = 0; fi < varNames.size(); ++fi) {
        const VarEntry* ve = nullptr;
        if (varDepths && fi < varDepths->size()) {
            const auto it = curFn_->OuterVarBindings.find(
                {(*varDepths)[fi], toLower(varNames[fi])});
            if (it != curFn_->OuterVarBindings.end()) ve = &it->second;
        }
        // A slot this activation has no capture for is one declared HERE --
        // so the search starts at this function's own scope, not at whatever
        // scope the call happens to sit in.  Starting at the innermost let a
        // with-statement answer: calling a nested procedure from inside
        // `with r do` handed it the address of r's field of that name, and the
        // increments meant for the enclosing variable landed in the record.
        if (!ve) ve = findVarInFunctionScope(varNames[fi]);
        if (!ve)
            codegenICE("captured variable '" + varNames[fi]
                       + "' is not visible at the call site of '"
                       + mangledName + "'");
        // A frame slot is an address; anything reaching here without one would
        // be stored as a null the callee then reads through.
        if (!ve->ptr)
            codegenICE("captured variable '" + varNames[fi]
                       + "' has no storage to link to '" + mangledName + "'");
        auto* slot = builder.CreateGEP(
            frameTy, frameAlloca,
            {zero, llvm::ConstantInt::get(i32Ty, static_cast<unsigned>(fi))},
            "frame." + varNames[fi]);
        builder.CreateStore(ve->ptr, slot);
    }
    return frameAlloca;
}
