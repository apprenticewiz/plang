// CGDebugInfo.h — -g debug-info construction.
//
// Built in init() unconditionally (matching every other unit's "always
// construct, check internal null state" pattern); DBuilder/DebugCU/
// DebugFile are only actually constructed when LangOptions::Debug is set,
// exactly as today. srcMgr_/mainFileID_ move here (not just referenced,
// unlike most of this decomposition's fields) -- confirmed by grep that
// their only external readers are inside this unit's own territory.
//
// This is the highest -g-risk extraction of the whole decomposition so
// far: it finally moves currentDebugScope itself, not just something that
// references it, and splits defVar's -g half (CGSymbolTable::declareLocal
// today) out into its own declareLocal here. Any change here needs real
// gdb/lldb verification, not just the IR-text test suite -- see project
// memory (feedback_verify_debuginfo_with_real_debugger).
#pragma once

#include <map>
#include <memory>
#include <string>

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "plang/Basic/LangOptions.h"
#include "plang/Basic/SourceLocation.h"
#include "plang/Basic/SourceManager.h"

namespace plang {
struct TypeNode;
struct Type;
struct ProcedureTypeNode;
}

class CGTypes;

class CGDebugInfo {
public:
    CGDebugInfo(llvm::Module& Mod, llvm::LLVMContext& Ctx, llvm::IRBuilder<>& B,
                const plang::LangOptions& Opts, const plang::SourceManager* SrcMgr,
                plang::FileID MainFileID, const std::string& ProgName);

    /// Bound after construction, once CGTypes itself exists (CGDebugInfo is
    /// built before CGTypes in Codegen::Impl::init(), so this can't be a
    /// constructor argument -- same "leaf unit built later, wired in after"
    /// shape as the llvmTypeOfNode closures SchemaLayoutEngine is handed).
    /// Record/Array/Set/Complex/String/VarString DIType construction below
    /// reads CGTypes's own field-offset/layout machinery through this rather
    /// than recomputing it, so there is exactly one place that knows a
    /// field's offset, not two that can drift apart.
    void setCGTypes(CGTypes& T) { Types = &T; }

    /// False when LangOptions::Debug was unset -- every other method is a
    /// safe no-op (returns null / does nothing) when this is false, exactly
    /// as the current `if (DBuilder)` checks are, but callers that used to
    /// gate on `DBuilder` directly now gate on this instead.
    bool isActive() const { return DBuilder != nullptr; }

    llvm::DIFile* getFile() const { return DebugFile; }
    llvm::DICompileUnit* getCompileUnit() const { return DebugCU; }
    /// The DISubprogram (ordinarily) or DILexicalBlock (see
    /// enterShadowScope) whatever is currently being emitted belongs to;
    /// null at module scope or when Debug is unset.
    llvm::DILocalScope* currentScope() const { return CurScope; }

    /// currentScope(), but walked up past any DILexicalBlock
    /// enterShadowScope opened until it reaches the nearest enclosing
    /// DISubprogram (or null, at module scope / when Debug is unset).
    /// A nested PROCEDURE DECLARATION's own DISubprogram must always
    /// parent under another DISubprogram, never under a DILexicalBlock:
    /// DISubprogram -> DILexicalBlock -> DISubprogram is a scope shape
    /// llc's DWARF AsmPrinter cannot handle once any optimization pass
    /// runs (SIGSEGV in getOrCreateAbstractSubprogramContextDIE), even
    /// though the identical metadata is accepted fine at -O0.  A
    /// shadowing lexical block exists purely to disambiguate a captured
    /// variable's OWN DILocalVariable from the same-named local that
    /// shadows it (see enterShadowScope) -- it says nothing about where
    /// a child procedure declared inside that activation belongs, since
    /// a procedure declaration itself is never subject to that same-name
    /// collision. Use this instead of currentScope() at the one call
    /// site that scopes a nested procedure's own DISubprogram; leave
    /// every DILocalVariable call site (declareLocal) reading
    /// currentScope() exactly as before.
    llvm::DILocalScope* nearestSubprogramScope() const {
        llvm::DILocalScope* S = CurScope;
        while (S && !llvm::isa<llvm::DISubprogram>(S))
            S = llvm::isa<llvm::DILexicalBlockBase>(S)
                ? llvm::cast<llvm::DILexicalBlockBase>(S)->getScope()
                : nullptr;
        return S;
    }

    /// The DIType for \p T -- covers every TypeKind that reaches codegen,
    /// scalar and composite alike (see the definition for the strategy
    /// each composite kind uses).  Null only when Debug is unset or \p T
    /// is a kind with no runtime representation to describe (Error,
    /// ConformantArray -- passed by reference with bounds threaded
    /// separately, so there is no one fixed shape to name).
    llvm::DIType* debugTypeOfSemaType(const plang::Type& T);

