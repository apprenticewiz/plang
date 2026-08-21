#include "LabelGotoEngine.h"

#include <algorithm>
#include <cstdlib>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/SemaUtil.h"

#include "CodegenICE.h"

using namespace plang;

bool LabelGotoEngine::declaresLabel(const BlockNode& block, const std::string& label) {
    // The parser reduced every label to its apparent integral value, so two
    // spellings of one label are one string here.
    return std::ranges::find(block.Labels, label) != block.Labels.end();
}

void LabelGotoEngine::scanNonLocalTargets(const BlockNode& inner, const BlockNode& block,
                                           std::set<std::string>& found) {
    for (const auto& proc : inner.Procs) {
        if (!proc->Body) continue;
        // A procedure declaring the label itself means its own, and every goto
        // below it names that one rather than the outer block's.
        walkStmts(proc->Body->Body.get(), [&](const StmtNode* s) {
            if (auto* g = llvm::dyn_cast<GotoStmt>(s))
                if (declaresLabel(block, g->Label)
                        && !declaresLabel(*proc->Body, g->Label))
                    found.insert(g->Label);
        });
        scanNonLocalTargets(*proc->Body, block, found);
    }
}

std::set<std::string> LabelGotoEngine::nonLocalTargets(const BlockNode& block) {
    std::set<std::string> found;
    scanNonLocalTargets(block, block, found);
    return found;
}

int64_t LabelGotoEngine::gotoDispatchValue(const std::string& label) {
    return std::strtoll(label.c_str(), nullptr, 10) + 1;
}

llvm::BasicBlock* LabelGotoEngine::getOrCreateLabel(const std::string& name) {
    auto it = LabelBlocks.find(name);
    if (it != LabelBlocks.end()) return it->second;
    auto* bb = llvm::BasicBlock::Create(Ctx, name, CurFn);
    LabelBlocks[name] = bb;
    return bb;
}

void LabelGotoEngine::openLabelScope(const BlockNode& block, bool programBlock) {
    const std::string bufName = "goto.buf$" + std::to_string(LabelOwners.size());
    LabelOwners.push_back({&block, {}, nullptr});
    if (nonLocalTargets(block).empty()) return;

    auto* bufTy = llvm::ArrayType::get(i64Ty(), GotoBufWords);
    llvm::Value* buf = nullptr;
    if (programBlock) {
        // The program block is entered once and stays entered for the run, so
        // one buffer for the whole program is one per activation.  It has to
        // exist before the procedures that jump to it are emitted, and main is
        // emitted after them, so it cannot live in main's frame.
        buf = new llvm::GlobalVariable(Mod, bufTy, /*isConstant=*/false,
                                       llvm::GlobalValue::PrivateLinkage,
                                       llvm::Constant::getNullValue(bufTy),
                                       "plang.goto.buf");
    } else {
        // A procedure may have several activations of itself outstanding, each
        // with its own block to return to, so its buffer belongs to the frame.
        buf = B.CreateAlloca(bufTy, nullptr, "goto.buf");
    }
    DefineBuf(bufName, buf, bufTy);
    LabelOwners.back().BufName = bufName;
    if (!programBlock) emitLabelLanding();
}

void LabelGotoEngine::emitLabelLanding() {
    if (LabelOwners.empty() || LabelOwners.back().BufName.empty()) return;
    llvm::Value* buf = LookupBuf(LabelOwners.back().BufName);
    if (!buf) return;

    // ISO §6.8.1 permits the jump only to a label at the outermost level of
    // the statement part, so the landing pad may sit here, ahead of the body,
    // and reach every target with a branch.  It sits after the block's
    // initialization because a goto landing here resumes the block rather than
    // starting it again.
    auto* setjmpFn = RtFns.getExternFnN("_setjmp", i32Ty(), {ptrTy()});
    if (auto* f = llvm::dyn_cast<llvm::Function>(setjmpFn))
        f->addFnAttr(llvm::Attribute::ReturnsTwice);
    auto* where = B.CreateCall(setjmpFn, {buf}, "goto.where");
    where->addFnAttr(llvm::Attribute::ReturnsTwice);

    auto* body = llvm::BasicBlock::Create(Ctx, "goto.body", CurFn);
    LabelOwners.back().Dispatch = B.CreateSwitch(where, body);
    B.SetInsertPoint(body);
}

