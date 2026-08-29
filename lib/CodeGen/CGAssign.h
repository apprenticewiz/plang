// CGAssign.h — ISO §6.8.2.2 assignment-statement emission.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Alignment.h"

#include "plang/Basic/SourceLocation.h"

#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "ComplexOps.h"
#include "RangeCheckGuards.h"
#include "SchemaAccess.h"
#include "SchemaLayoutEngine.h"
#include "SetOps.h"
#include "StringCallMarshalling.h"
#include "StringRuntime.h"

namespace llvm { class Module; class Value; class AllocaInst; }
namespace plang { struct AssignStmt; struct ExprNode; }

class CGAssign {
public:
    CGAssign(llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
             SchemaAccess& Schema, SchemaLayoutEngine& SchemaLayout,
             StringCallMarshalling& StrCall, StringRuntime& Strings,
             CGTypes& Types, RangeCheckGuards& RangeGuards, SetOps& Sets,
             ComplexOps& Complex, CGSymbolTable& SymTab,
             llvm::IntegerType* I8Ty, llvm::IntegerType* I64Ty,
             llvm::Type* DblTy, llvm::PointerType* PtrTy,
             std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
             std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
             std::function<llvm::Value*(llvm::Value*)> ToI64,
             std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
             std::function<std::optional<llvm::Align>(const plang::ExprNode&)> PackedAccessAlign,
             std::function<bool(const plang::ExprNode&)> ExprIsVarStr,
             std::function<bool(const plang::ExprNode&)> ExprIsCharStr,
             std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen,
             std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic,
             std::function<bool(const plang::ExprNode&)> ExprIsShortStr,
             std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap)
        : Ctx(Ctx), Mod(Mod), B(B), Schema(Schema), SchemaLayout(SchemaLayout),
          StrCall(StrCall), Strings(Strings), Types(Types),
          RangeGuards(RangeGuards), Sets(Sets), Complex(Complex), SymTab(SymTab),
          I8Ty(I8Ty), I64Ty(I64Ty), DblTy(DblTy), PtrTy(PtrTy),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          ToI64(std::move(ToI64)), CreateEntryAlloca(std::move(CreateEntryAlloca)),
          PackedAccessAlign(std::move(PackedAccessAlign)),
          ExprIsVarStr(std::move(ExprIsVarStr)), ExprIsCharStr(std::move(ExprIsCharStr)),
          ExprCharStrLen(std::move(ExprCharStrLen)),
          ExprStrCapStatic(std::move(ExprStrCapStatic)),
          ExprIsShortStr(std::move(ExprIsShortStr)),
          ExprShortStrCap(std::move(ExprShortStrCap)) {}

    void emitAssign(const plang::AssignStmt& s);

    /// The guts of emitAssign, taking Target/Value/Loc directly instead of
    /// reading them off an AssignStmt -- emitAssign(s) is now a one-line
    /// forward to this.  TP-only: Exit(value) means exactly what an ordinary
    /// `FuncName := value` assignment to the enclosing function's result
    /// does (Sema's checkCallStmt Exit arm checks the value the same way
    /// checkAssign checks an ordinary target), and CGProcCall's own Exit
    /// handling calls this directly with a synthesized Target -- an IdentExpr
    /// naming the function, which emitLValue already resolves straight to
    /// CurRetAlloca via its own fast path -- rather than needing a real
    /// AssignStmt AST node whose Target and Value are siblings owned by one
    /// parent: Exit's value expression is already owned by the CallStmt, and
    /// synthesizing a second owner for it would be a lifetime hazard for no
    /// reason, since this only ever reads through Target/Value and never
    /// needs to own either.
    void emitAssignValue(const plang::ExprNode& Target,
                          const plang::ExprNode& Value,
                          plang::SourceLocation Loc);

private:
    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    SchemaAccess& Schema;
    SchemaLayoutEngine& SchemaLayout;
    StringCallMarshalling& StrCall;
    StringRuntime& Strings;
    CGTypes& Types;
    RangeCheckGuards& RangeGuards;
    SetOps& Sets;
    ComplexOps& Complex;
    CGSymbolTable& SymTab;
    llvm::IntegerType* I8Ty;
    llvm::IntegerType* I64Ty;
    llvm::Type* DblTy;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    std::function<std::optional<llvm::Align>(const plang::ExprNode&)> PackedAccessAlign;
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
    std::function<bool(const plang::ExprNode&)> ExprIsCharStr;
    std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen;
    std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic;
    /// Turbo string[N]'s own predicate/capacity pair -- see exprIsShortStr's
    /// doc comment (CodeGenImpl.h).
    std::function<bool(const plang::ExprNode&)> ExprIsShortStr;
    std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap;

    llvm::Constant* i64c(int64_t v) const {
        return llvm::ConstantInt::get(I64Ty, static_cast<uint64_t>(v), true);
    }
};
