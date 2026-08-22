#include "CodeGenImpl.h"
#include "plang/Basic/SemaUtil.h"
using namespace plang;

// See NumStmtKinds in AstBase.h.
static_assert(NumStmtKinds == 12, "a new statement needs a case in emitStmt");

// ====================================================================
// Statement emission
// ====================================================================

void Codegen::Impl::emitStmt(const StmtNode* stmt) {
    if (!stmt || isTerminated()) return;

    // -g: one hook for every statement kind, not one per kind -- IRBuilder's
    // own Insert() attaches whatever SetCurrentDebugLocation last set to
    // every instruction it creates from here on, automatically, so this is
    // the only place a location needs to be set at all.
    dbgInfo_->setLocation(stmt->Loc);

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
        StackScope frame(*this); emitAssign(*s);   return; }
    if (auto* s = llvm::dyn_cast<CompoundStmt>(stmt))  { emitCompound(*s); return; }
    if (auto* s = llvm::dyn_cast<IfStmt>(stmt))        { emitIf(*s);       return; }
    if (auto* s = llvm::dyn_cast<WhileStmt>(stmt))     { emitWhile(*s);    return; }
    if (auto* s = llvm::dyn_cast<ForStmt>(stmt))       { emitFor(*s);      return; }
    if (auto* s = llvm::dyn_cast<ForInStmt>(stmt))    { emitForIn(*s);    return; }
    if (auto* s = llvm::dyn_cast<RepeatStmt>(stmt))    { emitRepeat(*s);   return; }
    if (auto* s = llvm::dyn_cast<CallStmt>(stmt)) {
        StackScope frame(*this); emitCallStmt(*s); return; }
    if (auto* s = llvm::dyn_cast<GotoStmt>(stmt)) { emitGoto(*s); return; }
    if (auto* s = llvm::dyn_cast<LabeledStmt>(stmt)) {
        auto* lblBB = getOrCreateLabel("lbl_" + s->Label);
        brIfNeeded(lblBB);
        builder.SetInsertPoint(lblBB);
        emitStmt(s->Stmt.get());
        return;
    }
    if (auto* s = llvm::dyn_cast<CaseStmt>(stmt)) { emitCase(*s); return; }
    if (auto* s = llvm::dyn_cast<WithStmt>(stmt)) { emitWith(*s); return; }
    // Falling off the end would drop the statement from the program silently.
    codegenICE("unhandled statement kind in codegen");
}

bool Codegen::Impl::declaresLabel(const BlockNode& block, const std::string& label) {
    return LabelGotoEngine::declaresLabel(block, label);
}

std::set<std::string> Codegen::Impl::nonLocalTargets(const BlockNode& block) {
    return LabelGotoEngine::nonLocalTargets(block);
}

int64_t Codegen::Impl::gotoDispatchValue(const std::string& label) {
    return LabelGotoEngine::gotoDispatchValue(label);
}

llvm::BasicBlock* Codegen::Impl::getOrCreateLabel(const std::string& name) {
    return gotoEngine_->getOrCreateLabel(name);
}

void Codegen::Impl::openLabelScope(const BlockNode& block, bool programBlock) {
    gotoEngine_->openLabelScope(block, programBlock);
}

void Codegen::Impl::emitLabelLanding() {
    gotoEngine_->emitLabelLanding();
}

void Codegen::Impl::closeLabelScope() {
    gotoEngine_->closeLabelScope();
}

void Codegen::Impl::pinLocalsToMemory(llvm::Function* f) {
    LabelGotoEngine::pinLocalsToMemory(f);
}

void Codegen::Impl::emitGoto(const GotoStmt& s) {
    gotoEngine_->emitGoto(s);
}

void Codegen::Impl::emitCompound(const CompoundStmt& s) {
    for (const auto& st : s.Stmts) {
        if (isTerminated()) resumeAfterTerminator();
        emitStmt(st.get());
    }
}

/// Open a fresh block to continue emitting into after a goto or a return has
/// closed the current one.  Stopping instead would be wrong: a later statement
/// can carry a label, and skipping it leaves the block that a forward goto
/// already branched to empty and unterminated.  A block nothing reaches costs
/// nothing — LLVM discards it.
void Codegen::Impl::resumeAfterTerminator() {
    builder.SetInsertPoint(
        llvm::BasicBlock::Create(ctx, "unreachable", curFunc));
}