void LabelGotoEngine::closeLabelScope() {
    if (auto* dispatch = LabelOwners.back().Dispatch) {
        for (const auto& label : nonLocalTargets(*LabelOwners.back().Block))
            dispatch->addCase(
                llvm::ConstantInt::get(i32Ty(), gotoDispatchValue(label)),
                getOrCreateLabel("lbl_" + label));
        pinLocalsToMemory(CurFn);
    }
    LabelOwners.pop_back();
}

void LabelGotoEngine::pinLocalsToMemory(llvm::Function* f) {
    if (!f) return;
    // The edge from the setjmp to the landing pad is drawn as an ordinary
    // branch out of the entry block, because that is the only way to draw it.
    // Read as one, it says the block is entered with nothing assigned yet, and
    // a variable the optimiser has moved into a register is then given its
    // entry value on that edge: nlg2's `k` came back as 0 rather than 30.
    //
    // The jump really arrives long after entry, and what a variable holds then
    // is what the abandoned frames left in memory.  So the variables of a
    // block that a goto can land in stay in memory, marked here rather than at
    // each access because there is one place to do it and hundreds of those.
    // Only the owning block pays; every other function optimises as before.
    for (auto& inst : f->getEntryBlock()) {
        auto* slot = llvm::dyn_cast<llvm::AllocaInst>(&inst);
        if (!slot) continue;
        for (auto* use : slot->users()) {
            if (auto* load = llvm::dyn_cast<llvm::LoadInst>(use))
                load->setVolatile(true);
            else if (auto* store = llvm::dyn_cast<llvm::StoreInst>(use))
                store->setVolatile(true);
        }
    }
}

void LabelGotoEngine::emitGoto(const GotoStmt& s) {
    // A label of the block being emitted is in this function, so the jump is a
    // branch.  Anything else belongs to a block further out.
    if (LabelOwners.empty() || declaresLabel(*LabelOwners.back().Block, s.Label)) {
        B.CreateBr(getOrCreateLabel("lbl_" + s.Label));
        return;
    }
    const LabelOwner* owner = nullptr;
    for (auto it = LabelOwners.rbegin(); it != LabelOwners.rend(); ++it)
        if (declaresLabel(*it->Block, s.Label)) { owner = &*it; break; }
    if (!owner || owner->BufName.empty()) {
        // Sema accepted the goto, so the label was found and is placed where a
        // jump may land; openLabelScope saw the same program and should have
        // planted the pad.
        codegenICE("no landing pad for non-local goto to label " + s.Label);
        return;
    }
    llvm::Value* buf = LookupBuf(owner->BufName);
    if (!buf) { codegenICE("jump buffer for label " + s.Label + " is out of reach"); return; }

    // _longjmp, not longjmp, to match the _setjmp the landing pad was entered
    // with.  The two forms differ in whether they carry the signal mask, and
    // they have to agree: on macOS longjmp restores a mask from the buffer
    // whatever put it there, so paired with _setjmp, which does not save one,
    // it sets the mask from whatever the buffer happened to hold.  The
    // program-level buffer is zeroed, which unblocks every signal the program
    // had blocked; a procedure's buffer is a stack slot, so it is worse.
    auto* jump = RtFns.getExternFnN("_longjmp", llvm::Type::getVoidTy(Ctx),
                              {ptrTy(), i32Ty()});
    if (auto* f = llvm::dyn_cast<llvm::Function>(jump))
        f->addFnAttr(llvm::Attribute::NoReturn);
    B.CreateCall(jump, {buf,
                        llvm::ConstantInt::get(i32Ty(),
                                               gotoDispatchValue(s.Label))});
    B.CreateUnreachable();
}
