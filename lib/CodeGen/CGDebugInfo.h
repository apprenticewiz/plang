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

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "plang/Basic/LangOptions.h"
#include "plang/Basic/SourceLocation.h"
#include "plang/Basic/SourceManager.h"

namespace plang {
struct TypeNode;
struct RecordTypeNode;
struct Type;
struct ProcedureTypeNode;
struct ExtentForm;
}

class CGTypes;
class SchemaTypeRegistry;

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
    /// Already exists by the time dbgInfo_ is constructed in
    /// Codegen::Impl::init() (schemaTypes_ is built just before it), so
    /// this is called right after construction rather than deferred like
    /// setCGTypes is. recordSchemaLayoutForScript uses this to reach a
    /// schema type's own RecordTypeNode -- the FieldDecls, and each array
    /// field's TypeNode::ExtentLow/ExtentHigh -- which the resolved Type
    /// alone does not carry.
    void setSchemaTypes(SchemaTypeRegistry& S) { SchemaTypes = &S; }

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

    /// Builds a minimal DISubprogram for a compiler-synthesized shim (e.g.
    /// the uniform-signature procedural-parameter thunk in
    /// ClosureAndCallABI::procParamThunk) that has no Pascal-level source
    /// identity of its own to attribute lines to.  Marked
    /// DIFlagArtificial -- the standard DWARF way to say
    /// "this frame exists but isn't user code" -- rather than left with no
    /// DISubprogram at all: an unattributed thunk gets no line-table
    /// entries whatsoever, so a debugger's "step into" a call made through
    /// a procedural parameter silently jumps clean over the thunk AND the
    /// real target (confirmed with gdb: `step` on the call site runs the
    /// whole call to completion instead of entering anything), rather than
    /// stopping in either the thunk or, transparently through it, the real
    /// target.  Attaches Fn->setSubprogram like emitFunctionStart, but
    /// deliberately does NOT touch the IRBuilder's current debug location
    /// -- procParamThunk sets each instruction's location itself (all of
    /// them line 0, this SP's own scope), since a thunk has no notion of
    /// "current statement" to advance through.
    llvm::DISubprogram* emitThunkStart(llvm::Function* Fn, llvm::DIScope* Scope,
                                        const std::string& Name);

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
    /// \p suppress is issue #142's one deliberate exception to "every named
    /// Pascal ... passes through this": a schema var/value parameter is
    /// bound through the ordinary defVar (its VarEntry::typeNode is real
    /// codegen state -- CGIndexAccess.cpp reads it for the schema's own
    /// array bound), but its OWN debug declaration needs
    /// declareSchemaParamRef's different shape instead of this method's
    /// header-at-offset-0 assumption -- see declareSchemaParamRef's own
    /// comment for why. Defaulted false: every other caller is unaffected.
    void declareLocal(const std::string& name, const plang::TypeNode* typeNode,
                       llvm::Value* ptr, llvm::Value* debugIndirectPtr,
                       bool suppress = false);

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

    /// -g, issue #142: a schema var/value PARAMETER's ABI pointer (see
    /// CodeGenProcs.cpp's schema-param binding, and SchemaAccess::
    /// schemaActual/schemaRefOf) addresses the object's BODY directly, never
    /// its header -- unlike a directly-allocated ExtentVaries object (built
    /// by SchemaAccess::emitNewSchema, header included), a schema parameter
    /// carries its discriminants as separate SSA arguments with no header in
    /// memory at all: not just at a different offset from \p bodyPtr, but
    /// for a fixed-extent actual (a SchemaInstance local passed `var`)
    /// genuinely absent from memory altogether, since that actual's own
    /// storage (CGTypes::llvmTypeOfSemaTypeImpl's SchemaInstance case) is
    /// the body value with nothing in front of it.  buildSchemaDIType's
    /// header-at-offset-0 struct is therefore never right for a parameter,
    /// whichever shape the actual is -- declareLocal's ordinary TypeNode
    /// path can't describe this any more than declareProcParam's pair could.
    ///
    /// Builds a small debug-only shadow block instead: \p discs (already
    /// correct SSA values, straight off the call's own arguments) written
    /// into header-shaped slots, followed by \p bodyPtr itself stored as a
    /// plain pointer member -- so the discriminants are always read from
    /// real memory this activation owns, and the body is reached through an
    /// explicit indirection rather than assumed adjacent.
    /// plang_schema_printers.py recognizes the resulting struct's distinct
    /// ".ref" tag and reads it the same way.  A no-op under every condition
    /// declareLocal already bails under, plus an unresolved schema body.
    void declareSchemaParamRef(const std::string& name, const plang::TypeNode* typeNode,
                                const std::vector<llvm::Value*>& discs,
                                llvm::Value* bodyPtr);

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
    /// anything else.  A no-op when Debug is unset.  Also writes the schema
    /// debug-script sidecar (see writeSchemaDebugScript) -- this is the one
    /// place every compile with Debug set reaches exactly once, late enough
    /// that every schema type debugTypeOfSemaType ever built has already
    /// called recordSchemaLayoutForScript.
    void finalize() {
        if (!DBuilder) return;
        DBuilder->finalize();
        writeSchemaDebugScript();
    }