    /// Builds Fn's DISubprogram, attaches it, and sets the IRBuilder's
    /// current debug location to Fn's own first line -- has a real IR side
    /// effect (Fn->setSubprogram, SetCurrentDebugLocation), not just a
    /// detached node, which is why this isn't named a plain "get/create".
    /// \p Scope is getFile() for a top-level function and the enclosing
    /// procedure's own DISubprogram (via currentScope()) for a nested one.
    llvm::DISubprogram* emitFunctionStart(llvm::Function* Fn, llvm::DIScope* Scope,
                                           const std::string& Name,
                                           plang::SourceLocation Loc);

    /// R3: makes \p NewScope the current scope for as long as the guard
    /// lives, restoring the previous one on destruction -- replaces the
    /// four hand-written save/restore pairs around emitFunctionDef/
    /// emitMain/emitModuleInitFn/emitModuleLifecycleFn.  \p NewScope is
    /// always a DISubprogram in practice (every caller passes
    /// emitFunctionStart's return value straight through), but the
    /// parameter and Saved are typed DILocalScope so restoring one
    /// activation's saved scope can put back a DILexicalBlock that
    /// enterShadowScope opened in an OUTER activation before this one's
    /// own ScopeGuard was constructed.
    class ScopeGuard {
    public:
        ScopeGuard(CGDebugInfo& DI, llvm::DILocalScope* NewScope)
            : DI(DI), Saved(DI.CurScope) { DI.CurScope = NewScope; }
        ~ScopeGuard() { DI.CurScope = Saved; }
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
    private:
        CGDebugInfo& DI;
        llvm::DILocalScope* Saved;
    };

    /// -g: the single choke point every named Pascal variable, parameter,
    /// local, captured outer variable and with-bound field passes through
    /// -- the one place a DILocalVariable/DIGlobalVariableExpression needs
    /// building.  See CGSymbolTable::defVar's own comment (this is exactly
    /// its former -g half, moved verbatim) for debugIndirectPtr/why a bare
    /// SSA value needs it and an alloca doesn't.  Uses the IRBuilder this
    /// object was constructed with -- there is only ever one, threaded
    /// throughout codegen by reference, the same one defVar's caller used
    /// before this split.
    void declareLocal(const std::string& name, const plang::TypeNode* typeNode,
                       llvm::Value* ptr, llvm::Value* debugIndirectPtr);

    /// -g, ISO §6.6.3.1: a procedural/functional parameter's own storage is
    /// a two-pointer closure pair (ClosureAndCallABI::procPairTy -- the
    /// entry point and the static-link frame its body reads outer
    /// variables through), which has no TypeNode of ITS OWN shape for the
    /// ordinary declareLocal path to walk: paramMeta.procType's
    /// ResolvedType is the SIGNATURE (what buildSubroutineDIType below
    /// builds), not this pair's own two-member storage.  Builds that pair
    /// type directly -- {code: pointer to the signature's own
    /// DISubroutineType, frame: an untyped pointer} -- and declares
    /// against it, rather than mismatching the DISubroutineType itself
    /// against storage it doesn't describe.  A no-op under every condition
    /// declareLocal already bails under (Debug unset, no current scope /
    /// insertion point), plus an unresolved \p PT.
    void declareProcParam(const std::string& name, const plang::ProcedureTypeNode* PT,
                           llvm::Value* ptr);

    /// -g, issue #19: opens a DILexicalBlock nested inside the current
    /// scope and makes it the current scope from now on, for a caller
    /// that just found a name collision a flat scope cannot express --
    /// see CGSymbolTable::defVar, the only caller.  A nested procedure's
    /// closure-capture loop registers every outer variable it can see
    /// under its OWN DISubprogram before that procedure's own
    /// parameters/locals are bound, so a parameter or local spelled the
    /// same as a captured outer variable collides with it in the same
    /// flat scope: two DILocalVariables of one name under one
    /// DISubprogram, which gdb/lldb resolve to the first (the captured,
    /// outer, WRONG one) regardless of which the current PC is actually
    /// inside. Reopening the current scope as a lexical block gives the
    /// shadowing declaration somewhere strictly innermost to live, which
    /// a debugger prefers over the flat outer one.
    ///
    /// Idempotent per activation by construction, not by tracked state:
    /// once CurScope is a lexical block this is a no-op, so a second,
    /// differently-named collision in the same activation reuses it
    /// rather than nesting a second block inside the first -- both
    /// shadowing declarations are equally "the rest of this activation
    /// from here on", so one shared block is the correct scope for both,
    /// not just the simpler one to build. Never explicitly closed: since
    /// the ONLY way a collision happens is a capture rebound by this same
    /// activation's own parameter/local, nothing legitimately re-widens
    /// back to the flat scope afterward, so the block can simply cover
    /// the rest of the activation -- it is discarded for free when this
    /// activation's own ScopeGuard restores CurScope to whatever it was
    /// before this activation began.
    llvm::DILocalScope* enterShadowScope(plang::SourceLocation Loc);

