// ConstFold.h — compile-time constant folding for codegen.
//
// Fully stateless: every dependency is an explicit parameter, nothing here
// touches Codegen::Impl.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"

namespace plang {
struct ExprNode;
}
using plang::ExprNode;

std::optional<int64_t> tryEvalConstInt(
        const ExprNode& e,
        const std::unordered_map<std::string, llvm::Value*>* known = nullptr);

int64_t evalConstInt(const ExprNode& e, int64_t fallback,
                     const std::unordered_map<std::string, llvm::Value*>* known = nullptr);

// EP §6.8.2: evaluate a nonvarying (constant) expression to an LLVM Constant.
// Returns null if the expression cannot be folded at compile time (e.g. variable
// references, function calls).  Previously-defined constants are resolved via
// the 'known' map (lowercase name -> llvm::Constant*).
llvm::Constant* evalConst(
        const ExprNode& e,
        const std::unordered_map<std::string, llvm::Value*>& known,
        llvm::LLVMContext& ctx,
        llvm::IntegerType* i64Ty,
        llvm::Type* dblTy);
