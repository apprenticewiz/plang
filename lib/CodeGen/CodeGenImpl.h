// CodeGenImpl.h — Private implementation header for Codegen.
// NOT installed; used only by the CodeGen translation units.
#pragma once

#include "plang/Basic/LangOptions.h"
#include "plang/Basic/SourceManager.h"
#include "plang/CodeGen/CodeGen.h"
#include "plang/Sema/Sema.h"
#include "plang/Sema/Type.h"

#include <algorithm>
#include <cstddef>
#include <bit>
#include <cfloat>
#include <climits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include "plang/AST/Ast.h"
#include "llvm/Support/Casting.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Basic/Token.h"

#include "BuiltinIO.h"
#include "CGAssign.h"
#include "CGBinaryOps.h"
#include "CGControlFlow.h"
#include "CGDebugInfo.h"
#include "CGExprCore.h"
#include "CGFieldAccess.h"
#include "CGFuncCall.h"
#include "CGIndexAccess.h"
#include "CGLinkage.h"
#include "CGPackUnpack.h"
#include "CGProcCall.h"
#include "CGStmtCore.h"
#include "CGStructuredValue.h"
#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "CGWith.h"
#include "ClosureAndCallABI.h"
#include "ComplexOps.h"
#include "ConstFold.h"
#include "FileVarHelpers.h"
#include "LabelGotoEngine.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "SchemaLayoutEngine.h"
#include "SchemaTypeRegistry.h"
#include "SetOps.h"
#include "StringCallMarshalling.h"
#include "StringRuntime.h"
#include "VarEntry.h"

#include "CodegenICE.h"

using namespace plang;

// ---------------------------------------------------------------------------
// Impl — contains all LLVM objects
// ---------------------------------------------------------------------------

struct Codegen::Impl {
    // ---- LLVM core objects ----
    llvm::LLVMContext            ctx;
    std::unique_ptr<llvm::Module> mod;
    llvm::IRBuilder<>            builder{ctx};

    // ---- leaf units (constructed fresh in init(), once mod/common types
    // exist; see init()'s own comment) ----
    std::unique_ptr<RuntimeFunctionCache> runtimeFns_;
    std::unique_ptr<StringRuntime>        strings_;
    std::unique_ptr<ComplexOps>           complexOps_;
    std::unique_ptr<RangeCheckGuards>     rangeGuards_;
    std::unique_ptr<SetOps>               setOps_;
    std::unique_ptr<LabelGotoEngine>      gotoEngine_;
    std::unique_ptr<CGLinkage>            linkage_;
    std::unique_ptr<SchemaTypeRegistry>   schemaTypes_;
    std::unique_ptr<SchemaLayoutEngine>   schemaLayout_;
    std::unique_ptr<CGSymbolTable>        symTab_;
    // -g debug info; unconditionally constructed in init() like every other
    // unit above, internally a no-op wherever langOpts.Debug is unset.
    std::unique_ptr<CGDebugInfo>          dbgInfo_;
    // Static type-lowering/layout; built later in init() than the units
    // above (needs the common type aliases below plus complexOps_/setOps_,
    // see init()'s own comment).
    std::unique_ptr<CGTypes>              cgTypes_;
    /// `Impl::RecordLayout`/`FieldPlace` are pure data with no behavior tied
    /// to `Impl` -- a plain alias, not a wrapper, keeps every external
    /// site naming these types (CodeGenExprs.cpp/CodeGenStmts.cpp)
    /// compiling unchanged now that `CGTypes` owns them.
    using RecordLayout = CGTypes::RecordLayout;
    using FieldPlace   = CGTypes::FieldPlace;
    // Schema value/access-path resolution; built after cgTypes_ (needs it
    // for llvmTypeOfSemaType) and rangeGuards_ (needs it for guards).
    std::unique_ptr<SchemaAccess>         schemaAccess_;
    /// `Impl::SchemaRef`/`SchemaPath` are pure data too -- same alias
    /// treatment as `RecordLayout`/`FieldPlace` above, for the same reason
    /// (external sites in CodeGenProcParams.cpp/CodeGenStmts.cpp/
    /// CodeGenExprs.cpp name them directly).
    using SchemaRef  = SchemaAccess::SchemaRef;
    using SchemaPath = SchemaAccess::SchemaPath;
    // Call-argument marshalling + the EP string-store/address operations
    // it and everyday string assignment both rest on; built after
    // schemaAccess_ (needs it for strAddrAndCap).
    std::unique_ptr<StringCallMarshalling> strCallMarshal_;
    // Procedural-parameter ABI + conformant-array marshalling; built after
    // schemaAccess_ (needs it for schemaPathOf/pushSchemaArgs).
    std::unique_ptr<ClosureAndCallABI>    closureAbi_;
    // File-variable address/type/size helpers (ISO §6.6.5.2); built after
    // cgTypes_/symTab_/runtimeFns_ all exist.
    std::unique_ptr<FileVarHelpers>       fileVarHelpers_;
    // Built-in write/writeln/read/readln/writestr/readstr (ISO §6.9, EP
    // §6.7.5.5); built after fileVarHelpers_, its last real dependency.
    std::unique_ptr<BuiltinIO>            builtinIO_;
    // Assignment-statement emission (ISO §6.8.2.2); built after builtinIO_,
    // once every sibling unit it touches (schemaAccess_/schemaLayout_/
    // strCallMarshal_/strings_/cgTypes_/rangeGuards_/setOps_/complexOps_/
    // symTab_) already exists.
    std::unique_ptr<CGAssign>             assign_;
    // Structured-statement emission: if/while/for/for-in/repeat/case; built
    // after assign_, once symTab_/cgTypes_/setOps_/runtimeFns_ all exist.
    std::unique_ptr<CGControlFlow>        controlFlow_;
    // with-statement emission (EP §6.8.3.10); built after controlFlow_,
    // once schemaAccess_/schemaLayout_/cgTypes_/symTab_ all exist.
    std::unique_ptr<CGWith>               with_;
    // ISO §6.7.5.4 transfer procedures (pack/unpack); built after with_,
    // once symTab_/schemaAccess_/schemaLayout_/cgTypes_/rangeGuards_ all
    // exist.  Built before procCall_, which holds a reference to it.
    std::unique_ptr<CGPackUnpack>         packUnpack_;
    // The required-procedure dispatch chain and user-declared procedure
    // call statements; built after packUnpack_, once every sibling unit
    // it touches already exists.
    std::unique_ptr<CGProcCall>           procCall_;
    // Record field access and pointer dereference (ISO §6.4.3.3/§6.5.5);
    // built after procCall_, once cgTypes_/schemaAccess_/symTab_/
    // fileVarHelpers_/rangeGuards_ all exist.
    std::unique_ptr<CGFieldAccess>        fieldAccess_;
    // Array indexing (ISO §6.5.3.2); built after fieldAccess_, once
    // schemaAccess_/strCallMarshal_/rangeGuards_/strings_/runtimeFns_/
    // symTab_/cgTypes_ all exist.
    std::unique_ptr<CGIndexAccess>        indexAccess_;
    // EP §6.8.7 typed value constructors (array/record/set); built after
    // indexAccess_, once cgTypes_/setOps_ both exist.
    std::unique_ptr<CGStructuredValue>    structuredValue_;
    // ISO §6.7.2 binary/unary operators; built after structuredValue_,
    // once complexOps_/schemaAccess_/strCallMarshal_/strings_/cgTypes_/
    // setOps_/rangeGuards_/runtimeFns_ all exist.
    std::unique_ptr<CGBinaryOps>          binaryOps_;
    // Call-expression emission (built-in dispatch + user-function calls);
    // built after binaryOps_, needs the same 12 sibling units CGProcCall
    // needed for the analogous procedure-call-statement split, plus
    // linkage_/symTab_/closureAbi_/rangeGuards_ (all already exist by then).
    std::unique_ptr<CGFuncCall>           funcCall_;
    // ISO §6.7.1 expression emission (emitExpr/emitLValue/spillToTemporary,
    // the central recursive-descent dispatcher); built after funcCall_,
    // needs 15 already-extracted sibling units, all already built by then.
    std::unique_ptr<CGExprCore>           exprCore_;
    // ISO §6.8 statement emission (emitStmt/emitCompound/
    // resumeAfterTerminator + the 5 live LabelGotoEngine forwarders);
    // built after exprCore_, needs assign_/controlFlow_/procCall_/with_/
    // gotoEngine_/dbgInfo_, all already built by then. The last piece of
    // the whole Codegen::Impl decomposition with real logic to extract.
    std::unique_ptr<CGStmtCore>           stmtCore_;

    // ---- common type aliases (set in init()) ----
    llvm::IntegerType* i1Ty{nullptr};
    llvm::IntegerType* i8Ty{nullptr};
    llvm::IntegerType* i32Ty{nullptr};
    llvm::IntegerType* i64Ty{nullptr};
    llvm::Type*        dblTy{nullptr};
    llvm::PointerType* ptrTy{nullptr};

