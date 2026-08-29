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
                std::function<bool(const plang::Type*)> OrdinalIsUnsigned,
                std::function<bool(const plang::ExprNode&)> ExprIsShortStr,
                std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap)
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
          OrdinalIsUnsigned(std::move(OrdinalIsUnsigned)),
          ExprIsShortStr(std::move(ExprIsShortStr)),
          ExprShortStrCap(std::move(ExprShortStrCap)) {}

    llvm::Value* emitBinary(const plang::BinaryExpr& e);
    llvm::Value* emitUnary(const plang::UnaryExpr& e);

private:
    /// True if the expression is a character string in any of the three
    /// shapes one can take: a literal, ISO §6.4.3.2's packed array[1..n] of
    /// char, or EP's string(n).
    bool exprIsStringLike(const plang::ExprNode& e);
    /// True if the expression's resolved type is a set.
    static bool exprIsSet(const plang::ExprNode& e);

    /// The two-block-plus-PHI CFG shape shared by every short-circuiting
    /// Boolean binary operator this class lowers: evaluate the left operand;
    /// branch straight to \c endBB without ever evaluating the right operand
    /// when it cannot change the result (left is false for an and-shaped
    /// operator, true for an or-shaped one); otherwise evaluate the right
    /// operand and join the two possible outcomes with a PHI.  \p isAnd
    /// selects and-shaped versus or-shaped short-circuiting -- it does not
    /// distinguish EP's and_then from Turbo's `{$B-}` and, or EP's or_else
    /// from Turbo's `{$B-}` or, since all four compile to this identical
    /// shape.  Two callers: the EP AndThen/OrElse arm (always, regardless of
    /// any switch -- EP has none) and the plain And/Or arm, conditionally,
    /// once RangeGuards confirms Turbo's BoolEval switch says to.
    ///
    /// Calls EmitExpr for \p e's right operand from inside this function, so
    /// per emitExpr's documented invariant (CGExprCore.h) the block that
    /// call leaves current is re-fetched via B.GetInsertBlock() afterward
    /// rather than assumed to still be the rhsBB this function created.
    llvm::Value* emitShortCircuit(const plang::BinaryExpr& e, bool isAnd);

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
    /// Turbo string[N]'s own predicate/capacity pair -- the sibling of
    /// ExprIsVarStr/ExprStrCapStatic just above, for concatenation and
    /// comparison's OWN ShortString branches (never routed through Schema's
    /// VarString-only strAddrAndCap/exprStrCapV).
    std::function<bool(const plang::ExprNode&)> ExprIsShortStr;
    std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap;
};
