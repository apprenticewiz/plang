#include "CodeGenImpl.h"
using namespace plang;

// ====================================================================
// String interning
// ====================================================================

llvm::GlobalVariable* Codegen::Impl::internStrGV(const std::string& content) {
    return strings_->internStrGV(content);
}

llvm::Value* Codegen::Impl::internStrPtr(const std::string& content) {
    return strings_->internStrPtr(content);
}

llvm::Constant* Codegen::Impl::internStrStruct(const std::string& content) {
    return strings_->internStrStruct(content);
}

// ====================================================================
// Sets (ISO §6.7.2.4)
//
// A set is a flat bitmask, one bit per ordinal of the base type.  Every
// operation below is emitted inline: the bitwise ones map directly to LLVM
// instructions, which avoids having to define a calling convention for a
// 256-bit value crossing into the C runtime.
//
// Membership and construction clamp their ordinal so an out-of-range value
// yields the empty set or false rather than a shift past the type width,
// which LLVM treats as poison.
// ====================================================================

llvm::Value* Codegen::Impl::toSetWidth(llvm::Value* v) {
    return setOps_->toSetWidth(v);
}

llvm::Value* Codegen::Impl::clampToSetWidth(llvm::Value* v) {
    return setOps_->clampToSetWidth(v);
}

int64_t Codegen::Impl::setBaseOf(const ExprNode& e) {
    return setOps_->setBaseOf(e);
}

llvm::Value* Codegen::Impl::alignSet(llvm::Value* v, int64_t from, int64_t to) {
    return setOps_->alignSet(v, from, to);
}

llvm::Value* Codegen::Impl::setBitIndex(llvm::Value* ordinal, int64_t base) {
    return setOps_->setBitIndex(ordinal, base);
}

llvm::Value* Codegen::Impl::emitSetSingleton(llvm::Value* ordinal, int64_t base) {
    return setOps_->emitSetSingleton(ordinal, base);
}

llvm::Value* Codegen::Impl::emitSetRange(llvm::Value* lo, llvm::Value* hi,
                                         int64_t base) {
    return setOps_->emitSetRange(lo, hi, base);
}

llvm::Value* Codegen::Impl::emitSetMember(llvm::Value* ordinal, llvm::Value* set,
                                          int64_t base) {
    return setOps_->emitSetMember(ordinal, set, base);
}

llvm::Value* Codegen::Impl::emitSetBinary(TokenKind op, llvm::Value* a,
                                          llvm::Value* b) {
    return setOps_->emitSetBinary(op, a, b);
}

// ====================================================================
// ISO runtime checks
// ====================================================================

void Codegen::Impl::emitGuard(llvm::Value* failCond, const char* name,
                              llvm::function_ref<void()> emitFail) {
    rangeGuards_->emitGuard(failCond, name, emitFail);
}

void Codegen::Impl::emitDivZeroCheck(llvm::Value* divisor, const char* op) {
    rangeGuards_->emitDivZeroCheck(divisor, op);
}

void Codegen::Impl::emitModDivisorCheck(llvm::Value* divisor) {
    rangeGuards_->emitModDivisorCheck(divisor);
}

void Codegen::Impl::emitNilCheck(llvm::Value* ptr) {
    rangeGuards_->emitNilCheck(ptr);
}

void Codegen::Impl::emitRangeCheck(llvm::Value* val, int64_t lo, int64_t hi,
                                   bool isIndex, SourceLocation Loc,
                                   std::optional<bool> valSigned) {
    rangeGuards_->emitRangeCheck(val, lo, hi, isIndex, Loc, valSigned);
}

void Codegen::Impl::emitRangeCheckDyn(llvm::Value* val, llvm::Value* lo,
                                      llvm::Value* hi, bool isIndex,
                                      SourceLocation Loc,
                                      std::optional<bool> valSigned) {
    rangeGuards_->emitRangeCheckDyn(val, lo, hi, isIndex, Loc, valSigned);
}

// ====================================================================
// External function declarations
// ====================================================================

llvm::Function* Codegen::Impl::getExternFn(const std::string& name, llvm::FunctionType* ty) {
    return runtimeFns_->getExternFn(name, ty);
}

llvm::Function* Codegen::Impl::getRTMathRR(const std::string& name) {
    return runtimeFns_->getRTMathRR(name);
}

llvm::Function* Codegen::Impl::getRTMathRI(const std::string& name) {
    return runtimeFns_->getRTMathRI(name);
}

llvm::Function* Codegen::Impl::getRTMathII(const std::string& name) {
    return runtimeFns_->getRTMathII(name);
}

llvm::Function* Codegen::Impl::getRuntimeFn(const std::string& name, llvm::Type* argTy) {
    return runtimeFns_->getRuntimeFn(name, argTy);
}

llvm::Function* Codegen::Impl::getRuntimeBoolFn(const std::string& name) {
    return runtimeFns_->getRuntimeBoolFn(name);
}

llvm::Function* Codegen::Impl::getExternFnN(const std::string& name,
                                              llvm::Type* retTy,
                                              std::vector<llvm::Type*> params) {
    return runtimeFns_->getExternFnN(name, retTy, std::move(params));
}

llvm::Function* Codegen::Impl::getRuntimeNewFn() {
    return runtimeFns_->getRuntimeNewFn();
}

llvm::Function* Codegen::Impl::getRuntimeDisposeFn() {
    return runtimeFns_->getRuntimeDisposeFn();
}

llvm::Function* Codegen::Impl::getRuntimeHaltFn() {
    return runtimeFns_->getRuntimeHaltFn();
}

llvm::Function* Codegen::Impl::getConfArrMarkFn() {
    return runtimeFns_->getConfArrMarkFn();
}

llvm::Function* Codegen::Impl::getConfArrPushFn() {
    return runtimeFns_->getConfArrPushFn();
}

llvm::Function* Codegen::Impl::getConfArrUnwindFn() {
    return runtimeFns_->getConfArrUnwindFn();
}
