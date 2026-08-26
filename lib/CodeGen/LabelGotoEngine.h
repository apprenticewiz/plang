// LabelGotoEngine.h — ISO §6.8.1 non-local goto, via setjmp/longjmp.
//
// Leaving a procedure abandons its frame and every frame under it, which a
// branch cannot express: the target is not in this function.  So the owning
// block records where it was with setjmp, and a goto from an enclosed
// procedure returns there with longjmp, naming the label it wants in the
// value the setjmp is seen to return.  A switch on that value does the rest.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "RuntimeFunctionCache.h"

namespace plang {
struct BlockNode;
struct GotoStmt;
}

class LabelGotoEngine {
public:
    /// DefineBuf(name, ptr, ty) registers a jump buffer as a nameable local
    /// (Codegen::Impl::defVar today); LookupBuf(name) retrieves its address
    /// (findVar(name)->ptr today). Narrowed to exactly this shape --
    /// llvm::Value*, not a VarEntry* -- since that pointer is the only field
    /// this engine ever needs off what the symbol table returns.
    LabelGotoEngine(llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
                     llvm::Function*& CurFn, RuntimeFunctionCache& RtFns,
                     std::function<void(const std::string&, llvm::Value*, llvm::Type*)> DefineBuf,
                     std::function<llvm::Value*(const std::string&)> LookupBuf)
        : Ctx(Ctx), Mod(Mod), B(B), CurFn(CurFn), RtFns(RtFns),
          DefineBuf(std::move(DefineBuf)), LookupBuf(std::move(LookupBuf)) {}

    /// Does \p block's label section declare \p label?
    static bool declaresLabel(const plang::BlockNode& block, const std::string& label);
    /// The labels \p block declares that a goto inside a procedure declared in
    /// it names.  These are the ones that need somewhere to land.
    static std::set<std::string> nonLocalTargets(const plang::BlockNode& block);
    /// The value a longjmp passes for \p label.  Offset by one because zero is
    /// what setjmp returns when it is first called, and longjmp turns a
    /// requested zero into one anyway.
    static int64_t gotoDispatchValue(const std::string& label);

    llvm::BasicBlock* getOrCreateLabel(const std::string& name);

    /// Record \p block as the owner of its labels, and, if a goto from inside
    /// one of its procedures names any of them, plant the landing pad.  Emits
    /// at the current insertion point, which must be past the block's
    /// initialization: a goto landing here resumes the block, it does not
    /// restart it.
    void openLabelScope(const plang::BlockNode& block, bool programBlock);
    /// Plant the setjmp and the switch that dispatches on what it returns.
    /// Done for a procedure by openLabelScope; the program's block registers
    /// itself before its procedures are emitted and lands here later, once
    /// main exists to hold the setjmp and its variables are initialized.
    void emitLabelLanding();
    /// Point the landing pad at the label blocks the body has by now created.
    void closeLabelScope();
    void emitGoto(const plang::GotoStmt& s);
    /// Keep \p f's local variables in memory, so that a goto landing in it
    /// finds what they hold rather than what the optimiser decided they must
    /// hold on the edge from the setjmp.  See closeLabelScope.
    static void pinLocalsToMemory(llvm::Function* f);

    /// Replaces the save/clear/restore of the per-function label-name map
    /// that used to be hand-written at emitFunctionDef's entry and both its
    /// exit points (a normal return and the declareOnly early return) --
    /// RAII means the destructor restores on either path, where two
    /// hand-written restores could (and once did, for other fields sharing
    /// this shape) drift out of sync.
    class FunctionLabelScope {
    public:
        explicit FunctionLabelScope(LabelGotoEngine& E)
            : E(E), Saved(std::move(E.LabelBlocks)) { E.LabelBlocks.clear(); }
        ~FunctionLabelScope() { E.LabelBlocks = std::move(Saved); }
        FunctionLabelScope(const FunctionLabelScope&) = delete;
        FunctionLabelScope& operator=(const FunctionLabelScope&) = delete;
    private:
        LabelGotoEngine& E;
        std::map<std::string, llvm::BasicBlock*> Saved;
    };

private:
    /// Recursive half of nonLocalTargets: scans the procedures declared in
    /// \p inner for gotos naming a label \p block declares, collecting into
    /// \p found.
    static void scanNonLocalTargets(const plang::BlockNode& inner,
                                     const plang::BlockNode& block,
                                     std::set<std::string>& found);

    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    llvm::Function*& CurFn;
    RuntimeFunctionCache& RtFns;
    std::function<void(const std::string&, llvm::Value*, llvm::Type*)> DefineBuf;
    std::function<llvm::Value*(const std::string&)> LookupBuf;

    /// Label name to the block it names, for the function being emitted.
    /// ISO §6.2.1 scopes a label to its block, so this is saved and cleared
    /// around each function via FunctionLabelScope.
    std::map<std::string, llvm::BasicBlock*> LabelBlocks;

    /// A block being emitted that declares labels, with the machinery a goto
    /// from an enclosed procedure needs to reach it.  Innermost last, so a
    /// goto finds the nearest activation that declares its label.
    struct LabelOwner {
        const plang::BlockNode* Block{nullptr};
        std::string             BufName; ///< scope name of its jump buffer
        llvm::SwitchInst*       Dispatch{nullptr};
        /// The value-conformant-array-copy shadow stack's depth (runtime/
        /// plang_sys.cpp), taken right before this owner's own _setjmp --
        /// after its parameter prologue has pushed its own by-value
        /// conformant array copies, so those are never above this mark, and
        /// before its body can call anything that might push more.  Set by
        /// emitLabelLanding, read by closeLabelScope to free what a longjmp
        /// landing here abandoned.
        llvm::Value*             ConfArrMark{nullptr};
    };
    std::vector<LabelOwner> LabelOwners;

    /// The jump buffer is sized once, here, for every target: the generated
    /// code cannot see the platform's jmp_buf, and the runtime asserts that
    /// this is enough room for it.
    static constexpr unsigned GotoBufWords = 64;

    llvm::IntegerType* i32Ty() const { return llvm::Type::getInt32Ty(Ctx); }
    llvm::IntegerType* i64Ty() const { return llvm::Type::getInt64Ty(Ctx); }
    llvm::PointerType* ptrTy() const { return llvm::PointerType::get(Ctx, 0); }
};
