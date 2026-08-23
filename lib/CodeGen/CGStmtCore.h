// CGStmtCore.h — ISO §6.8 statement emission: emitStmt (the central
// recursive-descent statement dispatcher), emitCompound, and
// resumeAfterTerminator, plus the 5 live LabelGotoEngine forwarders
// (ISO §6.8.1 non-local goto) that lived alongside them.
#pragma once

#include <functional>
#include <string>

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "CGAssign.h"
#include "CGControlFlow.h"
#include "CGDebugInfo.h"
#include "CGProcCall.h"
#include "CGWith.h"
#include "LabelGotoEngine.h"

namespace llvm { class BasicBlock; class Function; class Value; }
namespace plang {
struct StmtNode; struct CompoundStmt; struct GotoStmt; struct BlockNode;
}

class CGStmtCore {
public:
    CGStmtCore(llvm::LLVMContext& Ctx, llvm::IRBuilder<>& B, llvm::Function*& CurFn,
               CGAssign& Assign, CGControlFlow& ControlFlow, CGProcCall& ProcCall,
               CGWith& With, LabelGotoEngine& GotoEngine, CGDebugInfo& DbgInfo,
               std::function<bool()> IsTerminated,
               std::function<void(llvm::BasicBlock*)> BrIfNeeded,
               std::function<llvm::Value*(std::function<llvm::Value*()>)> WithStackScope)
        : Ctx(Ctx), B(B), CurFn(CurFn), Assign(Assign), ControlFlow(ControlFlow),
          ProcCall(ProcCall), With(With), GotoEngine(GotoEngine), DbgInfo(DbgInfo),
          IsTerminated(std::move(IsTerminated)), BrIfNeeded(std::move(BrIfNeeded)),
          WithStackScope(std::move(WithStackScope)) {}

    void emitStmt(const plang::StmtNode* stmt);
    void emitCompound(const plang::CompoundStmt& s);
    void resumeAfterTerminator();

    llvm::BasicBlock* getOrCreateLabel(const std::string& name) {
        return GotoEngine.getOrCreateLabel(name);
    }
    void openLabelScope(const plang::BlockNode& block, bool programBlock) {
        GotoEngine.openLabelScope(block, programBlock);
    }
    void emitLabelLanding() { GotoEngine.emitLabelLanding(); }
    void closeLabelScope() { GotoEngine.closeLabelScope(); }
    void emitGoto(const plang::GotoStmt& s) { GotoEngine.emitGoto(s); }

private:
    llvm::LLVMContext& Ctx;
    llvm::IRBuilder<>& B;
    llvm::Function*& CurFn;
    CGAssign& Assign;
    CGControlFlow& ControlFlow;
    CGProcCall& ProcCall;
    CGWith& With;
    LabelGotoEngine& GotoEngine;
    CGDebugInfo& DbgInfo;
    std::function<bool()> IsTerminated;
    std::function<void(llvm::BasicBlock*)> BrIfNeeded;
    std::function<llvm::Value*(std::function<llvm::Value*()>)> WithStackScope;
};