    /// -g: one hook for every statement kind, called from emitStmt -- sets
    /// the IRBuilder's current debug location to \p Loc, scoped to
    /// currentScope().  A location Sema could not resolve (synthesized
    /// code) leaves the previous location in force rather than attaching
    /// line 0.  A no-op when Debug is unset or there is no current scope
    /// (module-level code, outside any function).
    void setLocation(plang::SourceLocation Loc);

    /// -g: construct whatever deferred debug-info nodes DIBuilder collected
    /// (e.g. forward-declared types) before the module is inspected by
    /// anything else.  A no-op when Debug is unset.
    void finalize() { if (DBuilder) DBuilder->finalize(); }

private:
    /// Record, Array, Set, Complex, String and VarString DIType construction
    /// (see debugTypeOfSemaType.cpp -- each has its own builder below) reads
    /// field offsets/sizes/element types out of here rather than
    /// re-deriving them, so there is one computation of a field's offset,
    /// not a second one that can silently drift from CGTypes's own.
    llvm::DIType* buildRecordDIType(const plang::Type& T);
    llvm::DIType* buildArrayDIType(const plang::Type& T);
    llvm::DIType* buildSetDIType(const plang::Type& T);
    llvm::DIType* buildComplexDIType(const plang::Type& T);
    /// Shared by String (wrapped in a pointer -- see the TypeKind::String
    /// case) and VarString (used directly): both are the runtime's
    /// { i64 length; [cap x i8] data } shape (StringRuntime/CGTypes::
    /// strStructType); a bare `string` has no declared capacity, so this
    /// falls back to PlangMaxStringCapacity for it, which is honest about
    /// being a representative shape, not the exact runtime allocation size
    /// of any one instance -- see the concerns note where this is called.
    llvm::DIType* buildStringDIType(int64_t cap);
    llvm::DISubroutineType* buildSubroutineDIType(const plang::Type& T);
    /// A record field's own DIType via debugTypeOfSemaType(*SemaTy) where
    /// that resolves to something (the ordinary case); otherwise -- no
    /// matching Sema::Type::Field found, or that field's own type has no
    /// DWARF encoding of its own (a nested kind this pass still leaves
    /// null) -- a same-sized DW_ATE_unsigned basic type named for the
    /// field, built off its LLVM storage type directly, so the member is
    /// still visible (raw bytes) rather than silently dropped from the
    /// struct.
    llvm::DIType* fieldOrFallbackDIType(const plang::Type* SemaTy, llvm::Type* LLTy,
                                         const std::string& DisplayName);

    llvm::Module& Mod;
    llvm::LLVMContext& Ctx;
    llvm::IRBuilder<>& B;
    const plang::LangOptions& Opts;
    const plang::SourceManager* SrcMgr;
    /// Null until setCGTypes runs (always before any real codegen; see its
    /// own comment). Composite DIType builders bail to null, exactly like
    /// the pre-existing default: break, if asked to run before then --
    /// defensive only, every real caller goes through Codegen::Impl::init().
    CGTypes* Types{nullptr};

    std::unique_ptr<llvm::DIBuilder> DBuilder;
    llvm::DICompileUnit* DebugCU{nullptr};
    llvm::DIFile* DebugFile{nullptr};
    /// Keyed on Type* identity, not on Type::Name or Kind: the lesson from
    /// this cycle's own schema-body-peel bug class was specifically that a
    /// spelling-keyed cache is what goes wrong when two distinct Types can
    /// share a name.  A Type lives as long as the shared_ptr chain rooted
    /// in the AST/symbol table, which outlives this cache either way.
    std::map<const plang::Type*, llvm::DIType*> debugTypes_;
    /// DISubprogram, ordinarily; a DILexicalBlock nested inside one for
    /// the rest of an activation that hit the shadowing collision
    /// enterShadowScope exists for.  DILocalScope is the common base
    /// DILocation::get and createAutoVariable/createLexicalBlock's Scope
    /// parameter both already accept.
    llvm::DILocalScope* CurScope{nullptr};
};