private:
    /// Issue #130's real fix: LLVM's DWARF emitter has no implementation of
    /// a computed MEMBER ADDRESS (confirmed directly from
    /// llvm/lib/CodeGen/AsmPrinter/DwarfUnit.cpp's constructMemberDIE --
    /// an expression-typed member offset always becomes DW_AT_data_bit_offset,
    /// a bitfield-only attribute, with no parallel path to
    /// DW_AT_data_member_location; confirmed empirically too, gdb 17.2
    /// crashes trying to print a member built that way). A field declared
    /// after a varying-extent one therefore cannot get a correct static
    /// DWARF offset at all -- so this sidesteps DWARF for exactly those
    /// fields instead: alongside the (unchanged, still probe-approximate)
    /// DIType, record each ExtentVaries schema's REAL layout as data (field
    /// order, sizes, alignments, and each array field's bound as a
    /// JSON-serialized ExtentForm) into a sidecar file a gdb Python
    /// pretty-printer (share/plang/gdb/plang_schema_printers.py) loads
    /// separately, walking it against the object's LIVE memory at print
    /// time -- the exact computation tryFlattenSchemaBody attempted to push
    /// into DWARF itself, run in Python instead, where it can actually
    /// compute a real address rather than being limited to what
    /// DW_AT_data_bit_offset's bitfield semantics allow.
    void recordSchemaLayoutForScript(const plang::Type& T, const plang::RecordTypeNode& rt,
                                      uint64_t hdrBytes);
    /// Issue #140's disambiguation key: a 64-bit FNV-1a hash over the
    /// discriminant count and every body field's (name, kind, byte size) in
    /// DWARF declaration order -- the same quantities buildRecordDIType
    /// gives each member (DL.getTypeSizeInBits(fp.Ty)/8), so this is
    /// exactly the structural identity gdb's own DWARF-derived member list
    /// can reproduce independently at print time. Returns std::nullopt for
    /// exactly the shapes recordSchemaLayoutForScript already declines to
    /// record (a variant body, a field whose bound isn't a closed const
    /// form, or a field with no representable LLVM type) -- those never
    /// reach a sidecar entry at all, so they need no fingerprint.
    std::optional<uint64_t> computeSchemaFingerprint(const plang::RecordTypeNode& rt,
                                                       size_t numDiscs);
    /// Serializes every schema recordSchemaLayoutForScript has collected to
    /// <source file>.plang-schemas.json, once, here in finalize() --
    /// silently does nothing if none were recorded (Debug set but no
    /// ExtentVaries schema ever reached codegen) or SrcMgr is null (the
    /// -pc1 internal frontend always sets it when Debug is on; see
    /// Codegen::setSourceManager's own call site).
    void writeSchemaDebugScript();
    /// <source file>.plang-schemas.json -- shared by writeSchemaDebugScript
    /// and the constructor's own early truncation of any pre-existing
    /// sidecar (issue #141: shrinks the window in which a stale sidecar
    /// from a previous compile of this same source can be read against a
    /// binary that this compile never finished, or never asked for -g at
    /// all -- writeSchemaDebugScript still writes the real one at the very
    /// end, same as before). Empty when SrcMgr is null.
    std::filesystem::path schemaSidecarPath() const;
    /// True if F is expressible in the sidecar's tiny JSON-array encoding
    /// (append the encoded form to Out); Const/Disc/Add/Sub/Mul/Div/Mod/Neg/
    /// Pow all are, unlike DWARF -- Python's ** operator gives this format a
    /// real advantage over the DW_OP attempt for Pow specifically.
    static void jsonEncodeExtentForm(const plang::ExtentForm& F, std::string& Out);
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
    /// Schema/SchemaInstance whose extent a discriminant fixes only at run
    /// time (Type::ExtentVaries): SchemaLayoutEngine::schemaHeaderBytes /
    /// SchemaAccess::emitNewSchema store a leading discriminant header in
    /// front of the body at run time, so this wraps the body's own DIType
    /// in a synthetic outer struct that accounts for it -- see the
    /// definition for why recursing into the body directly (as the
    /// fixed-extent case still does) put every field, not just the varying
    /// one, at the wrong DWARF offset.
    llvm::DIType* buildSchemaDIType(const plang::Type& T);
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

    /// The module's real pointer width, in bits -- what every createPointerType
    /// call below must report as a DWARF pointer member's DW_AT_byte_size,
    /// rather than the 64 every one of them hardcoded until issue #243's
    /// follow-up (a data layout that is genuinely non-host's own only became
    /// reachable once --target started working at all).  Mod's data layout is
    /// already target-correct -- Codegen::Impl::init sets it from the same
    /// --target this debug info is being built for -- so this asks it rather
    /// than re-deriving the width a second, independent way.
    [[nodiscard]] unsigned ptrBits() const {
        return Mod.getDataLayout().getPointerSizeInBits();
    }

    llvm::Module& Mod;
    llvm::LLVMContext& Ctx;
    llvm::IRBuilder<>& B;
    const plang::LangOptions& Opts;
    const plang::SourceManager* SrcMgr;
    /// Kept only for writeSchemaDebugScript's own SrcMgr->getBufferName
    /// call -- every other use of the main file already goes through
    /// SrcMgr->getPresumedLoc(Loc) with a real per-node SourceLocation, not
    /// this file identity, so this one extra field is scoped to that one
    /// caller rather than threaded anywhere else.
    plang::FileID MainFileID;
    /// Null until setCGTypes runs (always before any real codegen; see its
    /// own comment). Composite DIType builders bail to null, exactly like
    /// the pre-existing default: break, if asked to run before then --
    /// defensive only, every real caller goes through Codegen::Impl::init().
    CGTypes* Types{nullptr};
    /// Null until setSchemaTypes runs. recordSchemaLayoutForScript bails
    /// (silently skips the sidecar entry) if this is null, same defensive-
    /// only shape as Types above.
    SchemaTypeRegistry* SchemaTypes{nullptr};

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
    /// Issues #140/#141: two schemas can share a declared NAME while having
    /// completely different bodies (two different procedures, or two
    /// different modules, each with their own "Foo = schema(...)"), so the
    /// bare name alone is not a valid sidecar key -- keying on it, as this
    /// used to, let a second same-named-but-different schema silently
    /// overwrite (get silently DROPPED behind) the first one's entry, and
    /// the gdb pretty-printer would then apply the wrong layout to the
    /// second schema's instances with no warning at all.
    ///
    /// Fixed by keying every entry on BOTH the name and a structural
    /// fingerprint (computeSchemaFingerprint) hashed over the discriminant
    /// count and every body field's name/kind/size, in DWARF declaration
    /// order -- schemas that happen to share a name AND have a compatible
    /// layout collapse to one entry exactly as before (the common,
    /// intended case: the same schema recorded once from several probe
    /// instantiation sites), while a genuine name collision gets a SECOND,
    /// distinct (fingerprint, body) entry instead of silently overwriting
    /// the first (recordSchemaLayoutForScript also warns to stderr once per
    /// colliding name -- see warnedSchemaNames_ -- since this is exactly the
    /// ambiguity a human should know their program has). The gdb
    /// pretty-printer (share/plang/gdb/plang_schema_printers.py) recomputes
    /// the identical fingerprint from the live DWARF type's own member list
    /// at print time, so it can pick the matching variant rather than just
    /// the first one with a matching name.
    std::map<std::string, std::vector<std::pair<uint64_t, std::string>>> schemaScriptEntries_;
    /// True once recordSchemaLayoutForScript has already warned about a
    /// given name colliding -- keeps the stderr warning to one line per
    /// ambiguous name rather than once per probe instantiation site that
    /// happens to reach it.
    std::set<std::string> warnedSchemaNames_;
    /// A random 64-bit token minted once per CGDebugInfo construction (once
    /// per compile), embedded both in DWARF (appended to the compile
    /// unit's DW_AT_producer string, right after PLANG_VERSION_STRING) and
    /// in the sidecar JSON's own top-level "buildId" field -- issue #141's
    /// real fix for sidecar staleness. A rebuild (even of the very same
    /// source, even to a binary that never asked for -g at all) always
    /// mints a fresh token, so a gdb session against an OLDER already-built
    /// -g binary that loads a NEWER (or just different) sidecar written by
    /// a later compile of the same source path -- the exact scenario issue
    /// #141 raised -- reads two different tokens and can detect the
    /// mismatch instead of confidently printing a different program's
    /// layout. Left default (0) when Debug is unset; never read in that
    /// case since no sidecar is ever written.
    uint64_t SchemaBuildId_{0};
};
