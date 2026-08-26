#include "RuntimeFunctionCache.h"

llvm::Function* RuntimeFunctionCache::getExternFn(const std::string& name,
                                                    llvm::FunctionType* ty) {
    auto it = Fns.find(name);
    if (it != Fns.end()) return it->second;
    auto* f = llvm::Function::Create(ty, llvm::Function::ExternalLinkage, name, &Mod);
    Fns[name] = f;
    return f;
}

llvm::Function* RuntimeFunctionCache::getRTMathRR(const std::string& name) {
    auto* dblTy = llvm::Type::getDoubleTy(Ctx);
    auto* ty = llvm::FunctionType::get(dblTy, {dblTy}, false);
    return getExternFn(name, ty);
}

llvm::Function* RuntimeFunctionCache::getRTMathRI(const std::string& name) {
    auto* dblTy = llvm::Type::getDoubleTy(Ctx);
    auto* i64Ty = llvm::Type::getInt64Ty(Ctx);
    auto* ty = llvm::FunctionType::get(i64Ty, {dblTy}, false);
    return getExternFn(name, ty);
}

llvm::Function* RuntimeFunctionCache::getRTMathII(const std::string& name) {
    auto* i64Ty = llvm::Type::getInt64Ty(Ctx);
    auto* ty = llvm::FunctionType::get(i64Ty, {i64Ty}, false);
    return getExternFn(name, ty);
}

llvm::Function* RuntimeFunctionCache::getRuntimeFn(const std::string& name,
                                                     llvm::Type* argTy) {
    auto* voidTy = llvm::Type::getVoidTy(Ctx);
    auto* ty = argTy
        ? llvm::FunctionType::get(voidTy, {argTy}, false)
        : llvm::FunctionType::get(voidTy, {}, false);
    return getExternFn(name, ty);
}

llvm::Function* RuntimeFunctionCache::getRuntimeBoolFn(const std::string& name) {
    auto* i8Ty = llvm::Type::getInt8Ty(Ctx);
    auto* ty = llvm::FunctionType::get(i8Ty, {}, false);
    return getExternFn(name, ty);
}

llvm::Function* RuntimeFunctionCache::getExternFnN(const std::string& name,
                                                     llvm::Type* retTy,
                                                     std::vector<llvm::Type*> params) {
    auto* ty = llvm::FunctionType::get(retTy, params, false);
    return getExternFn(name, ty);
}

llvm::Function* RuntimeFunctionCache::getRuntimeNewFn() {
    auto* ptrTy = llvm::PointerType::get(Ctx, 0);
    auto* i64Ty = llvm::Type::getInt64Ty(Ctx);
    auto* ty = llvm::FunctionType::get(ptrTy, {i64Ty}, false);
    return getExternFn("plang_new", ty);
}

llvm::Function* RuntimeFunctionCache::getRuntimeDisposeFn() {
    auto* ptrTy = llvm::PointerType::get(Ctx, 0);
    auto* voidTy = llvm::Type::getVoidTy(Ctx);
    auto* ty = llvm::FunctionType::get(voidTy, {ptrTy}, false);
    return getExternFn("plang_dispose", ty);
}

llvm::Function* RuntimeFunctionCache::getRuntimeHaltFn() {
    auto* i64Ty = llvm::Type::getInt64Ty(Ctx);
    auto* voidTy = llvm::Type::getVoidTy(Ctx);
    auto* ty = llvm::FunctionType::get(voidTy, {i64Ty}, false);
    auto* f = getExternFn("plang_halt", ty);
    f->addFnAttr(llvm::Attribute::NoReturn);
    return f;
}

llvm::Function* RuntimeFunctionCache::getConfArrMarkFn() {
    auto* i64Ty = llvm::Type::getInt64Ty(Ctx);
    auto* ty = llvm::FunctionType::get(i64Ty, {}, false);
    return getExternFn("plang_confarr_mark", ty);
}

llvm::Function* RuntimeFunctionCache::getConfArrPushFn() {
    auto* ptrTy = llvm::PointerType::get(Ctx, 0);
    auto* voidTy = llvm::Type::getVoidTy(Ctx);
    auto* ty = llvm::FunctionType::get(voidTy, {ptrTy}, false);
    return getExternFn("plang_confarr_push", ty);
}

llvm::Function* RuntimeFunctionCache::getConfArrUnwindFn() {
    auto* i64Ty = llvm::Type::getInt64Ty(Ctx);
    auto* voidTy = llvm::Type::getVoidTy(Ctx);
    auto* ty = llvm::FunctionType::get(voidTy, {i64Ty}, false);
    return getExternFn("plang_confarr_unwind", ty);
}
