#include "StringRuntime.h"

llvm::GlobalVariable* StringRuntime::internStrGV(const std::string& content) {
    auto it = StrGVs.find(content);
    if (it != StrGVs.end()) return it->second;

    auto* cda = llvm::ConstantDataArray::getString(Ctx, content, /*addNull=*/true);
    auto* gv  = new llvm::GlobalVariable(Mod, cda->getType(), /*isConst=*/true,
                                          llvm::GlobalValue::PrivateLinkage, cda);
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    gv->setAlignment(llvm::Align(1));
    StrGVs[content] = gv;
    return gv;
}

llvm::Value* StringRuntime::internStrPtr(const std::string& content) {
    auto* gv  = internStrGV(content);
    auto* gep = B.CreateConstInBoundsGEP2_64(gv->getValueType(), gv, 0, 0);
    return gep;
}

llvm::Constant* StringRuntime::internStrStruct(const std::string& content) {
    auto it = StructGVs.find(content);
    if (it != StructGVs.end()) return it->second;

    // Everything that handles a string value expects the address of a
    // { i64 length, [cap x i8] } — a bare run of bytes would be read as though
    // its first eight characters were the length.  A string constant is not
    // written to, so the whole struct can be built once, here.
    const auto cap = static_cast<int64_t>(content.size());
    auto* st  = StrStructTypeOf(cap);
    auto* i64Ty = llvm::Type::getInt64Ty(Ctx);
    auto* len = llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(cap), true);
    auto* dat = llvm::ConstantDataArray::getString(Ctx, content, /*addNull=*/false);
    auto* init = llvm::ConstantStruct::get(st, {len, dat});
    auto* gv  = new llvm::GlobalVariable(Mod, st, /*isConst=*/true,
                                         llvm::GlobalValue::PrivateLinkage, init);
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    StructGVs[content] = gv;
    return gv;
}
