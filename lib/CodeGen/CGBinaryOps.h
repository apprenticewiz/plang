// CGBinaryOps.h — ISO §6.7.2 binary and unary operators, including EP
// and_then/or_else short-circuit evaluation, **/pow exponentiation,
// EP §6.8.3.6 string concatenation, EP §6.8.3.5 string comparison, and
// ISO §6.7.2.4/§6.7.2.5 set operators.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "CGTypes.h"
#include "ComplexOps.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "SetOps.h"
#include "StringCallMarshalling.h"
#include "StringRuntime.h"

namespace llvm { class Function; class Value; class AllocaInst; }
namespace plang { struct BinaryExpr; struct UnaryExpr; struct ExprNode; struct Type; }

class CGBinaryOps {
public:
    CGBinaryOps(llvm::LLVMContext& Ctx, llvm::IRBuilder<>& B, llvm::Function*& CurFn,
                ComplexOps& Complex, SchemaAccess& Schema, StringCallMarshalling& StrCall,
                StringRuntime& Strings, CGTypes& Types, SetOps& Sets,
                RangeCheckGuards& RangeGuards, RuntimeFunctionCache& RtFns,
                llvm::IntegerType* I1Ty, llvm::IntegerType* I64Ty, llvm::IntegerType* I8Ty,
                llvm::Type* DblTy, llvm::PointerType* PtrTy,
                std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
                std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
                std::function<llvm::Value*(llvm::Value*)> EnsureI1,
                std::function<llvm::Value*(llvm::Value*)> ToDouble,
                std::function<llvm::Value*(llvm::Value*)> ToI64,
                std::function<llvm::Value*(llvm::Value*, llvm::Type*)> CoerceToType,
                std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
                std::function<llvm::Value*(llvm::Value*, const std::string&)> CreateDynStrAlloca,
                std::function<bool(const plang::ExprNode&)> ExprIsVarStr,
                std::function<bool(const plang::ExprNode&)> ExprIsCharStr,
                std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen,
                std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic,
                std::function<bool(const plang::Type*)> OrdinalIsUnsigned)
        : Ctx(Ctx), B(B), CurFn(CurFn), Complex(Complex), Schema(Schema), StrCall(StrCall),
          Strings(Strings), Types(Types), Sets(Sets), RangeGuards(RangeGuards), RtFns(RtFns),
          I1Ty(I1Ty), I64Ty(I64Ty), I8Ty(I8Ty), DblTy(DblTy), PtrTy(PtrTy),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          EnsureI1(std::move(EnsureI1)),
          ToDouble(std::move(ToDouble)), ToI64(std::move(ToI64)),
          CoerceToType(std::move(CoerceToType)),
          CreateEntryAlloca(std::move(CreateEntryAlloca)),
          CreateDynStrAlloca(std::move(CreateDynStrAlloca)),
          ExprIsVarStr(std::move(ExprIsVarStr)), ExprIsCharStr(std::move(ExprIsCharStr)),
          ExprCharStrLen(std::move(ExprCharStrLen)),
          ExprStrCapStatic(std::move(ExprStrCapStatic)),
          OrdinalIsUnsigned(std::move(OrdinalIsUnsigned)) {}

    llvm::Value* emitBinary(const plang::BinaryExpr& e);
    llvm::Value* emitUnary(const plang::UnaryExpr& e);

private:
    /// True if the expression is a character string in any of the three
    /// shapes one can take: a literal, ISO §6.4.3.2's packed array[1..n] of
    /// char, or EP's string(n).
    bool exprIsStringLike(const plang::ExprNode& e);
    /// True if the expression's resolved type is a set.
    static bool exprIsSet(const plang::ExprNode& e);

    llvm::Constant* i64c(int64_t v) const {
        return llvm::ConstantInt::get(I64Ty, static_cast<uint64_t>(v), true);
    }

    llvm::LLVMContext& Ctx;
    llvm::IRBuilder<>& B;
    llvm::Function*& CurFn;
    ComplexOps& Complex;
    SchemaAccess& Schema;
    StringCallMarshalling& StrCall;
    StringRuntime& Strings;
    CGTypes& Types;
    SetOps& Sets;
    RangeCheckGuards& RangeGuards;
    RuntimeFunctionCache& RtFns;
    llvm::IntegerType* I1Ty;
    llvm::IntegerType* I64Ty;
    llvm::IntegerType* I8Ty;
    llvm::Type* DblTy;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::Value*(llvm::Value*)> EnsureI1;
    std::function<llvm::Value*(llvm::Value*)> ToDouble;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    std::function<llvm::Value*(llvm::Value*, llvm::Type*)> CoerceToType;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    std::function<llvm::Value*(llvm::Value*, const std::string&)> CreateDynStrAlloca;
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
    std::function<bool(const plang::ExprNode&)> ExprIsCharStr;
    std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen;
    std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic;
    std::function<bool(const plang::Type*)> OrdinalIsUnsigned;
};
