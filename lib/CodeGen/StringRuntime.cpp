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
    auto* len = llvm::ConstantInt::get(i64Ty(), static_cast<uint64_t>(cap), true);
    auto* dat = llvm::ConstantDataArray::getString(Ctx, content, /*addNull=*/false);
    auto* init = llvm::ConstantStruct::get(st, {len, dat});
    auto* gv  = new llvm::GlobalVariable(Mod, st, /*isConst=*/true,
                                         llvm::GlobalValue::PrivateLinkage, init);
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    StructGVs[content] = gv;
    return gv;
}

llvm::Function* StringRuntime::getStrFn(const std::string& name, llvm::Type* retTy,
                                         std::initializer_list<llvm::Type*> argTys) {
    return RtFns.getExternFnN(name, retTy, argTys);
}

llvm::Value* StringRuntime::strLoadLen(llvm::Value* strPtr) {
    (void)B.CreateStructGEP(
        llvm::StructType::get(Ctx, {i64Ty(), llvm::ArrayType::get(i8Ty(), 0)}),
        strPtr, 0, "len.ptr");
    // Use a plain load via GEP into the ptr (opaque pointers: just load i64 at offset 0)
    return B.CreateLoad(i64Ty(), strPtr, "str.len");
}

llvm::Value* StringRuntime::strDataPtr(llvm::Value* strPtr) {
    // data is at byte offset 8 (after the i64 length field)
    auto* i8Ptr = B.CreateConstGEP1_64(i8Ty(), strPtr, 8, "str.data");
    return i8Ptr;
}

void StringRuntime::emitStrAssign(llvm::Value* dst, llvm::Value* capDst,
                                   llvm::Value* src, llvm::Value* capSrc) {
    auto* fn = getStrFn("plang_str_assign",
        llvm::Type::getVoidTy(Ctx), {ptrTy(), i64Ty(), ptrTy(), i64Ty()});
    B.CreateCall(fn, {dst, capDst, src, capSrc});
}

void StringRuntime::emitStrFromCStr(llvm::Value* dst, llvm::Value* cap,
                                    llvm::Value* cstr) {
    auto* fn = getStrFn("plang_str_from_cstr",
        llvm::Type::getVoidTy(Ctx), {ptrTy(), i64Ty(), ptrTy()});
    B.CreateCall(fn, {dst, cap, cstr});
}

void StringRuntime::emitStrFromBytes(llvm::Value* dst, llvm::Value* cap,
                                     llvm::Value* cstr, llvm::Value* len) {
    auto* fn = getStrFn("plang_str_from_bytes",
        llvm::Type::getVoidTy(Ctx), {ptrTy(), i64Ty(), ptrTy(), i64Ty()});
    B.CreateCall(fn, {dst, cap, cstr, len});
}

void StringRuntime::emitStrFromChar(llvm::Value* dst, llvm::Value* cap,
                                    llvm::Value* c) {
    auto* fn = getStrFn("plang_str_from_char",
        llvm::Type::getVoidTy(Ctx), {ptrTy(), i64Ty(), i8Ty()});
    B.CreateCall(fn, {dst, cap, c});
}
