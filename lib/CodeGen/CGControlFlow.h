// CGControlFlow.h — structured-statement emission: if/while/for/for-in/
// repeat/case (ISO §6.8.3, EP §6.9.3.9.3's `for v in set-expr do`).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "plang/Basic/LangOptions.h"

#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "RuntimeFunctionCache.h"
#include "SetOps.h"

namespace llvm { class Function; class Value; class AllocaInst; }
namespace plang {
struct IfStmt; struct WhileStmt; struct ForStmt; struct ForInStmt;
struct RepeatStmt; struct CaseStmt; struct StmtNode; struct ExprNode;
struct Type;
}

class CGControlFlow {
public:
    CGControlFlow(llvm::LLVMContext& Ctx, llvm::IRBuilder<>& B,
                  llvm::Function*& CurFn,
                  CGSymbolTable& SymTab, CGTypes& Types, SetOps& Sets,
                  RuntimeFunctionCache& RtFns,
                  llvm::IntegerType* I1Ty, llvm::IntegerType* I64Ty,
                  const plang::LangOptions& Opts,
                  std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
                  std::function<void(const plang::StmtNode*)> EmitStmt,
                  std::function<llvm::Value*(llvm::Value*)> EnsureI1,
                  std::function<llvm::Value*(llvm::Value*)> ToI64,
                  std::function<llvm::Value*(llvm::Value*, llvm::Type*)> CoerceToType,
                  std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
                  std::function<void()> ResumeAfterTerminator,
                  std::function<bool()> IsTerminated,
                  std::function<void(llvm::BasicBlock*)> BrIfNeeded,
                  std::function<bool(const plang::Type*)> OrdinalIsUnsigned,
                  std::function<llvm::Value*(std::function<llvm::Value*()>)> WithStackScope)
        : Ctx(Ctx), B(B), CurFn(CurFn), SymTab(SymTab), Types(Types), Sets(Sets),
          RtFns(RtFns), I1Ty(I1Ty), I64Ty(I64Ty), Opts(Opts),
          EmitExpr(std::move(EmitExpr)), EmitStmt(std::move(EmitStmt)),
          EnsureI1(std::move(EnsureI1)), ToI64(std::move(ToI64)),
          CoerceToType(std::move(CoerceToType)),
          CreateEntryAlloca(std::move(CreateEntryAlloca)),
          ResumeAfterTerminator(std::move(ResumeAfterTerminator)),
          IsTerminated(std::move(IsTerminated)), BrIfNeeded(std::move(BrIfNeeded)),
          OrdinalIsUnsigned(std::move(OrdinalIsUnsigned)),
          WithStackScope(std::move(WithStackScope)) {}

    void emitIf(const plang::IfStmt& s);
    void emitWhile(const plang::WhileStmt& s);
    void emitFor(const plang::ForStmt& s);
    void emitForIn(const plang::ForInStmt& s);
    void emitRepeat(const plang::RepeatStmt& s);
    void emitCase(const plang::CaseStmt& s);

private:
    llvm::LLVMContext& Ctx;
    llvm::IRBuilder<>& B;
    llvm::Function*& CurFn;
    CGSymbolTable& SymTab;
    CGTypes& Types;
    SetOps& Sets;
    RuntimeFunctionCache& RtFns;
    llvm::IntegerType* I1Ty;
    llvm::IntegerType* I64Ty;
    const plang::LangOptions& Opts;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<void(const plang::StmtNode*)> EmitStmt;
    std::function<llvm::Value*(llvm::Value*)> EnsureI1;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    std::function<llvm::Value*(llvm::Value*, llvm::Type*)> CoerceToType;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    std::function<void()> ResumeAfterTerminator;
    std::function<bool()> IsTerminated;
    std::function<void(llvm::BasicBlock*)> BrIfNeeded;
    std::function<bool(const plang::Type*)> OrdinalIsUnsigned;
    std::function<llvm::Value*(std::function<llvm::Value*()>)> WithStackScope;
};