    /// A signed i64 constant.  Enough places now pass a capacity or an extent
    /// that may be either a literal or a run-time value that spelling out
    /// ConstantInt::get at each of them buries which is which.
    llvm::Constant* i64c(int64_t v) const {
        return llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(v), true);
    }

    // ---- symbol table ----
    // VarEntry is free-standing (VarEntry.h), not nested here -- see that
    // header's own comment for why.
    std::vector<std::unordered_map<std::string, VarEntry>> scopes;

    // Constants from Pascal 'const' sections — folded to LLVM Values inline.
    std::unordered_map<std::string, llvm::Value*> consts;

    // ---- caches ----
    // structTypes/recordLayouts/schemaCtx/strStructTypes moved into CGTypes
    // (private there -- confirmed zero external touches once
    // fileStructType/timestampStructType/bindingStructType moved with
    // them). typeAliases stays here, referenced by CGTypes -- it's
    // copied/restored wholesale in emitFunctionDef and read directly at
    // ~14 other external sites.
    std::unordered_map<std::string, const TypeNode*> typeAliases; // user typedef name → AST node

    // ---- current function context ----
    llvm::Function*     curFunc{nullptr};
    llvm::AllocaInst*   curRetAlloca{nullptr}; // alloca for function result
    llvm::Type*         curRetType{nullptr};    // return type (null for procedures)
    std::string         curFuncName;            // for result-variable detection
    std::string         namePrefix{PlangProcPrefix};   // mangling prefix
    std::string         globalPrefix{PlangGlobalPrefix}; // ditto, for globals

    /// EP §6.11: a module is an outer naming scope, so what it declares is
    /// mangled with its name the way a nested procedure is mangled with its
    /// enclosing one.  Without this, two modules each exporting `f` both want
    /// the symbol `plang_f`, and the second definition is renamed to
    /// `plang_f.1` and never called.
    static std::string moduleScope(const std::string& moduleName) {
        return CGLinkage::moduleScope(moduleName);
    }

    /// Name of the unit being emitted: a lowercased module name, or empty for
    /// the program.  Selects which set of imports importOwner consults.
    std::string currentUnit_;

    /// What this unit imports; see Sema::importOwners.
    /// ISO §6.2.2.10: the names of the required constants, which a program may
    /// declare again and mean its own.  A user declaration of one of these
    /// replaces the entry in `consts`, but a variable of the name lives
    /// elsewhere, so the lookup has to know which entries may be set aside.
    std::set<std::string> requiredConsts;

    /// True if \p lowerName is a required constant that nothing has replaced.
    [[nodiscard]] bool isRequiredConst(const std::string& lowerName) const {
        return symTab_->isRequiredConst(lowerName);
    }

    /// Records what a declared constant stands for.  A name declared here is
    /// the program's own from now on, whatever the language calls it.
    void defineConst(const std::string& name, llvm::Value* value) {
        symTab_->defineConst(name, value);
    }

    const ImportOwnerTable* importOwners_{nullptr};
    /// EP §6.11: interfaces read from .pmi files; see Codegen::setLoadedInterfaces.
    std::vector<const ModuleNode*> loadedInterfaces_;
    /// -g: set by Codegen::setSourceManager, which always runs before
    /// emit()/init() -- these stay plain Impl members, rather than moving
    /// into dbgInfo_ outright, purely so setSourceManager has somewhere to
    /// write before dbgInfo_ exists; init() passes both by value into
    /// dbgInfo_'s constructor, same ordering shape as importOwners_ above.
    /// An ordinary Impl member and never a function-local or
    /// namespace-scope static -- 0.2.1 shipped a fix for exactly that
    /// mistake elsewhere (a static outliving the LLVMContext that owned it,
    /// segfaulting the second Codegen a process constructs, which the test
    /// suite does routinely, once per test case).  Every debug-info object
    /// added from here on follows the same rule.
    const SourceManager* srcMgr_{nullptr};
    FileID               mainFileID_;
    /// The heading of the module being emitted, whose declarations are its
    /// block's; null while emitting anything else.
    const BlockNode* moduleIfaceBlock_{nullptr};

    /// What is known about \p name as an import of the unit being emitted, or
    /// null if this unit does not import it.
    const ImportedName* importedName(const std::string& name) const {
        return linkage_->importedName(name);
    }

    /// Globals declared by module bodies in this compilation unit, keyed
    /// "module.name".  Each module body is emitted in its own scope so that one
    /// module's `count` is not mistaken for another's, and an importer finds
    /// the variable here instead — with its TypeNode, which a declaration
    /// synthesized from the LLVM type alone would lack.
    std::map<std::string, VarEntry> moduleGlobals_;

    /// The module that declares \p name as this unit sees it, or "" when the
    /// name is not imported.  A qualified name answers for itself: the parser
    /// folds `M.f` into one identifier, and M is the module.
    std::string importOwner(const std::string& name) const {
        return linkage_->importOwner(name);
    }

    /// The name \p name is mangled under in the module that declares it.  EP
    /// §6.11.2 renaming lets a unit call an imported procedure something else
    /// entirely; the object file still knows only the original.
    std::string importLinkName(const std::string& name) const {
        return linkage_->importLinkName(name);
    }

    /// True when \p name is an imported procedure or function, so a bare
    /// mention of it is a call even though nothing of that name has been
    /// emitted here — its module was compiled separately.
    bool isImportedCallable(const std::string& name) const {
        return linkage_->isImportedCallable(name);
    }

    // Static link for nested procedures (ISO §6.7.1).
    // funcOuterVarNames_: per-function record of outer variable name lists,
    //   keyed by LLVM mangled name.  Populated at definition time; consulted at
    //   call sites so every caller uses the same slot ordering.
    // nestedFunctions_: set of mangled names that require a static-link frame.
    //   Explicit flag replaces the fragile "declaredParamCount > astArgCount"
    //   arity heuristic which conflates frame params with arity mismatches.

    /// RAII guard for one function-activation's per-activation-transient
    /// state: the static-link value, the outer-variable capture lists, and
    /// a value-conformant-array parameter's heap copies.  Constructed as a
    /// local at the top of each of emitFunctionDef/emitMain/
    /// emitModuleInitFn/emitModuleLifecycleFn (CodeGenProcs.cpp), living
    /// exactly as long as that one activation -- unlike every
    /// std::unique_ptr<X> sibling elsewhere in this struct, this is not a
    /// persistent, once-constructed member. Confirmed by exhaustive grep
    /// that no sibling class holds a reference into any of the four fields
    /// below, so each activation owns its own independent copy outright --
    /// no save+clear+restore dance needed, object lifetime alone provides
    /// correct nesting for arbitrarily deep recursion (emitFunctionDef
    /// recurses into nested procedures via emitAllProcedures before its own
    /// body is emitted, so the live C++ call-stack depth already mirrors
    /// Pascal nesting depth exactly).
    ///
    /// curFunc/curRetAlloca/curRetType/curFuncName/namePrefix stay right
    /// here as Impl fields, unmoved -- rangeGuards_/gotoEngine_/
    /// controlFlow_/binaryOps_/stmtCore_/exprCore_/linkage_ hold direct
    /// references into them, reseatable only by reassigning the SAME
    /// field's value, never by relocating its storage. This guard's
    /// constructor/destructor instead snapshot and restore those five
    /// fields' VALUES, replacing what used to be four separately
    /// hand-maintained save/restore blocks with one.
    class CGFunction {
    public:
        explicit CGFunction(Impl& I);
        ~CGFunction();
        CGFunction(const CGFunction&)            = delete;
        CGFunction& operator=(const CGFunction&) = delete;

        /// curFunc's value when this activation began (null at top level) --
        /// the one saved snapshot a caller needs by name.
        llvm::Function* OuterFunc() const { return SavedFunc; }

        /// The %static_link parameter value in the current nested function,
        /// or null when at top level.
        llvm::Value* StaticLink{nullptr};
        /// Ordered list of outer variable names for this activation's frame
        /// struct.
        std::vector<std::string> OuterVarNames;
        /// The outer variables this activation reaches through its own
        /// static link, by name, as they were when the prologue exposed
        /// them.
        ///
        /// They are also defVar'd, but a local of the same name replaces
        /// that binding outright -- so after `procedure c; var n: integer`,
        /// the address of the enclosing `n` was gone.  Building a frame for
        /// a sibling then found c's own n and the callee wrote into the
        /// wrong activation.
        std::map<std::pair<size_t, std::string>, VarEntry> OuterVarBindings;
        /// Heap blocks a value conformant array parameter was copied into,
        /// freed where the body ends.  See the conformant branch of the
        /// parameter prologue.
        std::vector<llvm::Value*> ValueConformantCopies;

    private:
        Impl&              I;
        llvm::Function*    SavedFunc;
        llvm::AllocaInst*  SavedRetAlloca;
        llvm::Type*        SavedRetType;
        std::string        SavedFuncName;
        std::string        SavedNamePrefix;
        CGFunction*        SavedCurFn;
    };
    /// The activation CGFunction currently belongs to, or null before the
    /// first one is constructed. Set/restored by CGFunction's own
    /// constructor/destructor; buildStaticLinkFrame (CodeGenProcParams.cpp,
    /// staying a plain Impl method permanently) reaches OuterVarBindings
    /// through this rather than a raw field.
    CGFunction* curFn_{nullptr};

    std::map<std::string, std::vector<std::string>>  funcOuterVarNames_;
    /// The scope DEPTH each captured variable was found at, beside its name.
    ///
    /// A name is not an identity.  Two variables at different nesting levels
    /// may share one, and then a frame has two slots spelled alike -- and
    /// filling them by name gave both the innermost binding, so the outer
    /// variable was dropped and the callee read the wrong one.  The depth is
    /// fixed by nesting and is the same number in every activation, so it says
    /// WHICH variable a slot is for.
    std::map<std::string, std::vector<size_t>>       funcOuterVarDepths_;
    std::set<std::string>                            nestedFunctions_;

    // Everything a call site needs to know about one AST argument position of
    // a function's declared signature, keyed by mangled function name.
    //
    // Five of these were separate maps, each independently keyed by the same
    // mangled name and indexed by the same AST argument position, each
    // recording one fact about it.  Nothing tied them together: the four
    // call sites that build them (CodeGenProcs.cpp's four parameter-kind
    // branches) always pushed to all five in lockstep by construction, but a
    // future one that forgot a single push_back would desync that entry from
    // every other function's by one position, silently.  One vector of one
    // record removes the class of mistake outright — there is no longer a
    // second, third, fourth and fifth table to forget.
    //
    // (procParamThunks_ below looks like a sixth of these at a glance, but
    // isn't: it's keyed by the callee/signature pair a thunk was built for,
    // not by (function, argument position), so it stays its own cache.)
    struct ParamMeta {
        // EP §6.7.3.7: (loVarName, hiVarName) pairs, one per conformant
        // dimension.  Empty means this argument is not conformant.
        std::vector<std::pair<std::string,std::string>> conformantDims{};
        // EP §6.4.7: discriminant count; 0 means not a schema parameter.
        unsigned schemaDiscCount{0};
        // ISO §6.6.3.1: the declared signature, or null for an ordinary
        // argument.  A call site reads this to know it must pass the entry
        // point and frame pair, and what signature to build the thunk
        // against.
        const ProcedureTypeNode* procType{nullptr};
        // The ordinal bit 0 stands for, when this is a value parameter of
        // set type; 0 otherwise, which is the window a non-negative base
        // uses anyway.  A set argument compatible with a parameter whose
        // base begins elsewhere has to be moved into the callee's window to
        // arrive whole.
        int64_t setBase{0};
        // ISO §6.6.3.3 vs §6.6.3.2: whether this is a variable parameter,
        // and so takes the actual's address rather than its value.
        //
        // A call site cannot read this off the LLVM signature, where both a
        // var parameter and a value parameter of pointer type are simply
        // `ptr`.  Taken for the former, `procedure one(p: ^integer)` was
        // handed the address of the caller's pointer variable, and `p^` read
        // the variable rather than what it pointed at.
        bool byRef{false};
    };
    std::map<std::string, std::vector<ParamMeta>> paramMeta_;

    // ISO §6.6.3.1: uniform-signature thunks, keyed by the callee they wrap
    // and the signature they present it through.  See procParamThunk.
    std::map<std::pair<llvm::Function*, llvm::FunctionType*>, llvm::Function*>
        procParamThunks_;

    /// Is AST argument \p astArgIdx of \p mangledName a variable parameter?
    [[nodiscard]] bool paramIsByRef(const std::string& mangledName,
                                    size_t astArgIdx) const {
        auto it = paramMeta_.find(mangledName);
        return it != paramMeta_.end() && astArgIdx < it->second.size()
            && it->second[astArgIdx].byRef;
    }

    /// The procedural signature at AST argument \p astArgIdx, or null.
    [[nodiscard]] const ProcedureTypeNode*
    procParamArg(const std::string& mangledName, size_t astArgIdx) const {
        auto it = paramMeta_.find(mangledName);
        if (it == paramMeta_.end() || astArgIdx >= it->second.size())
            return nullptr;
        return it->second[astArgIdx].procType;
    }

    /// Pushes an actual for a conformant array formal: the array, then a lo/hi
    /// pair per dimension (EP §6.7.3.7).
    void pushConformantArgs(std::vector<llvm::Value*>& args, const ExprNode& arg,
                            size_t dims) {
        closureAbi_->pushConformantArgs(args, arg, dims);
    }

    /// The { entry point, frame } cell a procedural parameter is held in.
    /// Kept on Impl (not just forwarded) since ClosureAndCallABI's own
    /// procPairTy is private -- this is CodeGenProcs.cpp's own copy of the
    /// same one-liner, unaffected by the extraction.
    llvm::StructType* procPairTy() {
        return llvm::StructType::get(ctx, {ptrTy, ptrTy});
    }
    void storeProcPair(llvm::Value* cell, llvm::Value* fn, llvm::Value* frame) {
        closureAbi_->storeProcPair(cell, fn, frame);
    }
    /// Reads a closure cell back as (entry point, frame).
    std::pair<llvm::Value*, llvm::Value*> loadProcPair(llvm::Value* cell) {
        return closureAbi_->loadProcPair(cell);
    }

    /// The LLVM signature a procedural parameter is called through.
    llvm::FunctionType* procParamFnType(const ProcedureTypeNode& node) {
        return closureAbi_->procParamFnType(node);
    }

    /// A wrapper around \p target with that uniform signature.
    llvm::Function* procParamThunk(llvm::Function* target,
                                   const ProcedureTypeNode& node) {
        return closureAbi_->procParamThunk(target, node);
    }

    /// Pushes the (entry point, frame) pair for the procedure named by \p arg.
    void pushProcParamArgs(std::vector<llvm::Value*>& args, const ExprNode& arg,
                           const ProcedureTypeNode& node) {
        closureAbi_->pushProcParamArgs(args, arg, node);
    }

    /// The static-link frame a direct call to \p mangledName would build, or
    /// null when it needs none.  Stays on Impl -- resolves the
    /// closure-capture loop's own state, this project's standing
    /// extra-caution zone; ClosureAndCallABI reaches this through a
    /// closure, never absorbing it.
    llvm::Value* buildStaticLinkFrame(const std::string& mangledName);

    /// Emits a call through procedural parameter \p ve.  Returns null for a
    /// procedural (void) target.
    llvm::Value* emitProcParamCall(const VarEntry& ve,
                                   std::span<const std::unique_ptr<ExprNode>> args) {
        return closureAbi_->emitProcParamCall(ve, args);
    }

    llvm::BasicBlock* getOrCreateLabel(const std::string& name) {
        return stmtCore_->getOrCreateLabel(name);
    }

    /// Record \p block as the owner of its labels, and, if a goto from inside
    /// one of its procedures names any of them, plant the landing pad.  Emits
    /// at the current insertion point, which must be past the block's
    /// initialization: a goto landing here resumes the block, it does not
    /// restart it.
    void openLabelScope(const BlockNode& block, bool programBlock) {
        stmtCore_->openLabelScope(block, programBlock);
    }

    /// Plant the setjmp and the switch that dispatches on what it returns.
    /// Done for a procedure by openLabelScope; the program's block registers
    /// itself before its procedures are emitted and lands here later, once
    /// main exists to hold the setjmp and its variables are initialized.
    void emitLabelLanding() { stmtCore_->emitLabelLanding(); }

    /// Point the landing pad at the label blocks the body has by now created.
    void closeLabelScope() { stmtCore_->closeLabelScope(); }

    void emitGoto(const GotoStmt& s) { stmtCore_->emitGoto(s); }

    /// Whether range checking is on where \p Loc is.
    ///
    /// Not a flag on this object, because Turbo's `{$R+}` is positional: the
    /// same compilation checks one loop and not the next.  With no switch
    /// table -- which is every ISO 7185 and Extended Pascal compilation, since
    /// neither has directives -- this is the command-line default and nothing
    /// is searched, so the code emitted is what it was before there was a
    /// table.  See Basic/SwitchTable.h.
    [[nodiscard]] bool rangeChecksAt(SourceLocation Loc) const {
        return rangeGuards_->rangeChecksAt(Loc);
    }

    /// Mirrors LangOptions::NilChecks; see emitNilCheck.
    bool nilChecks = true;

    /// Mirrors LangOptions::OptLevel; see optimize.
    unsigned optLevel = 0;

    /// The language options this module is being generated under.
    ///
    /// Codegen took three scalars out of LangOptions and kept none of the rest,
    /// so it could not tell one dialect from another -- which was survivable
    /// only because every dialect difference so far is settled in the front
    /// end: of the thirty-four sites that ask, twenty-one are in Parse, nine in
    /// Sema, three in Lex and none here.
    ///
    /// Turbo is not like that.  Short-circuit `and`/`or`, the write field
    /// widths, the numbered run-time errors, one-byte enumerations and the
    /// whole parallel runtime are decisions made while generating code, and
    /// none of them can be made by a phase that does not know which language it
    /// is compiling.  The three scalars above stay as they are: they are read
    /// on hot paths and predate this.
    LangOptions langOpts;

    // ====================================================================
    // Initialize / reset for a new module
    // ====================================================================
    void init(const std::string& progName);

    /// Runs the LLVM optimization pipeline over the finished module.
    ///
    /// Lowering emits a stack slot per variable and reloads it at every use,
    /// which is the shape mem2reg and the rest of the middle end expect to
    /// clean up.  Nothing downstream does it for us: the driver hands -O to
    /// llc, and llc does instruction selection, not this.
    void optimize();

    // ====================================================================
    // Symbol table
    // ====================================================================
    /// findVar looks no further down than this.  EP §6.4.7: a schema body's
    /// bound expressions are written where the schema is DECLARED, and the only
    /// names in scope there are its own discriminants and compile-time
    /// constants.  Re-emitting them at an ALLOCATION site put the allocating
    /// procedure's locals in front of those names, so a `const k` used in a
    /// body was captured by any unrelated `var k` at the new() -- which sized
    /// the object from a run-time variable and corrupted the heap.
    size_t varLookupFloor_{0};

    /// DeclarationScopeOnly moved to CGSymbolTable -- it has no construction
    /// call sites anywhere in the current tree (confirmed by grep), so
    /// there is nothing here to keep forwarder-compatible.

    void pushScope() { symTab_->pushScope(); }
    void popScope()  { symTab_->popScope(); }

    /// Constants a variable declaration hid, one map per scope, put back when
    /// the scope ends.
    ///
    /// ISO §6.2.2.1: an identifier denotes its innermost enclosing definition.
    /// The constant table is flat and has no idea which is nearer, so reading a
    /// name always answered from it: `const size = 10;` with a `var size:
    /// integer` inside a procedure wrote 42 to the variable and read 10 back
    /// from the constant.  A required constant was already handled this way;
    /// every constant the program declares needed it too.
    std::vector<std::map<std::string, llvm::Value*>> shadowedConsts;
    /// debugIndirectPtr: when non-null (only ever passed when debug info is
    /// active), a stable alloca holding ptr's own value, for a caller whose
    /// ptr is itself unstable (a bare SSA value -- a load result, or a raw
    /// Argument -- rather than an alloca/GlobalVariable).  Registers the
    /// debug declare against the alloca with a DW_OP_deref expression
    /// instead of against ptr directly with an empty one; ordinary
    /// codegen still reads/writes through ptr, unchanged.  See defVar's
    /// body for why a bare SSA value needs this and an alloca doesn't.
    void defVar(const std::string& name, llvm::Value* ptr, llvm::Type* type,
                const TypeNode* typeNode = nullptr,
                llvm::Value* debugIndirectPtr = nullptr);
    const VarEntry* findVar(const std::string& name) const;

    /// Whether \p name is bound by a scope opened INSIDE the current function
    /// body -- which in practice means a with-statement.
    ///
    /// ISO §6.8.3.10 makes a with-statement's field designators an inner
    /// scope, so inside `with r do`, a field spelled like the enclosing
    /// function denotes the FIELD.  The function-result pseudo-variable is
    /// checked before the variable table, so it used to win: `with r do count
    /// := 99` stored into the result and left r.count alone.  Simply asking
    /// the variable table first would be wrong the other way -- inside
    /// function `count`, a *global* count is shadowed by the result -- so what
    /// matters is whether the binding is newer than the function's own scope.
    /// Look \p name up from this function's own scope outward, skipping the
    /// scopes a with-statement or a `for ... in` opened inside the body.
    [[nodiscard]] const VarEntry* findVarInFunctionScope(const std::string& name) const {
        return symTab_->findVarInFunctionScope(name);
    }

    [[nodiscard]] bool boundInsideFunction(const std::string& name) const {
        return symTab_->boundInsideFunction(name);
    }

    /// How many scopes were open when the current function body began.
    size_t curFuncScopeDepth{0};

    /// Bring a variable this unit imports into scope, and answer with it.
    /// \p semaTy describes it well enough to declare it when the module that
    /// owns it was compiled separately.  Null when there is no such variable.
    const VarEntry* resolveImportedVar(const std::string& name,
                                       const Type* semaTy);

    // ====================================================================
    // Type resolution -- CGTypes forwarders.  semaFieldType/layoutVariantCase/
    // layoutVariantPart/variantBlobType/llvmTypeOfSemaTypeImpl/
    // llvmTypeOfNodeViaSema/checkSizeAgreement/checkFieldOffsetAgreement/
    // checkSchemaFieldOffsetAgreement have zero external callers (confirmed
    // by grep) and so aren't forwarded at all -- CGTypes is the only caller
    // of its own internal helpers, same as every prior unit's purely-
    // internal methods.
    // ====================================================================
    llvm::StructType* strStructType(int64_t cap) { return cgTypes_->strStructType(cap); }
    /// Returns null when the name is not a known type; callers retry via Sema.
    llvm::Type* llvmTypeOfName(const std::string& name) { return cgTypes_->llvmTypeOfName(name); }

    /// The first and last index of \p n, or nothing when neither the bounds nor
    /// Sema's resolved type can supply them.  An index written as an ordinal
    /// type — `array[color]` — has no bound expressions to fold, so the range
    /// comes from the type Sema resolved for the node.
    std::optional<std::pair<int64_t, int64_t>>
    arrayIndexRange(const ArrayTypeNode& n) const { return cgTypes_->arrayIndexRange(n); }
    llvm::StructType* structTypeFor(const RecordTypeNode& rt) { return cgTypes_->structTypeFor(rt); }

    /// The layout of \p rt, building it if this is the first time it is asked
    /// for.  Never null.
    /// \p semaRec is the record type Sema resolved for THIS use, when there is
    /// one; its field types win over re-reading the denoters.  See CGTypes.
    const RecordLayout& layoutOf(const RecordTypeNode& rt,
                                 const Type* semaRec = nullptr) {
        return cgTypes_->layoutOf(rt, semaRec);
    }

    /// The layout of the record \p T, which is \p T's declaration laid out
    /// under the discriminants \p T was resolved with.  Null when T is not a
    /// record or has no declaration to lay out.
    const RecordLayout* layoutOfRecord(const Type& T) { return cgTypes_->layoutOfRecord(T); }

    /// `Impl::SchemaBindingScope` names `CGTypes::SchemaBindingScope`, so the
    /// one external construction site outside CGTypes
    /// (`CodeGenProcs.cpp`'s `emitInitialState`) keeps reading almost
    /// unchanged, just naming `*cgTypes_` instead of `*this`.
    using SchemaBindingScope = CGTypes::SchemaBindingScope;

    /// The storage a value of this type occupies, given both of the ways there
    /// are to say what the type is: the denoter it was written as and the type
    /// Sema resolved.  This is the one to call where storage is committed — a
    /// variable, a field, an allocation — because it is the only one that sees
    /// both answers and so the only one that can tell when they differ.
    ///
    /// Either may be null.  With a denoter the syntax decides, since it carries
    /// what the semantic type flattens away: the bounds, the variant tree, the
    /// declaration a typedef stands for.  With only a resolved type that
    /// decides.  With neither there is nothing to lay out and that is an
    /// internal error, not a pointer-sized guess.
    llvm::Type* llvmTypeOf(const TypeNode* denoter, const Type* resolved) {
        return cgTypes_->llvmTypeOf(denoter, resolved);
    }

    llvm::Type* llvmTypeOfNode(const TypeNode& node) { return cgTypes_->llvmTypeOfNode(node); }
    /// The integer type an ordinal denoter lowers to; see CGTypes.
    [[nodiscard]] llvm::Type* ordinalTyOf(const TypeNode& node) { return cgTypes_->ordinalTyOf(node); }
    /// Whether llvmTypeOfSemaType has a lowering for \p T; see CGTypes.
    static bool canLowerSemaType(const Type& T) { return CGTypes::canLowerSemaType(T); }
    llvm::Type* llvmTypeOfSemaType(const Type& T) { return cgTypes_->llvmTypeOfSemaType(T); }

    // ====================================================================
    // Alloca helpers
    // ====================================================================
    llvm::AllocaInst* createEntryAlloca(llvm::Type* ty, const std::string& name);

    /// A type denoter with any `packed` wrappers taken off.
    static const TypeNode* peelPackedNode(const TypeNode* tn) {
        while (auto* pk = llvm::dyn_cast_or_null<PackedTypeNode>(tn))
            tn = pk->Inner.get();
        return tn;
    }

    /// A { i64 len, [cap x i8] } temporary whose capacity is only known at run
    /// time -- the result of concatenating a `string(n)` whose n a discriminant
    /// fixes.  Sizing one of these by a constant is what silently truncated
    /// such a result to PlangMaxStringCapacity: 255 is the answer for a
    /// capacity nobody knows, and here somebody does, just not yet.
    ///
    /// The allocation lands where the builder is rather than in the entry
    /// block, because that is where its size is known.  A StackScope over the
    /// statement gives it back afterwards, so one in a loop costs a fixed
    /// amount of stack rather than one allocation per iteration.
    llvm::Value* createDynStrAlloca(llvm::Value* capV, const std::string& name);

    /// Restores the stack pointer on the way out, but only if something inside
    /// actually took a dynamic allocation -- a scope that costs nothing is one
    /// that can be put everywhere a statement is emitted without reading like
    /// an optimisation decision.  The save is spliced in at the point the scope
    /// opened, which is why the flag can be consulted at the end.
    class StackScope {
    public:
        explicit StackScope(Impl& I);
        ~StackScope();
        StackScope(const StackScope&)            = delete;
        StackScope& operator=(const StackScope&) = delete;
    private:
        Impl&        I;
        llvm::Instruction* Save;
        bool         SavedUsed;
    };
    /// Set by createDynStrAlloca, cleared and restored by StackScope.
    bool dynAllocaUsed_{false};

    // ====================================================================
    // String interning
    // ====================================================================
    llvm::GlobalVariable* internStrGV(const std::string& content);
    llvm::Value* internStrPtr(const std::string& content);
    /// A read-only { length, bytes } string struct holding \p content, which
    /// is the shape every string value is passed around as.
    llvm::Constant* internStrStruct(const std::string& content);

    // ====================================================================
    // External function declarations
    // ====================================================================
    llvm::Function* getExternFn(const std::string& name, llvm::FunctionType* ty);
    llvm::Function* getRTMathRR(const std::string& name);
    llvm::Function* getRTMathRI(const std::string& name);
    llvm::Function* getRTMathII(const std::string& name);
    llvm::Function* getRuntimeFn(const std::string& name, llvm::Type* argTy);
    llvm::Function* getRuntimeBoolFn(const std::string& name);
    llvm::Function* getExternFnN(const std::string& name,
                                  llvm::Type* retTy,
                                  std::vector<llvm::Type*> params);

    // ---- file-variable helpers ----
    /// The LLVM type of a file variable: storage laid out to match the
    /// runtime's PascalFile, checked field-by-field against it.  See CGTypes.
    llvm::StructType* fileStructType() { return cgTypes_->fileStructType(); }

    /// ISO §6.5.5: the address of the buffer variable \p fileExpr ^, which the
    /// runtime keeps beside the stream.
    llvm::Value* fileBufferPtr(const ExprNode& fileExpr) {
        return fileVarHelpers_->fileBufferPtr(fileExpr);
    }

    // ---- set helpers (ISO §6.7.2.4) ----
    /// Sets are a flat bitmask of PlangMaxSetElements bits.  Bit 0 stands for
    /// the base type's origin rather than for ordinal 0, so a base type
    /// reaching below zero still fits; see setBaseOffset.  Sema rejects base
    /// types that span more ordinals than there are bits.
    llvm::IntegerType* setTy() { return setOps_->setTy(); }
    /// Widens/narrows an integer to the set width.  Sets never flow through
    /// toI64, which would discard every ordinal above 63.
    llvm::Value* toSetWidth(llvm::Value* v);
    llvm::Value* clampToSetWidth(llvm::Value* v);
    /// The ordinal that bit 0 of e's set type stands for; 0 when e has no set
    /// type, which is the layout every non-negative base type uses anyway.
    int64_t setBaseOf(const ExprNode& e);
    /// Moves a set value from the window based at `from` into the one based at
    /// `to`.  Two compatible set types may be based at different ordinals, and
    /// a value crossing between them has to be shifted to keep its members.
    llvm::Value* alignSet(llvm::Value* v, int64_t from, int64_t to);
    /// Bit index for an ordinal in a set based at `base`.
    llvm::Value* setBitIndex(llvm::Value* ordinal, int64_t base);
    llvm::Value* emitSetSingleton(llvm::Value* ordinal, int64_t base);
    llvm::Value* emitSetRange(llvm::Value* lo, llvm::Value* hi, int64_t base);
    llvm::Value* emitSetMember(llvm::Value* ordinal, llvm::Value* set,
                               int64_t base);
    /// Lowers a set-valued or set-comparing binary operator; returns null if
    /// op is not one of them.
    llvm::Value* emitSetBinary(TokenKind op, llvm::Value* a, llvm::Value* b);

    // ---- complex-number helpers (EP §6.4.2.2) ----
    /// Returns the LLVM struct type for EP complex: { double, double }.
    llvm::StructType* complexTy() { return complexOps_->complexTy(); }

    /// EP §6.4.3.4: DateValid, year, month, day, TimeValid, hour, minute,
    /// second.  Built from and checked against PlangTimeStamp; see CGTypes.
    llvm::StructType* timestampStructType() { return cgTypes_->timestampStructType(); }

    /// EP §6.4.3.4: 'name' (a string(PlangMaxBindingName)) and 'bound', both
    /// required.  Built from and checked against PlangBindingType; see
    /// CGTypes.
    llvm::StructType* bindingStructType() { return cgTypes_->bindingStructType(); }

    /// Build a { double, double } aggregate from two double values.
    llvm::Value* makeComplex(llvm::Value* re, llvm::Value* im) {
        return complexOps_->makeComplex(re, im);
    }

    /// Coerce a scalar or complex value to a { double, double } complex aggregate.
    /// If the value is already complexTy, it is returned as-is.
    /// Integer values are first widened to double.
    llvm::Value* coerceToComplex(llvm::Value* v) {
        return complexOps_->coerceToComplex(v);
    }

    static bool isTextTypeName(const TypeNode* tn) {
        return FileVarHelpers::isTextTypeName(tn);
    }
    bool isFileVar(const ExprNode& e) { return fileVarHelpers_->isFileVar(e); }
    llvm::Value* fileVarPtr(const ExprNode& e) { return fileVarHelpers_->fileVarPtr(e); }
    /// Returns true if the expression is a typed binary file variable
    /// (file of T where T is not char/text).  Used to route binary I/O.
    /// The file type \p e denotes, looked up through a type name rather than
    /// read off the denoter, which may be one.  Null when \p e is not a file.
    const Type* fileTypeOf(const ExprNode& e) { return fileVarHelpers_->fileTypeOf(e); }
    bool isTypedBinaryFileVar(const ExprNode& e) {
        return fileVarHelpers_->isTypedBinaryFileVar(e);
    }
    /// Returns the byte-size of one component of a typed file variable.
    /// Returns 1 as a fallback for untyped or unknown files.
    int64_t getFileElemSize(const ExprNode& fileExpr) {
        return fileVarHelpers_->getFileElemSize(fileExpr);
    }
    /// The LLVM type of one component of \p fileExpr, or null for a text or
    /// untyped file.
    llvm::Type* getFileElemType(const ExprNode& fileExpr) {
        return fileVarHelpers_->getFileElemType(fileExpr);
    }
    /// EP §6.4.3.6/§6.7.5.2: the smallest value `a` of a direct-access file's
    /// declared index-type -- what SeekRead/SeekWrite/SeekUpdate measure an
    /// index AGAINST, and what position/LastPosition report relative TO
    /// (§6.7.6.6: `position(f) = succ(a, ...)`).  0 for a file with no
    /// declared index-type, which is every non-direct-access file.
    int64_t getFileIndexLow(const ExprNode& fileExpr) {
        return fileVarHelpers_->getFileIndexLow(fileExpr);
    }

    /// EP §6.7.3.7: the address of an element of a conformant array parameter,
    /// or null if \p e does not subscript one.  Takes the whole subscript
    /// chain, because the dimensions can only be folded together once.
    llvm::Value* emitConformantElemPtr(const IndexExpr& e) {
        return indexAccess_->emitConformantElemPtr(e);
    }
    llvm::Function* getRuntimeNewFn();
    llvm::Function* getRuntimeDisposeFn();
    llvm::Function* getRuntimeHaltFn();

    // ====================================================================
    // Basic block helpers
    // ====================================================================
    bool isTerminated() const {
        auto* bb = builder.GetInsertBlock();
        return bb && bb->getTerminator() != nullptr;
    }
    void brIfNeeded(llvm::BasicBlock* target) {
        if (!isTerminated()) builder.CreateBr(target);
    }

    // ====================================================================
    // EP String helpers
    // ====================================================================

    /// The EP string(N) type an expression denotes, or null.
    ///
    /// Looks THROUGH a schema whose body is a string.  EP §6.4.3.3 makes
    /// `string` a schema, so `type s(n: integer) = string(n); var v: s(10)`
    /// declares a string as surely as `var v: string(10)` does -- but v's type
    /// is a SchemaInstance, and asking only about Kind said no.  Codegen then
    /// treated v as an opaque aggregate: an assignment stored a pointer into
    /// it, and a comparison reached the internal error R6 put in the place
    /// where a capacity comes from neither a type nor a literal.  (Which is
    /// what surfaced this: the check was added for a case measured at zero, and
    /// the first thing it caught was a real one.)
    static const Type* varStrTypeOf(const Type* T) {
        if (!T) return nullptr;
        if (T->Kind == TypeKind::VarString) return T;
        // The body may itself be another schema instantiation (EP §6.4.7),
        // not just a string directly -- `B(n) = A(n)` for `A(m) = string(m)`
        // -- so this is asked of schemaUnderlying, not the immediate hop.  A
        // PLAIN variable of such a type (not reached through a pointer, which
        // had its own version of this fixed already) fell through every
        // caller of this function: assignment stored a raw pointer into it,
        // and comparison hit the internal error meant for a capacity that
        // comes from neither a type nor a literal.
        if ((T->Kind == TypeKind::Schema || T->Kind == TypeKind::SchemaInstance)
                && T->SchemaBody) {
            const Type* U = schemaUnderlying(T->SchemaBody.get());
            if (U->Kind == TypeKind::VarString) return U;
        }
        return nullptr;
    }
    static const Type* varStrTypeOf(const ExprNode& e) {
        return varStrTypeOf(e.ResolvedType.get());
    }

    /// True if the expression's resolved type is EP string(N).
    static bool exprIsVarStr(const ExprNode& e) { return varStrTypeOf(e); }

    /// Capacity of the expression's VarString type; 0 if not VarString.
    static int64_t exprStrCap(const ExprNode& e) {
        const Type* T = varStrTypeOf(e);
        return T ? T->StrCapacity : 0;
    }

    /// The same capacity as a value.  EP §6.4.3.3 makes `string` a schema whose
    /// one discriminant is the capacity, so for a `^string` the answer is in the
    /// header new() wrote and is not known until run time; StrCapacity holds the
    /// probe's answer and would check `q^ := 'hi'` against a string(1).
    llvm::Value* exprStrCapV(const ExprNode& e) { return schemaAccess_->exprStrCapV(e); }
    void setVarStrCap(const std::string& name, llvm::Value* cap) {
        schemaAccess_->setVarStrCap(name, cap);
    }
    void setVarSchemaPath(const std::string& name, const SchemaRef& root,
                          const TypeNode* decl) {
        schemaAccess_->setVarSchemaPath(name, root, decl);
    }

    /// The capacity to SIZE A TEMPORARY with, which has to be a constant.  A
    /// discriminant-fixed capacity is not one, and the probe's answer would cut
    /// the temporary to a single character, so such a string gets the widest
    /// capacity plang has -- every real capacity fits in it.  Use exprStrCapV
    /// wherever the capacity is a value the runtime is told, not a size.
    static int64_t exprStrCapStatic(const ExprNode& e) {
        const Type* T = varStrTypeOf(e);
        if (!T) return 0;
        const bool varies = T->ExtentVaries
                         || (e.ResolvedType && e.ResolvedType->ExtentVaries);
        return varies ? PlangMaxStringCapacity : T->StrCapacity;
    }

    /// EP §6.4.7 run-time layout, for a schema body whose extent a discriminant
    /// fixes.  Call under an RtDiscScope for the object being laid out: every
    /// extent in the body is a closed form over the discriminants BY INDEX, and
    /// is evaluated against that object's.  A subtree that reads no
    /// discriminant folds to a constant.
    uint64_t rtAlignOfTypeNode(const TypeNode* tn) {
        return schemaLayout_->rtAlignOfTypeNode(tn);
    }
    llvm::Value* rtSizeOfTypeNode(const TypeNode* tn) {
        return schemaLayout_->rtSizeOfTypeNode(tn);
    }
    /// The index bounds of \p at as run-time values.  The only place that
    /// answers this, so that the run-time walk and the static layout cannot
    /// disagree about how many elements an array has.
    std::optional<std::pair<llvm::Value*, llvm::Value*>>
    rtIndexBounds(const ArrayTypeNode& at) {
        return schemaLayout_->rtIndexBounds(at);
    }
    llvm::Value* rtFieldOffset(const RecordTypeNode& rt, const std::string& field) {
        return schemaLayout_->rtFieldOffset(rt, field);
    }
    llvm::Value* rtWalkFields(const std::vector<FieldDecl>& fields,
                              llvm::Value* off, bool packed,
                              const std::string* stopAt, bool* found) {
        return schemaLayout_->rtWalkFields(fields, off, packed, stopAt, found);
    }
    /// One walk over a variant part: its size with \p stopAt null, or the
    /// offset of that field with \p stopAt set and *found written.
    llvm::Value* rtWalkVariant(const VariantPart& vp, llvm::Value* off,
                               bool packed, const std::string* stopAt,
                               bool* found, bool nested = false) {
        return schemaLayout_->rtWalkVariant(vp, off, packed, stopAt, found, nested);
    }
    /// What the shared run of a variant part must be aligned to; see the
    /// definition for why the outer tag is not part of the answer.
    uint64_t rtVariantRunAlign(const VariantPart& vp) {
        return schemaLayout_->rtVariantRunAlign(vp);
    }
    uint64_t rtVariantAlign(const VariantPart& vp) {
        return schemaLayout_->rtVariantAlign(vp);
    }

    std::optional<SchemaPath> schemaPathOf(const ExprNode& e) {
        return schemaAccess_->schemaPathOf(e);
    }
    /// Address and capacity of a string from one walk of its access path.
    std::pair<llvm::Value*, llvm::Value*> strAddrAndCap(const ExprNode& e) {
        return schemaAccess_->strAddrAndCap(e);
    }
    llvm::Value* strCapFromPath(const SchemaPath& path) {
        return schemaAccess_->strCapFromPath(path);
    }
    /// Descend into a nested schema instantiation; see SchemaAccess.
    std::pair<SchemaRef, const TypeNode*>
    descendIntoInstantiation(const SchemaRef& root, llvm::Value* addr,
                             const TypeNode* decl) {
        return schemaAccess_->descendIntoInstantiation(root, addr, decl);
    }
    const TypeNode* fieldDenoterOf(const RecordTypeNode& rt, const std::string& field) {
        return schemaAccess_->fieldDenoterOf(rt, field);
    }
    const TypeNode* variantFieldDenoterOf(const VariantPart& vp, const std::string& field) {
        return schemaAccess_->variantFieldDenoterOf(vp, field);
    }
    llvm::Value* alignUpV(llvm::Value* v, uint64_t align) {
        return schemaLayout_->alignUpV(v, align);
    }
    const ArrayTypeNode* varyingArrayFieldOf(const FieldExpr& fe) {
        return schemaAccess_->varyingArrayFieldOf(fe);
    }

    /// True if the expression is an ISO §6.4.3.2 string-type: a
    /// packed array[1..n] of char, which is n bytes with no length and no
    /// terminator, quite unlike either of the other two string shapes.
    static bool exprIsCharStr(const ExprNode& e) {
        return e.ResolvedType && isCharStringType(*e.ResolvedType);
    }

    /// The n of the expression's string-type, or 0.
    static int64_t exprCharStrLen(const ExprNode& e) {
        return exprIsCharStr(e) ? charStringLength(*e.ResolvedType) : 0;
    }

    /// A string-type value as a temporary string(n), so that the runtime that
    /// already writes and compares strings can be used on it unchanged.  The
    /// length is fixed at n: every character of the array is part of the value.
    llvm::Value* emitCharStrAsStr(const ExprNode& e) {
        return strCallMarshal_->emitCharStrAsStr(e);
    }

    /// Store a string value into a string-type variable of length \p n at
    /// \p dst — exactly n bytes, with no length field to update.
    void emitCharStrStore(llvm::Value* dst, int64_t n, const ExprNode& src) {
        strCallMarshal_->emitCharStrStore(dst, n, src);
    }

    /// Resolve the LLVM mangled name for a Pascal procedure/function call.
    /// Walks outward through the nesting hierarchy so that a call to 'inner'
    /// from inside 'outer' finds 'plang_outer__inner', while a call to a
    /// top-level 'helper' from inside 'outer' falls back to 'plang_helper'.
    /// Drops the module qualifier from an EP §6.11.2 qualified name, leaving
    /// the identifier.  The module it names is recovered separately, by
    /// importOwner, because it is part of the mangled name.
    static std::string stripQualifier(const std::string& name) {
        return CGLinkage::stripQualifier(name);
    }

    std::string findMangledProc(const std::string& qualifiedName) const {
        return linkage_->findMangledProc(qualifiedName);
    }

    /// The symbol naming the global variable \p name denotes, mangled with the
    /// module that declares it.
    std::string mangledGlobal(const std::string& qualifiedName) const {
        return linkage_->mangledGlobal(qualifiedName);
    }

    /// The value of \p e in memory, for reading a component of a function
    /// result, which is a value with nowhere of its own to live.
    llvm::Value* spillToTemporary(const ExprNode& e) { return exprCore_->spillToTemporary(e); }

    llvm::Function* getStrFn(const std::string& name, llvm::Type* retTy,
                              std::initializer_list<llvm::Type*> argTys) {
        return strings_->getStrFn(name, retTy, argTys);
    }
    llvm::Value* strLoadLen(llvm::Value* strPtr) { return strings_->strLoadLen(strPtr); }
    llvm::Value* strDataPtr(llvm::Value* strPtr) { return strings_->strDataPtr(strPtr); }
    // EP §6.4.7: a capacity fixed by a schema discriminant is not a literal, so
    // these take it as a value.  The int64_t overloads wrap a constant and are
    // what every fixed-capacity caller still uses, so their IR is unchanged.
    void emitStrAssign(llvm::Value* dst, llvm::Value* capDst,
                       llvm::Value* src, llvm::Value* capSrc) {
        strings_->emitStrAssign(dst, capDst, src, capSrc);
    }
    void emitStrAssign(llvm::Value* dst, int64_t capDst,
                       llvm::Value* src, int64_t capSrc) {
        emitStrAssign(dst, i64c(capDst), src, i64c(capSrc));
    }
    /// Runs emitFail on a cold path taken when failCond holds, then leaves the
    /// builder on the success path.  The reporters never return.
    void emitGuard(llvm::Value* failCond, const char* name,
                   llvm::function_ref<void()> emitFail);
    void emitDivZeroCheck(llvm::Value* divisor, const char* op);
    /// ISO §6.7.2.2: mod requires a positive divisor, so this rejects zero and
    /// negatives together and replaces emitDivZeroCheck for that operator.
    void emitModDivisorCheck(llvm::Value* divisor);
    /// ISO §6.5.4: p^ where p is nil.  No-op unless range checking is enabled.
    void emitNilCheck(llvm::Value* ptr);
    /// No-op unless range checking is enabled; isIndex picks the wording.
    void emitRangeCheck(llvm::Value* val, int64_t lo, int64_t hi, bool isIndex,
                        SourceLocation Loc);
    /// emitRangeCheck for bounds that are only known at run time.
    void emitRangeCheckDyn(llvm::Value* val, llvm::Value* lo, llvm::Value* hi,
                           bool isIndex, SourceLocation Loc);

    // ====================================================================
    // EP §6.4.7: undiscriminated schema types (CodeGenSchema.cpp)
    // ====================================================================
    /// Records the schemas declared in `block` so their bodies can be
    /// re-emitted with run-time discriminants.
    void registerSchemaDefs(const BlockNode& block);
    const SchemaTypeRegistry::SchemaDef* findSchemaDef(const std::string& name) const;
    const TypeNode* schemaBodyNodeOf(const plang::Type& T) const;
    /// The run-time view of `e`, or nullopt when `e` is not schematic.
    /// May emit loads, so call it once per use.
    std::optional<SchemaRef> schemaRefOf(const ExprNode& e) {
        return schemaAccess_->schemaRefOf(e);
    }
    /// Body pointer and discriminants to pass for a schema formal parameter.
    std::pair<llvm::Value*, std::vector<llvm::Value*>>
        schemaActual(const ExprNode& arg, unsigned discCount) {
        return schemaAccess_->schemaActual(arg, discCount);
    }
    /// Discriminant count for one argument position of `mangledName`;
    /// zero when that parameter is not an undiscriminated schema.
    unsigned schemaArgDiscs(const std::string& mangledName, size_t astArgIdx) const {
        return schemaAccess_->schemaArgDiscs(mangledName, astArgIdx);
    }
    void pushSchemaArgs(std::vector<llvm::Value*>& args, const ExprNode& arg,
                        unsigned discCount) {
        schemaAccess_->pushSchemaArgs(args, arg, discCount);
    }
    /// Traps unless the two schematic values carry the same discriminants,
    /// which EP §6.7.3.2 requires for them to be assignment-compatible.
    void emitSchemaDiscMatch(const SchemaRef& dst, const SchemaRef& src) {
        schemaAccess_->emitSchemaDiscMatch(dst, src);
    }
    /// Bounds of an array-bodied schema, computed from `ref`'s discriminants.
    std::pair<llvm::Value*, llvm::Value*> schemaArrayBounds(const SchemaRef& ref) {
        return schemaAccess_->schemaArrayBounds(ref);
    }
    /// R3: a closed extent form evaluated against an object's discriminants.
    /// Bytes of discriminant header in front of a schema body; see the definition.
    uint64_t schemaHeaderBytes(const plang::Type& schema) {
        return schemaLayout_->schemaHeaderBytes(schema);
    }
    llvm::Value* emitExtentForm(const plang::ExtentForm& F,
                                const std::vector<llvm::Value*>& discs) {
        return schemaLayout_->emitExtentForm(F, discs);
    }
    /// R3: makes an object's discriminants the ones extent forms are evaluated
    /// against, for as long as the guard lives.  A thin adapter over
    /// SchemaLayoutEngine::RtDiscScope so every existing call site
    /// (`RtDiscScope disc(*this, ...)`, `*this` being an Impl&) keeps working
    /// unchanged; the discriminant stack itself lives on schemaLayout_ now.
    struct RtDiscScope {
        SchemaLayoutEngine::RtDiscScope Inner;
        RtDiscScope(Impl& I, const std::vector<llvm::Value*>& D)
            : Inner(*I.schemaLayout_, D) {}
    };
    /// R3: a denoter's low and high extents evaluated against the discriminants
    /// of the object it was reached through, so no identifier in it is ever
    /// resolved in the procedure doing the access.  Absent when Sema recorded
    /// no form, which outside a schema body is the ordinary case.
    std::optional<std::pair<llvm::Value*, llvm::Value*>>
    boundsOfDenoter(const TypeNode& D, const SchemaRef& Root) {
        return schemaLayout_->boundsOfDenoter(D, Root.discs);
    }
    /// LLVM type of the schema body's storage: the element type for an array
    /// body, the whole body otherwise.
    llvm::Type* schemaStorageType(const SchemaRef& ref) {
        return schemaAccess_->schemaStorageType(ref);
    }
    /// Size in bytes of one schematic value with the given discriminants.
    llvm::Value* schemaBodySize(const plang::Type& schema,
                                const std::vector<llvm::Value*>& discs) {
        return schemaAccess_->schemaBodySize(schema, discs);
    }
    /// EP §6.7.5.3: new(p, d1..ds) for a pointer whose domain is a schema.
    void emitNewSchema(const ExprNode& ptrArg, const plang::Type& schema,
                       std::span<const std::unique_ptr<ExprNode>> discArgs) {
        schemaAccess_->emitNewSchema(ptrArg, schema, discArgs);
    }
    void emitStrFromCStr(llvm::Value* dst, llvm::Value* cap, llvm::Value* cstr) {
        strings_->emitStrFromCStr(dst, cap, cstr);
    }
    void emitStrFromCStr(llvm::Value* dst, int64_t cap, llvm::Value* cstr) {
        emitStrFromCStr(dst, i64c(cap), cstr);
    }
    void emitStrFromChar(llvm::Value* dst, llvm::Value* cap, llvm::Value* c) {
        strings_->emitStrFromChar(dst, cap, c);
    }
    void emitStrFromChar(llvm::Value* dst, int64_t cap, llvm::Value* c) {
        emitStrFromChar(dst, i64c(cap), c);
    }
    /// Store \p src into the string variable at \p dst, whose capacity is
    /// \p capDst.  A string is a length and a buffer, so which runtime call
    /// this takes depends on what the source is; assignment and the 'value'
    /// initializer both come through here.
    void emitStrStore(llvm::Value* dst, llvm::Value* capDst, const ExprNode& src) {
        strCallMarshal_->emitStrStore(dst, capDst, src);
    }
    void emitStrStore(llvm::Value* dst, int64_t capDst, const ExprNode& src) {
        emitStrStore(dst, i64c(capDst), src);
    }

    /// The address of the { length, bytes } struct a string expression denotes,
    /// which is what every string runtime entry point takes.  Whether that is
    /// the expression's own storage or a temporary depends on the expression,
    /// and getting it wrong is the difference between a component and the whole
    /// structure it sits in.
    llvm::Value* emitStrAddr(const ExprNode& e) { return strCallMarshal_->emitStrAddr(e); }
    /// One argument of a call to a user-declared procedure or function, given
    /// the LLVM type the callee declared for that position: an address for a
    /// var parameter, a copy for a string, the value otherwise.  \p byRef says
    /// the formal is a variable parameter, which the LLVM type cannot: a value
    /// parameter of pointer type is declared `ptr` there as well.
    llvm::Value* emitCallArg(const ExprNode& arg, llvm::Type* paramTy,
                             bool byRef) {
        return strCallMarshal_->emitCallArg(arg, paramTy, byRef);
    }

    // ====================================================================
    // Globals
    // ====================================================================
    void emitGlobals(const BlockNode& block);
    void registerInterfaceTypes(const BlockNode& iface,
                                const std::string& unit);
    /// EP §6.11.1: storage and names for what a module's heading declares and
    /// its block does not.
    void emitInheritedGlobals(const BlockNode& iface, const BlockNode& own);
    void emitFileParams(const std::vector<std::string>& names);
    void emitFileParamBinds(const std::vector<std::string>& names);

    // ====================================================================
    // Procedures and functions
    // ====================================================================
    void emitAllProcedures(const BlockNode& block);
    /// With declareOnly, stops once the signature and the parameter metadata
    /// are in place — what a 'forward' declaration contributes.
    void emitFunctionDef(const ProcDecl& proc, bool declareOnly = false);
    /// Introduce the constants of any enumeration written inside \p tn.
    void registerEnumValues(const TypeNode* tn);
    /// The same for a variant part and the variants nested in it.
    void registerVariantEnumValues(const VariantPart& vp);
    void emitBlockDecls(const BlockNode& block);
    /// The value a named constant stands for; null when it can only be
    /// computed inside a basic block.
    llvm::Value* constantValueOf(const ConstDef& cd);
    /// EP §6.4.1: the 'value' clause of a variable declaration, for both local
    /// and program-level variables.
    void emitVarValueInit(const VarGroup& vg);
    /// EP §6.6: puts a written value into a variable's storage.
    void storeInitialValue(llvm::Value* ptr, llvm::Type* ty,
                           const TypeNode* tn, const ExprNode& value);
    /// The 'value' clause of a denoter, found through the names it is written
    /// with; `carrier` receives the denoter that bears it.
    const ExprNode* writtenInitialState(const TypeNode* tn,
                                        const TypeNode** carrier = nullptr) const;
    /// The shape of a denoter, resolving type names as Sema did; see the
    /// definition for why denoterOf is the wrong question here.
    const TypeNode* initialStateShapeOf(const TypeNode* tn) const;
    bool hasInitialState(const TypeNode* tn, int depth = 0) const;
    /// The body denoter a schema instantiation `t(5)` stands for.
    const TypeNode* schemaInstanceBody(const TypeNode* tn) const;
    /// EP §6.6: brings a variable of the denoter's type to the state such a
    /// variable begins in.
    void emitInitialState(llvm::Value* ptr, llvm::Type* ty,
                          const TypeNode* tn, int depth = 0);
    /// EP §6.8.7: gives a constant that is an array, record or set value the
    /// storage it needs, to be filled in by emitGlobalVarInits.
    void emitStructuredConst(const ConstDef& cd);
    /// The storage type of such a constant, and the declaration it came from.
    llvm::Type* structuredConstType(const ConstDef& cd, const TypeNode*& tn);
    /// Those constants, in the order they were declared.
    std::vector<const ConstDef*> structuredConsts_;
    /// The capacity of a string-typed declaration, resolving a type written
    /// through a name; 0 when the declaration is not a string.
    int64_t declaredStrCapacity(const TypeNode* tn);
    void emitBlockAllocas(const BlockNode& block);
    /// EP §6.8.2: a module constant whose value has to be computed; see
    /// emitRuntimeConst.  Emptied by emitRuntimeConstInits.
    std::vector<const ConstDef*> runtimeConsts_;
    void emitRuntimeConst(const ConstDef& cd);
    void emitRuntimeConstInits();

    void emitGlobalVarInits(const BlockNode& block);
    void emitGlobalVarInit(const VarGroup& vg);
    /// EP §6.11.2: the runtime initialization of a module's own globals, for
    /// its initialiser to run.  \p iface is the module's heading, whose
    /// declarations are the block's too, or null when it has none.
    void emitModuleGlobalInits(const BlockNode& own, const BlockNode* iface);
    /// \p initModules names the modules to bring up before the program body,
    /// as Pascal module names rather than symbols.
    void emitMain(const BlockNode& block,
                  const std::vector<std::string>& fileParams,
                  const std::vector<std::string>& initModules = {});

    /// EP §6.11: emit a module initialization or finalization function.
    /// The function is named fnName, emits stmt, and returns void.
    void emitModuleLifecycleFn(const std::string& fnName, const StmtNode& stmt);

    /// EP §6.11.2: emit __plang_init_<module> for \p mod and return its name.
    /// One is emitted for every module, whether or not it has a 'to begin do',
    /// because an importer has to be able to call it without knowing.
    std::string emitModuleInitFn(const ModuleNode& mod);

    /// The name of the initialiser for the module called \p moduleName,
    /// declaring it if this unit did not emit it.
    llvm::Function* moduleInitFn(const std::string& moduleName);

    // ====================================================================
    // Statement emission
    // ====================================================================
    void emitStmt(const StmtNode* stmt) { stmtCore_->emitStmt(stmt); }
    void resumeAfterTerminator() { stmtCore_->resumeAfterTerminator(); }
    void emitCompound(const CompoundStmt& s) { stmtCore_->emitCompound(s); }
    void emitAssign(const AssignStmt& s) { assign_->emitAssign(s); }
    void emitIf(const IfStmt& s) { controlFlow_->emitIf(s); }
    void emitWhile(const WhileStmt& s) { controlFlow_->emitWhile(s); }
    void emitFor(const ForStmt& s) { controlFlow_->emitFor(s); }
    void emitForIn(const ForInStmt& s) { controlFlow_->emitForIn(s); }
    void emitPackUnpack(const CallStmt& s, bool isPack) { packUnpack_->emitPackUnpack(s, isPack); }
    void emitRepeat(const RepeatStmt& s) { controlFlow_->emitRepeat(s); }
    void emitCase(const CaseStmt& s) { controlFlow_->emitCase(s); }
    void emitWith(const WithStmt& s) { with_->emitWith(s); }
    void emitCallStmt(const CallStmt& s) { procCall_->emitCallStmt(s); }
    /// The tail of emitCallStmt: a call to a procedure the program declared,
    /// reached either by falling past the required ones or, when the name is
    /// one of theirs, directly.  See CallStmt::ResolvedBuiltin.
    void emitUserProcCall(const CallStmt& s) { procCall_->emitUserProcCall(s); }

    // ====================================================================
    // Built-in write / writeln / read
    // ====================================================================
    void emitBuiltinWrite(const std::vector<std::unique_ptr<ExprNode>>& args, bool newline) {
        builtinIO_->emitBuiltinWrite(args, newline);
    }
    void emitBuiltinWriteStr(const std::vector<std::unique_ptr<ExprNode>>& args) {
        builtinIO_->emitBuiltinWriteStr(args);
    }
    void emitBuiltinReadStr(const std::vector<std::unique_ptr<ExprNode>>& args) {
        builtinIO_->emitBuiltinReadStr(args);
    }
    /// Whether an ordinal's values are unsigned in their LLVM representation.
    /// ISO §6.4.2.2 orders every ordinal by its ordinal number, which is never
    /// negative for these three; a signed compare would read boolean 'true'
    /// (i1 1) as -1 and the upper half of the char set as negative.
    static bool ordinalIsUnsigned(const plang::Type* t) {
        while (t && t->Kind == TypeKind::Subrange && t->SubBase) t = t->SubBase.get();
        return t && (t->Kind == TypeKind::Boolean || t->Kind == TypeKind::Char
                     || t->Kind == TypeKind::Enum);
    }
    void emitBuiltinRead(const std::vector<std::unique_ptr<ExprNode>>& args) {
        builtinIO_->emitBuiltinRead(args);
    }
    void emitBuiltinReadln(const std::vector<std::unique_ptr<ExprNode>>& args) {
        builtinIO_->emitBuiltinReadln(args);
    }

    // ====================================================================
    // Expression emission
    // ====================================================================
    llvm::Value* emitExpr(const ExprNode& e) { return exprCore_->emitExpr(e); }
    llvm::Value* emitLValue(const ExprNode& e) { return exprCore_->emitLValue(e); }
    llvm::Value* emitLValueOpt(const ExprNode& e) { return emitLValue(e); }
    llvm::Value* emitBinary(const BinaryExpr& e) { return binaryOps_->emitBinary(e); }
    llvm::Value* emitUnary(const UnaryExpr& e) { return binaryOps_->emitUnary(e); }
    llvm::Value* emitCallExpr(const CallExpr& e) { return funcCall_->emitCallExpr(e); }
    /// The tail of emitCallExpr: a functional parameter, or a call to a
    /// function the program declared.  See CallExpr::ResolvedBuiltin.
    llvm::Value* emitUserFuncCall(const CallExpr& e) { return funcCall_->emitUserFuncCall(e); }
    llvm::Value* emitIndexGEP(const IndexExpr& e) { return indexAccess_->emitIndexGEP(e); }
    llvm::Value* emitIndexLoad(const IndexExpr& e) { return indexAccess_->emitIndexLoad(e); }
    llvm::StructType* resolveRecordStructType(const FieldExpr& e) {
        return fieldAccess_->resolveRecordStructType(e);
    }
    /// The type of the field a field expression selects, which for a variant
    /// field is not the type of the struct element it shares with the others.
    llvm::Type* fieldLlvmType(const FieldExpr& e) { return fieldAccess_->fieldLlvmType(e); }
    llvm::Value* emitFieldGEP(const FieldExpr& e) { return fieldAccess_->emitFieldGEP(e); }
    /// Alignment a load/store through this expression may claim; see the
    /// definition.  nullopt means the value type's ABI alignment is honest.
    std::optional<llvm::Align> packedAccessAlign(const ExprNode& e) {
        return fieldAccess_->packedAccessAlign(e);
    }
    llvm::Value* emitFieldLoad(const FieldExpr& e) { return fieldAccess_->emitFieldLoad(e); }
    llvm::Value* emitDerefLoad(const DerefExpr& e) { return fieldAccess_->emitDerefLoad(e); }
    /// EP §6.8.7: emit a typed value constructor (array/record/set).
    /// For set constructors returns an i64 bitmask.
    /// For array/record constructors returns a ptr to a temporary alloca.
    /// EP §6.8.7: a value constructor.  `denoter` gives the shape for a
    /// component-value, which names no type of its own.
    llvm::Value* emitStructuredValue(const StructuredValueExpr& e,
                                     const TypeNode* denoter = nullptr) {
        return structuredValue_->emitStructuredValue(e, denoter);
    }
    const TypeNode* denoterOf(const TypeNode* tn) const;
    llvm::Value* ensureI1(llvm::Value* v);
    llvm::Value* toDouble(llvm::Value* v);
    llvm::Value* toI64(llvm::Value* v);
    llvm::Value* coerceToType(llvm::Value* v, llvm::Type* dst);
};
