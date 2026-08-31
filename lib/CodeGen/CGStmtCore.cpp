#include "CGStmtCore.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"

#include "CodegenICE.h"

using namespace plang;

void CGStmtCore::emitStmt(const StmtNode* stmt) {
    if (!stmt || IsTerminated()) return;

    // -g: one hook for every statement kind, not one per kind -- IRBuilder's
    // own Insert() attaches whatever SetCurrentDebugLocation last set to
    // every instruction it creates from here on, automatically, so this is
    // the only place a location needs to be set at all.
    DbgInfo.setLocation(stmt->Loc);

    // The two statement kinds that evaluate an arbitrary expression and then
    // finish are the two that can leave a run-time-sized string temporary
    // behind, and giving the stack back here is what keeps one inside a loop
    // costing a fixed amount rather than one allocation per iteration.
    //
    // Deliberately not around every statement: a labeled statement moves the
    // insertion point to a block that any goto may enter, so the save would not
    // dominate the restore and the IR would not verify.  A structured statement
    // is covered by the scopes of the simple statements inside it.
    if (auto* s = llvm::dyn_cast<AssignStmt>(stmt)) {
        WithStackScope([&]() -> llvm::Value* { Assign.emitAssign(*s); return nullptr; });
        return;
    }
    if (auto* s = llvm::dyn_cast<CompoundStmt>(stmt))  { emitCompound(*s); return; }
    if (auto* s = llvm::dyn_cast<IfStmt>(stmt))        { ControlFlow.emitIf(*s);       return; }
    if (auto* s = llvm::dyn_cast<WhileStmt>(stmt))     { ControlFlow.emitWhile(*s);    return; }
    if (auto* s = llvm::dyn_cast<ForStmt>(stmt))       { ControlFlow.emitFor(*s);      return; }
    if (auto* s = llvm::dyn_cast<ForInStmt>(stmt))    { ControlFlow.emitForIn(*s);    return; }
    if (auto* s = llvm::dyn_cast<RepeatStmt>(stmt))    { ControlFlow.emitRepeat(*s);   return; }
    if (auto* s = llvm::dyn_cast<CallStmt>(stmt)) {
        WithStackScope([&]() -> llvm::Value* { ProcCall.emitCallStmt(*s); return nullptr; });
        return;
    }
    if (auto* s = llvm::dyn_cast<GotoStmt>(stmt)) { emitGoto(*s); return; }
    if (auto* s = llvm::dyn_cast<LabeledStmt>(stmt)) {
        auto* lblBB = getOrCreateLabel("lbl_" + s->Label);
        BrIfNeeded(lblBB);
        B.SetInsertPoint(lblBB);
        emitStmt(s->Stmt.get());
        return;
    }
    if (auto* s = llvm::dyn_cast<CaseStmt>(stmt)) { ControlFlow.emitCase(*s); return; }
    if (auto* s = llvm::dyn_cast<WithStmt>(stmt)) { With.emitWith(*s); return; }
    // Turbo Tier 5, Cluster A item 4: 'Obj.Method(args);' / 'P^.Method(args);'
    // / the bare no-parens 'Obj.Method;' -- a static/direct call to Method's
    // own mangled symbol; see CGProcCall::emitMethodCallStmt's own comment.
    if (auto* s = llvm::dyn_cast<MethodCallStmt>(stmt)) {
        WithStackScope([&]() -> llvm::Value* { ProcCall.emitMethodCallStmt(*s); return nullptr; });
        return;
    }
    // Turbo Tier 5, Cluster A item 5: 'inherited [Method[(args)]];' -- a
    // static call to the direct ancestor's own implementation; see
    // CGProcCall::emitInheritedCallStmt's own comment.
    if (auto* s = llvm::dyn_cast<InheritedCallStmt>(stmt)) {
        WithStackScope([&]() -> llvm::Value* { ProcCall.emitInheritedCallStmt(*s); return nullptr; });
        return;
    }
    // Falling off the end would drop the statement from the program silently.
    codegenICE("unhandled statement kind in codegen");
}

void CGStmtCore::emitCompound(const CompoundStmt& s) {
    for (const auto& st : s.Stmts) {
        if (IsTerminated()) resumeAfterTerminator();
        emitStmt(st.get());
    }
}

/// Open a fresh block to continue emitting into after a goto or a return has
/// closed the current one.  Stopping instead would be wrong: a later statement
/// can carry a label, and skipping it leaves the block that a forward goto
/// already branched to empty and unterminated.  A block nothing reaches costs
/// nothing — LLVM discards it.
void CGStmtCore::resumeAfterTerminator() {
    B.SetInsertPoint(
        llvm::BasicBlock::Create(Ctx, "unreachable", CurFn));
}
