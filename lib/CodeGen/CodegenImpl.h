// CodegenImpl.h — Private implementation header for Codegen.
// NOT installed; used only by the CodeGen translation units.
#pragma once

#include "plang/Basic/LangOptions.h"
#include "plang/Basic/PascalFileLayout.h"
#include "plang/CodeGen/Codegen.h"
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

using namespace plang;

// ---------------------------------------------------------------------------
// Free utility functions (defined in Codegen.cpp, used by multiple TUs).
// ---------------------------------------------------------------------------

std::optional<int64_t> tryEvalConstInt(
        const ExprNode& e,
        const std::unordered_map<std::string, llvm::Value*>* known = nullptr);

int64_t evalConstInt(const ExprNode& e, int64_t fallback,
                     const std::unordered_map<std::string, llvm::Value*>* known = nullptr);

llvm::Constant* evalConst(
        const ExprNode& e,
        const std::unordered_map<std::string, llvm::Value*>& known,
        llvm::LLVMContext& ctx,
        llvm::IntegerType* i64Ty,
        llvm::Type* dblTy);

/// Aborts on an internal codegen inconsistency.
///
/// Reaching one of these means Sema accepted a construct that codegen cannot
/// lower.  Returning a placeholder value instead would emit a program that
/// compiles and runs but computes the wrong answer, so failing loudly here is
/// the only way such a gap becomes visible.
[[noreturn]] inline void codegenICE(const llvm::Twine& What) {
    llvm::report_fatal_error(llvm::Twine("plang codegen: ") + What, false);
}

// ---------------------------------------------------------------------------
// Name mangling
//
// Every symbol a compiled program defines for something the *source* named — a
// procedure, a function, a variable — is built from one of these prefixes.  The
// runtime's own ~150 entry points are the other half of the same link, and they
// are spelled `plang_*`.
//
// So user code may not be spelled `plang_*` too, which it was until 0.1.3: a
// procedure the program called `close`, `round`, `page` or `halt` — thirty-three
// names collided, twenty-four of them required identifiers ISO §6.2.2.10
// entitles a program to redeclare — was emitted as `plang_close`, the same
// symbol the runtime defines.  Whether that was caught depended on whether the
// runtime translation unit holding the twin happened to be pulled out of the
// archive for some other reason, so it was a duplicate-symbol error in some
// programs and silence in others.
//
// `pas_` and `pasg_` cannot collide with the runtime whatever the program
// declares, since nothing in the runtime begins with either.
//
// An enclosing scope — a module, or a procedure a procedure is nested in — is
// joined on with `$`.  Extended Pascal §6.1.3 allows an underscore inside an
// identifier, so the `__` that used to join them was itself something a name
// could contain, and a module `a` exporting `b` and a top-level `a__b` both
// wanted the same symbol.  `$` is not in the Pascal alphabet, so a mangled name
// now separates into its parts exactly one way.  It is accepted unquoted in an
// LLVM identifier, and in an ELF and a Mach-O symbol.
// ---------------------------------------------------------------------------

/// Prefix for a procedure or function the source declares.
inline constexpr const char* PlangProcPrefix   = "pas_";
/// Prefix for a variable the source declares at file or module scope.
inline constexpr const char* PlangGlobalPrefix = "pasg_";
/// Joins an enclosing scope to what it declares.  Must be something no Pascal
/// identifier can contain, or a mangled name is ambiguous; see above.
inline constexpr const char* PlangScopeSep     = "$";

// ---------------------------------------------------------------------------
// Impl — contains all LLVM objects
// ---------------------------------------------------------------------------

struct Codegen::Impl {
    // ---- LLVM core objects ----
    llvm::LLVMContext            ctx;
    std::unique_ptr<llvm::Module> mod;
    llvm::IRBuilder<>            builder{ctx};

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
    struct VarEntry {
        llvm::Value*     ptr;               // alloca or GlobalVariable (used as ptr)
        llvm::Type*      type;              // the *value* type (pointee)
        const TypeNode*  typeNode{nullptr}; // original Pascal TypeNode (for file detection)
        // EP §6.7.3.7: conformant array fields.
        bool        isConformantArray{false};  // true if this is a conformant array param
        std::string conformantLoName{};        // name of the lo bound variable
        std::string conformantHiName{};        // name of the hi bound variable
        llvm::Type* conformantElemTy{nullptr}; // LLVM element type of the conformant array
        /// The bound-variable names of every dimension, outermost first.  A
        /// multi-dimensional conformant array is one flat block whose row
        /// width is only known from the inner bounds, so indexing needs all of
        /// them and not just the outermost pair.
        std::vector<std::pair<std::string, std::string>> conformantDims{};
        /// The bound variables themselves, in the same order.
        ///
        /// The names above are what the programmer wrote in the parameter
        /// list, and re-resolving them wherever a subscript appears is what
        /// let an inner scope answer instead: a record with fields spelled
        /// `lo` and `hi` made `x[5]` inside `with r do` adjust by r.lo rather
        /// than by the array's own bound, and read out of the block.  The
        /// bound belongs to this activation and its address is known when the
        /// prologue creates it, so it is kept rather than looked up again.
        std::vector<std::pair<llvm::Value*, llvm::Value*>> conformantDimPtrs{};
        // EP §6.4.7: set for an undiscriminated schema formal parameter.  The
        // discriminants arrive as extra arguments, so they are plain Values
        // that stay live for the whole activation.
        const plang::Type*        schemaTy{nullptr};
        std::vector<llvm::Value*> schemaDiscs{};
        // ISO §6.6.3.1: set for a procedural or functional formal parameter.
        // ptr addresses a { ptr, ptr } cell holding the closure pair: where to
        // jump, and the frame the target reads its own outer variables
        // through.  The pair arrives in registers and could have been kept
        // there, but then a nested procedure could not reach it — a static
        // link carries one address per outer variable, and a value belonging
        // to another activation is not something this one may name.
        bool                       isProcParam{false};
        const ProcedureTypeNode*   procType{nullptr};
        /// EP §6.4.7: a `with`-bound field of a run-time-laid-out record has a
        /// capacity its object carries, and once bound it is an ordinary name
        /// with no path back to the object.  Recorded here so that
        /// `with p^ do s := ...` checks against the real capacity rather than
        /// the probe's string(1).  Last, and default-initialised, so that every
        /// existing aggregate initialisation of this struct is unaffected.
        llvm::Value*     strCapV{nullptr};
    };
    std::vector<std::unordered_map<std::string, VarEntry>> scopes;

    // Constants from Pascal 'const' sections — folded to LLVM Values inline.
    std::unordered_map<std::string, llvm::Value*> consts;

    // ---- caches ----
    std::map<std::string, llvm::GlobalVariable*> strGVs;      // interned string GVs
    // Interned { length, bytes } string structs, for constants whose value is
    // used where a string variable would be.
    std::map<std::string, llvm::GlobalVariable*> strStructGVs;
    std::map<std::string, llvm::StructType*>     structTypes;  // record struct types

    /// Where one field of a record lives.  A field of the fixed part is an
    /// element of the struct.  ISO §6.4.3.3 has at most one variant active at a
    /// time, so the alternatives share a single run of storage: their fields
    /// are all placed in the one element standing for the variant part, each at
    /// its own byte offset within it.
    struct FieldPlace {
        unsigned    Index{0};        ///< element index in the struct
        llvm::Type* Ty{nullptr};     ///< the field's own type
        bool        InVariant{false};
        uint64_t    Offset{0};       ///< byte offset into the variant element
    };
    struct RecordLayout {
        llvm::StructType* Ty{nullptr};
        /// Field name, folded to lower case, to where it lives.
        std::map<std::string, FieldPlace> Fields;
    };
    /// Keyed by declaration, not by struct type: two records may be laid out
    /// identically and share one llvm::StructType while naming their fields
    /// differently, and the variant tree that decides the offsets is only on
    /// the declaration.  The declaration alone is not enough for a record in a
    /// schema body, where one of them lays out differently in each
    /// instantiation, so the discriminants in force go into the key too.
    std::map<std::pair<const RecordTypeNode*, std::string>, RecordLayout>
        recordLayouts;

    /// The discriminants a layout is currently being worked out under, written
    /// as `name=value` pairs.  Empty everywhere outside a schema body.
    std::string schemaCtx;

    std::map<std::string, llvm::Function*>       externFuncs;  // declared externals
    std::unordered_map<std::string, const TypeNode*> typeAliases; // user typedef name → AST node
    std::map<int64_t, llvm::StructType*> strStructTypes;  // cap → { i64, [cap x i8] }

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
        return moduleName.empty() ? "" : toLower(moduleName) + PlangScopeSep;
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
        return requiredConsts.count(lowerName) != 0;
    }

    /// Records what a declared constant stands for.  A name declared here is
    /// the program's own from now on, whatever the language calls it.
    void defineConst(const std::string& name, llvm::Value* value) {
        const std::string lo = toLower(name);
        consts[lo] = value;
        requiredConsts.erase(lo);
    }

    const ImportOwnerTable* importOwners_{nullptr};
    /// EP §6.11: interfaces read from .pmi files; see Codegen::setLoadedInterfaces.
    std::vector<const ModuleNode*> loadedInterfaces_;
    /// The heading of the module being emitted, whose declarations are its
    /// block's; null while emitting anything else.
    const BlockNode* moduleIfaceBlock_{nullptr};

    /// What is known about \p name as an import of the unit being emitted, or
    /// null if this unit does not import it.
    const ImportedName* importedName(const std::string& name) const {
        if (!importOwners_) return nullptr;
        auto unit = importOwners_->find(currentUnit_);
        if (unit == importOwners_->end()) return nullptr;
        auto it = unit->second.find(toLower(name));
        return it == unit->second.end() ? nullptr : &it->second;
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
        if (const auto* imp = importedName(name)) return imp->Module;
        const auto dot = name.rfind('.');
        return dot == std::string::npos ? std::string()
                                        : toLower(name.substr(0, dot));
    }

    /// The name \p name is mangled under in the module that declares it.  EP
    /// §6.11.2 renaming lets a unit call an imported procedure something else
    /// entirely; the object file still knows only the original.
    std::string importLinkName(const std::string& name) const {
        if (const auto* imp = importedName(name))
            if (!imp->LinkName.empty()) return imp->LinkName;
        return stripQualifier(name);
    }

    /// True when \p name is an imported procedure or function, so a bare
    /// mention of it is a call even though nothing of that name has been
    /// emitted here — its module was compiled separately.
    bool isImportedCallable(const std::string& name) const {
        const auto* imp = importedName(name);
        return imp && imp->IsCallable;
    }

    // Static link for nested procedures (ISO §6.7.1).
    // curStaticLink: the %static_link parameter value in the current nested
    //   function, or null when at top level.
    // outerVarNames: ordered list of outer variable names for the current
    //   nested function's frame struct (set by emitFunctionDef, restored on exit).
    // funcOuterVarNames_: per-function record of outer variable name lists,
    //   keyed by LLVM mangled name.  Populated at definition time; consulted at
    //   call sites so every caller uses the same slot ordering.
    // nestedFunctions_: set of mangled names that require a static-link frame.
    //   Explicit flag replaces the fragile "declaredParamCount > astArgCount"
    //   arity heuristic which conflates frame params with arity mismatches.
    llvm::Value*                                     curStaticLink{nullptr};
    std::vector<std::string>                         outerVarNames;
    /// The outer variables this activation reaches through its own static
    /// link, by name, as they were when the prologue exposed them.
    ///
    /// They are also defVar'd, but a local of the same name replaces that
    /// binding outright -- so after `procedure c; var n: integer`, the address
    /// of the enclosing `n` was gone.  Building a frame for a sibling then
    /// found c's own n and the callee wrote into the wrong activation.
    std::map<std::pair<size_t, std::string>, VarEntry> outerVarBindings;
    /// Heap blocks a value conformant array parameter was copied into,
    /// freed where the body ends.  See the conformant branch of the
    /// parameter prologue.
    std::vector<llvm::Value*> valueConformantCopies_;
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

    // EP §6.7.3.7: conformant array parameter metadata.
    // Key: mangled function name.
    // Value: one entry per AST argument position.
    //   Each entry is a vector of (loVarName, hiVarName) pairs — one pair per
    //   conformant dimension.  An empty vector means the arg is not conformant.
    std::map<std::string,
             std::vector<std::vector<std::pair<std::string,std::string>>>>
        conformantParamDims_;

    // The ordinal bit 0 stands for in each value parameter of set type, by
    // mangled function name and AST argument position; 0 for every parameter
    // that is not a set, which is the window a non-negative base uses anyway.
    // A set argument is compatible with a parameter whose base begins
    // elsewhere, and has to be moved into the callee's window to arrive whole.
    std::map<std::string, std::vector<int64_t>> paramSetBases_;

    // EP §6.4.7: schema definitions reachable from the current block, so that
    // the body's bound expressions can be re-emitted with run-time
    // discriminants.  Key: lowercase schema name.
    struct SchemaDef {
        std::vector<std::string> discNames;
        const TypeNode*          body{nullptr};
    };
    std::map<std::string, SchemaDef> schemaDefs_;

    // EP §6.4.7: undiscriminated schema parameter metadata.
    // Key: mangled function name.  Value: discriminant count per AST argument
    // position; zero means the argument is not a schema parameter.
    std::map<std::string, std::vector<unsigned>> schemaParamDiscs_;

    // ISO §6.6.3.1: the declared signature at each AST argument position of a
    // function, or null where the argument is an ordinary one.  A call site
    // reads this to know it must pass the entry point and frame pair, and what
    // signature to build the thunk against.  Key: mangled function name.
    std::map<std::string, std::vector<const ProcedureTypeNode*>>
        procParamPositions_;

    // ISO §6.6.3.1: uniform-signature thunks, keyed by the callee they wrap
    // and the signature they present it through.  See procParamThunk.
    std::map<std::pair<llvm::Function*, llvm::FunctionType*>, llvm::Function*>
        procParamThunks_;

    // ISO §6.6.3.3 vs §6.6.3.2: whether each AST argument position of a
    // function is a variable parameter, and so takes the actual's address
    // rather than its value.  Key: mangled function name.
    //
    // A call site cannot read this off the LLVM signature, where both a var
    // parameter and a value parameter of pointer type are simply `ptr`.  Taken
    // for the former, `procedure one(p: ^integer)` was handed the address of
    // the caller's pointer variable, and `p^` read the variable rather than
    // what it pointed at.
    std::map<std::string, std::vector<bool>> paramByRef_;

    /// Is AST argument \p astArgIdx of \p mangledName a variable parameter?
    [[nodiscard]] bool paramIsByRef(const std::string& mangledName,
                                    size_t astArgIdx) const {
        auto it = paramByRef_.find(mangledName);
        return it != paramByRef_.end() && astArgIdx < it->second.size()
            && it->second[astArgIdx];
    }

    /// The procedural signature at AST argument \p astArgIdx, or null.
    [[nodiscard]] const ProcedureTypeNode*
    procParamArg(const std::string& mangledName, size_t astArgIdx) const {
        auto it = procParamPositions_.find(mangledName);
        if (it == procParamPositions_.end() || astArgIdx >= it->second.size())
            return nullptr;
        return it->second[astArgIdx];
    }

    /// Pushes an actual for a conformant array formal: the array, then a lo/hi
    /// pair per dimension (EP §6.7.3.7).
    void pushConformantArgs(std::vector<llvm::Value*>& args, const ExprNode& arg,
                            size_t dims);

    /// Discriminants a schema formal of this denoter takes, or 0 (EP §6.4.7).
    [[nodiscard]] unsigned schemaParamDiscCount(const TypeNode* t) const {
        if (!t || !t->ResolvedType || t->ResolvedType->Kind != TypeKind::Schema)
            return 0;
        return static_cast<unsigned>(t->ResolvedType->SchemaDiscs.size());
    }

    /// The { entry point, frame } cell a procedural parameter is held in.
    llvm::StructType* procPairTy() {
        return llvm::StructType::get(ctx, {ptrTy, ptrTy});
    }
    void storeProcPair(llvm::Value* cell, llvm::Value* fn, llvm::Value* frame);
    /// Reads a closure cell back as (entry point, frame).
    std::pair<llvm::Value*, llvm::Value*> loadProcPair(llvm::Value* cell);

    /// The LLVM signature a procedural parameter is called through.
    llvm::FunctionType* procParamFnType(const ProcedureTypeNode& node);

    /// A wrapper around \p target with that uniform signature.
    llvm::Function* procParamThunk(llvm::Function* target,
                                   const ProcedureTypeNode& node);

    /// Pushes the (entry point, frame) pair for the procedure named by \p arg.
    void pushProcParamArgs(std::vector<llvm::Value*>& args, const ExprNode& arg,
                           const ProcedureTypeNode& node);

    /// The static-link frame a direct call to \p mangledName would build, or
    /// null when it needs none.
    llvm::Value* buildStaticLinkFrame(const std::string& mangledName);

    /// Emits a call through procedural parameter \p ve.  Returns null for a
    /// procedural (void) target.
    llvm::Value* emitProcParamCall(const VarEntry& ve,
                                   std::span<const std::unique_ptr<ExprNode>> args);

    /// A schematic variable at run time: where its body lives and what
    /// discriminants it carries.  Produced by schemaRefOf.
    struct SchemaRef {
        const plang::Type*        semaTy{nullptr}; // TypeKind::Schema
        llvm::Value*              data{nullptr};   // start of the body storage
        std::vector<llvm::Value*> discs;           // one i64 per discriminant
    };

    /// Label name to the block it names, for the function being emitted.
    /// ISO §6.2.1 scopes a label to its block, so this is saved and cleared
    /// around each function: two procedures may both declare label 1, and
    /// sharing one entry would have the second branch into the first's body.
    std::map<std::string, llvm::BasicBlock*> labelBlocks;

    /// A block being emitted that declares labels, with the machinery a goto
    /// from an enclosed procedure needs to reach it (ISO §6.8.1).  Innermost
    /// last, so a goto finds the nearest activation that declares its label.
    ///
    /// Leaving a procedure means abandoning its frame and every frame under
    /// it, which a branch cannot express: the target is not in this function.
    /// So the owning block records where it was with setjmp, and the goto
    /// returns there with longjmp, naming the label it wants in the value the
    /// setjmp is seen to return.  A switch on that value does the rest.
    struct LabelOwner {
        const BlockNode*      block{nullptr};
        std::string           bufName;   ///< scope name of its jump buffer
        llvm::SwitchInst*     dispatch{nullptr};
    };
    std::vector<LabelOwner> labelOwners;

    /// The jump buffer is sized once, here, for every target: the generated
    /// code cannot see the platform's jmp_buf, and the runtime asserts that
    /// this is enough room for it.
    static constexpr unsigned gotoBufWords = 64;

    /// Does \p block's label section declare \p label?
    static bool declaresLabel(const BlockNode& block, const std::string& label);

    /// The labels \p block declares that a goto inside a procedure declared in
    /// it names.  These are the ones that need somewhere to land.
    static std::set<std::string> nonLocalTargets(const BlockNode& block);
    /// Recursive half of nonLocalTargets: scans the procedures declared in
    /// \p inner for gotos naming a label \p block declares, collecting into
    /// \p found.
    static void scanNonLocalTargets(const BlockNode& inner,
                                    const BlockNode& block,
                                    std::set<std::string>& found);

    /// The value a longjmp passes for \p label.  Offset by one because zero is
    /// what setjmp returns when it is first called, and longjmp turns a
    /// requested zero into one anyway.
    static int64_t gotoDispatchValue(const std::string& label);

    /// Record \p block as the owner of its labels, and, if a goto from inside
    /// one of its procedures names any of them, plant the landing pad.  \p buf
    /// is the jump buffer to record in — a global for the program's block,
    /// whose one activation lasts the run, and an alloca for a procedure's,
    /// which nested procedures reach over the static link.  Emits at the
    /// current insertion point, which must be past the block's initialization:
    /// a goto landing here resumes the block, it does not restart it.
    void openLabelScope(const BlockNode& block, bool programBlock);

    /// Plant the setjmp and the switch that dispatches on what it returns.
    /// Done for a procedure by openLabelScope; the program's block registers
    /// itself before its procedures are emitted and lands here later, once
    /// main exists to hold the setjmp and its variables are initialized.
    void emitLabelLanding();

    /// Point the landing pad at the label blocks the body has by now created.
    void closeLabelScope();

    void emitGoto(const GotoStmt& s);

    /// Keep \p f's local variables in memory, so that a goto landing in it
    /// finds what they hold rather than what the optimiser decided they must
    /// hold on the edge from the setjmp.  See closeLabelScope.
    void pinLocalsToMemory(llvm::Function* f);

    /// Whether range checking is on where \p Loc is.
    ///
    /// Not a flag on this object, because Turbo's `{$R+}` is positional: the
    /// same compilation checks one loop and not the next.  With no switch
    /// table -- which is every ISO 7185 and Extended Pascal compilation, since
    /// neither has directives -- this is the command-line default and nothing
    /// is searched, so the code emitted is what it was before there was a
    /// table.  See Basic/SwitchTable.h.
    [[nodiscard]] bool rangeChecksAt(SourceLocation Loc) const {
        return langOpts.switchOn(Switch::RangeChecks, Loc);
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
    void pushScope() { scopes.emplace_back(); shadowedConsts.emplace_back(); }
    void popScope()  {
        // Put back any constant a variable in this scope was shadowing.
        if (!shadowedConsts.empty() && shadowedConsts.size() == scopes.size()) {
            for (auto& [K, V] : shadowedConsts.back()) consts[K] = V;
            shadowedConsts.pop_back();
        }
        if (!scopes.empty()) scopes.pop_back();
    }

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
    void defVar(const std::string& name, llvm::Value* ptr, llvm::Type* type,
                const TypeNode* typeNode = nullptr);
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
        const std::string K = toLower(name);
        const size_t Start = curFuncScopeDepth ? curFuncScopeDepth - 1 : 0;
        for (size_t i = Start + 1; i-- > 0;) {
            const auto It = scopes[i].find(K);
            if (It != scopes[i].end()) return &It->second;
        }
        return nullptr;
    }

    [[nodiscard]] bool boundInsideFunction(const std::string& name) const {
        for (size_t i = scopes.size(); i-- > curFuncScopeDepth;)
            if (scopes[i].count(toLower(name))) return true;
        return false;
    }

    /// How many scopes were open when the current function body began.
    size_t curFuncScopeDepth{0};

    /// Bring a variable this unit imports into scope, and answer with it.
    /// \p semaTy describes it well enough to declare it when the module that
    /// owns it was compiled separately.  Null when there is no such variable.
    const VarEntry* resolveImportedVar(const std::string& name,
                                       const Type* semaTy);

    // ====================================================================
    // Type resolution
    // ====================================================================
    llvm::StructType* strStructType(int64_t cap);
    /// Returns null when the name is not a known type; callers retry via Sema.
    llvm::Type* llvmTypeOfName(const std::string& name);
    /// Lowers a type denoter from the type Sema resolved for it, reporting
    /// `what` as an internal error if Sema left it unresolved.
    llvm::Type* llvmTypeOfNodeViaSema(const TypeNode& node, const std::string& what);

    /// The first and last index of \p n, or nothing when neither the bounds nor
    /// Sema's resolved type can supply them.  An index written as an ordinal
    /// type — `array[color]` — has no bound expressions to fold, so the range
    /// comes from the type Sema resolved for the node.
    std::optional<std::pair<int64_t, int64_t>>
    arrayIndexRange(const ArrayTypeNode& n) const;

    /// The first index of \p n.  Subtracting it maps a Pascal index onto the
    /// zero-based LLVM array, so an array whose first index is not known is one
    /// every subscript of would land somewhere else: standing in a zero here
    /// reads `array [5..9]` as `array [0..4]` and indexes five places past
    /// where the element is.  Sema resolves every denoter it accepts, so
    /// arriving with neither a bound that folds nor a resolved type is an
    /// inconsistency and not a case to carry on from.
    int64_t arrayIndexLow(const ArrayTypeNode& n) const {
        auto R = arrayIndexRange(n);
        if (!R) codegenICE("array has no first index that either its bounds or "
                           "Sema can give");
        return R->first;
    }
    llvm::StructType* structTypeFor(const RecordTypeNode& rt);

    /// The layout of \p rt, building it if this is the first time it is asked
    /// for.  Never null.
    const RecordLayout& layoutOf(const RecordTypeNode& rt);

    /// The layout of the record \p T, which is \p T's declaration laid out
    /// under the discriminants \p T was resolved with.  Null when T is not a
    /// record or has no declaration to lay out.
    const RecordLayout* layoutOfRecord(const Type& T);

    /// Binds the discriminants \p T was resolved under for as long as it is
    /// alive, so that the bound expressions in its declaration — written in
    /// terms of those names — fold to the values this instance has, and so
    /// that the layout they produce is cached apart from the other instances'.
    class SchemaBindingScope {
    public:
        SchemaBindingScope(Impl& I, const Type& T);
        ~SchemaBindingScope();
        SchemaBindingScope(const SchemaBindingScope&)            = delete;
        SchemaBindingScope& operator=(const SchemaBindingScope&) = delete;

    private:
        Impl&       I;
        std::string SavedCtx;
        /// Each bound name with what it stood for before, if anything did.
        std::vector<std::pair<std::string, std::optional<llvm::Value*>>> Saved;
    };

    /// Place the alternatives of \p vp into \p elems and \p L.  The tag becomes
    /// an ordinary element — it is there whichever variant is active — and the
    /// alternatives share the one element appended after it.
    void layoutVariantPart(const VariantPart& vp, RecordLayout& L, bool packed,
                           std::vector<llvm::Type*>& elems);

    /// Place one alternative's fields from byte \p base of the variant element
    /// at \p blobIdx, recursing into a nested variant.  Returns the offset one
    /// past the last of them, and widens \p align to what they need.
    uint64_t layoutVariantCase(const VariantCase& vc, RecordLayout& L, bool packed,
                               unsigned blobIdx, uint64_t base, uint64_t& align);

    /// A type of at least \p size bytes and alignment \p align, to stand for
    /// the whole variant part.  A plain [n x i8] would be byte-aligned and
    /// leave every wider field inside it misaligned.
    llvm::Type* variantBlobType(uint64_t size, uint64_t align);

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
    llvm::Type* llvmTypeOf(const TypeNode* denoter, const Type* resolved);

    llvm::Type* llvmTypeOfNode(const TypeNode& node);
    /// Convert a semantic Type (from Sema) directly to an LLVM type.
    /// Used by schema instance handling where the AST TypeNode is parameterized.
    /// The integer type an ordinal denoter lowers to; see the definition.
    [[nodiscard]] llvm::Type* ordinalTyOf(const TypeNode& node);
    /// Whether llvmTypeOfSemaType has a lowering for \p T; see the definition.
    static bool canLowerSemaType(const Type& T);
    llvm::Type* llvmTypeOfSemaType(const Type& T);
    llvm::Type* llvmTypeOfSemaTypeImpl(const Type& T);
    /// Checks Sema's byteSizeOf against the layout; see the definition.
    void checkSizeAgreement(const Type& T, llvm::Type* Built);

    // ====================================================================
    // Alloca helpers
    // ====================================================================
    llvm::AllocaInst* createEntryAlloca(llvm::Type* ty, const std::string& name);

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
    /// runtime's PascalFile, which is declared once in
    /// plang/Basic/PascalFileLayout.h and read by both sides.
    ///
    /// Building it from a list is only half the job — the list has to be the
    /// same one the runtime compiled.  So every field's offset, and the whole
    /// size, are checked against that struct here, the first time the type is
    /// wanted.  A field added, widened or reordered on either side alone stops
    /// the compiler rather than leaving generated code reading a field at an
    /// offset nothing wrote it to.
    llvm::StructType* fileStructType() {
        // Cached on this Codegen and not in a static.  A static outlives the
        // LLVMContext that owns the type, so a second compilation in one
        // process -- which the binary never does and anything embedding the
        // front end does immediately -- got a type belonging to a context that
        // had been destroyed, and took a segmentation fault building a null
        // value of it.
        if (fileStructTy_) return fileStructTy_;
        llvm::StructType*& FST = fileStructTy_;

        FST = llvm::StructType::get(
            ctx, {
#define PLANG_FILE_FIELD_TYPE(Member, LLVMTy) LLVMTy,
                PLANG_FILE_FIELDS(PLANG_FILE_FIELD_TYPE)
#undef PLANG_FILE_FIELD_TYPE
            });

        const auto& dl     = mod->getDataLayout();
        const auto* layout = dl.getStructLayout(FST);
        if (FST->getNumElements() != PlangFileFieldCount)
            codegenICE("the file record has " + std::to_string(PlangFileFieldCount)
                       + " fields and codegen built "
                       + std::to_string(FST->getNumElements()));
        if (dl.getTypeAllocSize(FST) != sizeof(PascalFile))
            codegenICE("a file variable takes "
                       + std::to_string(dl.getTypeAllocSize(FST).getFixedValue())
                       + " bytes as codegen lays it out and "
                       + std::to_string(sizeof(PascalFile))
                       + " as the runtime declares it");
        unsigned idx = 0;
#define PLANG_FILE_FIELD_OFFSET(Member, LLVMTy)                                \
        if (layout->getElementOffset(idx) != offsetof(PascalFile, Member))     \
            codegenICE("the file record's '" #Member "' is at offset "         \
                       + std::to_string(layout->getElementOffset(idx))         \
                       + " as codegen lays it out and "                        \
                       + std::to_string(offsetof(PascalFile, Member))          \
                       + " as the runtime declares it");                       \
        ++idx;
        PLANG_FILE_FIELDS(PLANG_FILE_FIELD_OFFSET)
#undef PLANG_FILE_FIELD_OFFSET
        return FST;
    }

    /// ISO §6.5.5: the address of the buffer variable \p fileExpr ^, which the
    /// runtime keeps beside the stream.
    llvm::Value* fileBufferPtr(const ExprNode& fileExpr);

    // ---- set helpers (ISO §6.7.2.4) ----
    /// Sets are a flat bitmask of PlangMaxSetElements bits.  Bit 0 stands for
    /// the base type's origin rather than for ordinal 0, so a base type
    /// reaching below zero still fits; see setBaseOffset.  Sema rejects base
    /// types that span more ordinals than there are bits.
    llvm::IntegerType* setTy() { return llvm::Type::getIntNTy(ctx, PlangMaxSetElements); }
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
    /// alignSet for an argument being passed by value, whose destination window
    /// is the one recorded for the callee's parameter.  Leaves anything that is
    /// not a set value alone, a var parameter's address included.
    llvm::Value* alignSetArg(llvm::Value* v, const ExprNode& arg,
                             const std::string& mangled, size_t astArgIdx);
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
    llvm::StructType* complexTy() {
        if (!complexTy_) complexTy_ = llvm::StructType::get(ctx, {dblTy, dblTy});
        return complexTy_;
    }
    llvm::StructType* complexTy_{nullptr};
    /// The PascalFile struct, built and checked once per compilation; see
    /// fileStructType.
    llvm::StructType* fileStructTy_{nullptr};

    /// EP §6.4.3.4: { i8, i64, i64, i64, i8, i64, i64, i64 }
    /// DateValid, year, month, day, TimeValid, hour, minute, second.
    /// Matches the C layout of PlangTimeStamp in plang_time.cpp.
    llvm::StructType* timestampStructType() {
        if (!timestampTy_)
            timestampTy_ = llvm::StructType::get(ctx,
                {i8Ty, i64Ty, i64Ty, i64Ty, i8Ty, i64Ty, i64Ty, i64Ty});
        return timestampTy_;
    }
    llvm::StructType* timestampTy_{nullptr};

    /// EP §6.4.3.4: { string(PlangMaxBindingName) name, i8 bound } — both
    /// fields are required.  Matches PlangBindingType in plang_file.cpp.
    llvm::StructType* bindingStructType() {
        // Use the structTypes cache keyed by a unique string so the type
        // is stable across multiple llvmTypeOfName() calls within one module.
        auto it = structTypes.find("__binding__");
        if (it != structTypes.end()) return it->second;
        // 'bound' is i1 rather than i8 so that writing it selects the Boolean
        // formatter; an i1 still occupies one byte, matching the C struct.
        auto* st = llvm::StructType::get(ctx,
            llvm::ArrayRef<llvm::Type*>{strStructType(PlangMaxBindingName),
                                        llvm::Type::getInt1Ty(ctx)});
        structTypes["__binding__"] = st;
        return st;
    }

    /// Build a { double, double } aggregate from two double values.
    llvm::Value* makeComplex(llvm::Value* re, llvm::Value* im) {
        auto* v  = llvm::UndefValue::get(complexTy());
        auto* v1 = builder.CreateInsertValue(v,  re, 0, "cplx.re");
        return    builder.CreateInsertValue(v1, im, 1, "cplx.im");
    }

    /// Coerce a scalar or complex value to a { double, double } complex aggregate.
    /// If the value is already complexTy, it is returned as-is.
    /// Integer values are first widened to double.
    llvm::Value* coerceToComplex(llvm::Value* v) {
        if (!v) return llvm::ConstantAggregateZero::get(complexTy());
        if (v->getType() == complexTy()) return v;
        auto* re = toDouble(v);
        auto* im = llvm::ConstantFP::get(dblTy, 0.0);
        return makeComplex(re, im);
    }

    /// Inline complex addition.
    llvm::Value* emitComplexAdd(llvm::Value* a, llvm::Value* b);
    /// Inline complex subtraction.
    llvm::Value* emitComplexSub(llvm::Value* a, llvm::Value* b);
    /// Inline complex multiplication.
    llvm::Value* emitComplexMul(llvm::Value* a, llvm::Value* b);
    /// Inline complex division.
    llvm::Value* emitComplexDiv(llvm::Value* a, llvm::Value* b);
    /// Complex power via runtime plang_cpow_out.
    llvm::Value* emitComplexPow(llvm::Value* a, llvm::Value* b);
    /// Call a (re_out, im_out, re_in, im_in) runtime function and return complex.
    llvm::Value* callComplexUnary(const std::string& name, llvm::Value* z);
    static bool isTextTypeName(const TypeNode* tn);
    bool isFileVar(const ExprNode& e);
    llvm::Value* fileVarPtr(const ExprNode& e);
    /// Returns true if the expression is a typed binary file variable
    /// (file of T where T is not char/text).  Used to route binary I/O.
    /// The file type \p e denotes, looked up through a type name rather than
    /// read off the denoter, which may be one.  Null when \p e is not a file.
    const Type* fileTypeOf(const ExprNode& e);
    bool isTypedBinaryFileVar(const ExprNode& e);
    /// Returns the byte-size of one component of a typed file variable.
    /// Returns 1 as a fallback for untyped or unknown files.
    int64_t getFileElemSize(const ExprNode& fileExpr);
    /// The LLVM type of one component of \p fileExpr, or null for a text or
    /// untyped file.
    llvm::Type* getFileElemType(const ExprNode& fileExpr);

    /// EP §6.7.3.7: the address of an element of a conformant array parameter,
    /// or null if \p e does not subscript one.  Takes the whole subscript
    /// chain, because the dimensions can only be folded together once.
    llvm::Value* emitConformantElemPtr(const IndexExpr& e);
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

    /// True if the expression's resolved type is EP string(N).
    static bool exprIsVarStr(const ExprNode& e) {
        return e.ResolvedType && e.ResolvedType->Kind == TypeKind::VarString;
    }

    /// Capacity of the expression's VarString type; 0 if not VarString.
    static int64_t exprStrCap(const ExprNode& e) {
        return exprIsVarStr(e) ? e.ResolvedType->StrCapacity : 0;
    }

    /// The same capacity as a value.  EP §6.4.3.3 makes `string` a schema whose
    /// one discriminant is the capacity, so for a `^string` the answer is in the
    /// header new() wrote and is not known until run time; StrCapacity holds the
    /// probe's answer and would check `q^ := 'hi'` against a string(1).
    llvm::Value* exprStrCapV(const ExprNode& e);
    void setVarStrCap(const std::string& name, llvm::Value* cap);

    /// The capacity to SIZE A TEMPORARY with, which has to be a constant.  A
    /// discriminant-fixed capacity is not one, and the probe's answer would cut
    /// the temporary to a single character, so such a string gets the widest
    /// capacity plang has -- every real capacity fits in it.  Use exprStrCapV
    /// wherever the capacity is a value the runtime is told, not a size.
    static int64_t exprStrCapStatic(const ExprNode& e) {
        if (!exprIsVarStr(e)) return 0;
        return e.ResolvedType->ExtentVaries ? PlangMaxStringCapacity
                                            : e.ResolvedType->StrCapacity;
    }

    /// EP §6.4.7 run-time layout, for a schema body whose extent a discriminant
    /// fixes.  Call with the discriminants bound in the current scope (see
    /// bindSchemaDiscs): the bound and capacity expressions are re-emitted
    /// against them.  A subtree that reads no discriminant folds to a constant.
    uint64_t     rtAlignOfTypeNode(const TypeNode* tn);
    llvm::Value* rtSizeOfTypeNode(const TypeNode* tn);
    llvm::Value* rtFieldOffset(const RecordTypeNode& rt, const std::string& field);
    llvm::Value* rtWalkFields(const std::vector<FieldDecl>& fields,
                              llvm::Value* off, bool packed,
                              const std::string* stopAt, bool* found);
    llvm::Value* rtVariantSize(const VariantPart& vp, llvm::Value* off, bool packed);
    llvm::Value* rtVariantFieldOffset(const VariantPart& vp, llvm::Value* off,
                                      bool packed, const std::string& field);
    uint64_t     rtVariantAlign(const VariantPart& vp);

    /// A component of a run-time-laid-out object: the enclosing schema whose
    /// header carries the discriminants, the component's address, and the
    /// denoter its extents are written in.
    struct SchemaPath {
        SchemaRef       root;
        llvm::Value*    addr{nullptr};
        const TypeNode* decl{nullptr};
    };
    std::optional<SchemaPath> schemaPathOf(const ExprNode& e);
    const TypeNode* fieldDenoterOf(const RecordTypeNode& rt, const std::string& field);
    const TypeNode* variantFieldDenoterOf(const VariantPart& vp, const std::string& field);
    static bool isRuntimeLaidOut(const ExprNode& e);
    llvm::Value* alignUpV(llvm::Value* v, uint64_t align);
    void bindSchemaDiscs(const SchemaRef& ref);
    const ArrayTypeNode* varyingArrayFieldOf(const FieldExpr& fe);

    /// True if the expression is an ISO §6.4.3.2 string-type: a
    /// packed array[1..n] of char, which is n bytes with no length and no
    /// terminator, quite unlike either of the other two string shapes.
    static bool exprIsCharStr(const ExprNode& e) {
        return e.ResolvedType && isCharStringType(*e.ResolvedType);
    }

    /// True if the expression is a character string in any of the three shapes
    /// one can take: a literal, ISO §6.4.3.2's packed array[1..n] of char, or
    /// EP's string(n).  A literal is the one shape that carries no array type,
    /// so asking only after the other two left `'farka' <= 'farkz'` to the
    /// ordinary operators, which compared the addresses of the two constants.
    static bool exprIsStringLike(const ExprNode& e) {
        return exprIsVarStr(e) || exprIsCharStr(e)
            || (e.ResolvedType && e.ResolvedType->Kind == TypeKind::String);
    }

    /// The n of the expression's string-type, or 0.
    static int64_t exprCharStrLen(const ExprNode& e) {
        return exprIsCharStr(e) ? charStringLength(*e.ResolvedType) : 0;
    }

    /// A string-type value as a temporary string(n), so that the runtime that
    /// already writes and compares strings can be used on it unchanged.  The
    /// length is fixed at n: every character of the array is part of the value.
    llvm::Value* emitCharStrAsStr(const ExprNode& e);

    /// Store a string value into a string-type variable of length \p n at
    /// \p dst — exactly n bytes, with no length field to update.
    void emitCharStrStore(llvm::Value* dst, int64_t n, const ExprNode& src);

    /// True if the expression's resolved type is a set.
    static bool exprIsSet(const ExprNode& e) {
        return e.ResolvedType && e.ResolvedType->Kind == TypeKind::Set;
    }

    /// Resolve the LLVM mangled name for a Pascal procedure/function call.
    /// Walks outward through the nesting hierarchy so that a call to 'inner'
    /// from inside 'outer' finds 'plang_outer__inner', while a call to a
    /// top-level 'helper' from inside 'outer' falls back to 'plang_helper'.
    /// Drops the module qualifier from an EP §6.11.2 qualified name, leaving
    /// the identifier.  The module it names is recovered separately, by
    /// importOwner, because it is part of the mangled name.
    static std::string stripQualifier(const std::string& name) {
        const auto dot = name.rfind('.');
        return (dot == std::string::npos) ? name : name.substr(dot + 1);
    }

    std::string findMangledProc(const std::string& qualifiedName) const {
        const std::string      name = stripQualifier(qualifiedName);
        const std::size_t      root = std::string_view(PlangProcPrefix).size();
        const std::string_view sep(PlangScopeSep);
        std::string prefix = namePrefix;
        while (true) {
            std::string candidate = prefix + name;
            if (mod->getFunction(candidate)) return candidate;
            // Strip the innermost enclosing scope: "pas_outer$inner$" →
            // "pas_outer$".  prefix ends with the separator, so the search
            // starts before it, at the last character of the scope name.
            if (prefix.size() <= root) break; // the bare prefix is the last try
            auto pos = prefix.rfind(sep, prefix.size() - sep.size() - 1);
            if (pos == std::string::npos || pos + sep.size() < root) break;
            prefix = prefix.substr(0, pos + sep.size());
        }
        // Nothing of that name is in scope here, so it is imported.  The walk
        // above runs first so a procedure this unit declares itself still wins
        // over one of the same name that it imports.
        return PlangProcPrefix + moduleScope(importOwner(qualifiedName))
             + importLinkName(qualifiedName);
    }

    /// The symbol naming the global variable \p name denotes, mangled with the
    /// module that declares it.
    std::string mangledGlobal(const std::string& qualifiedName) const {
        const std::string name = stripQualifier(qualifiedName);
        if (mod->getGlobalVariable(globalPrefix + name))
            return globalPrefix + name;
        return PlangGlobalPrefix + moduleScope(importOwner(qualifiedName))
             + importLinkName(qualifiedName);
    }

    /// The value of \p e in memory, for reading a component of a function
    /// result, which is a value with nowhere of its own to live.
    llvm::Value* spillToTemporary(const ExprNode& e);

    llvm::Function* getStrFn(const std::string& name, llvm::Type* retTy,
                              std::initializer_list<llvm::Type*> argTys);
    llvm::Value* strLoadLen(llvm::Value* strPtr);
    llvm::Value* strDataPtr(llvm::Value* strPtr);
    // EP §6.4.7: a capacity fixed by a schema discriminant is not a literal, so
    // these take it as a value.  The int64_t overloads wrap a constant and are
    // what every fixed-capacity caller still uses, so their IR is unchanged.
    void emitStrAssign(llvm::Value* dst, llvm::Value* capDst,
                       llvm::Value* src, llvm::Value* capSrc);
    void emitStrAssign(llvm::Value* dst, int64_t capDst,
                       llvm::Value* src, int64_t capSrc) {
        emitStrAssign(dst, i64c(capDst), src, i64c(capSrc));
    }
    void emitReadArg(const ExprNode& arg, llvm::Value* fp);
    void emitSkipLine(llvm::Value* fp);

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
    // EP §6.4.7: undiscriminated schema types (CodegenSchema.cpp)
    // ====================================================================
    /// Records the schemas declared in `block` so their bodies can be
    /// re-emitted with run-time discriminants.
    void registerSchemaDefs(const BlockNode& block);
    const SchemaDef* findSchemaDef(const std::string& name) const;
    /// The run-time view of `e`, or nullopt when `e` is not schematic.
    /// May emit loads, so call it once per use.
    std::optional<SchemaRef> schemaRefOf(const ExprNode& e);
    /// Body pointer and discriminants to pass for a schema formal parameter.
    std::pair<llvm::Value*, std::vector<llvm::Value*>>
        schemaActual(const ExprNode& arg, unsigned discCount);
    /// Discriminant count for one argument position of `mangledName`;
    /// zero when that parameter is not an undiscriminated schema.
    unsigned schemaArgDiscs(const std::string& mangledName, size_t astArgIdx) const;
    void pushSchemaArgs(std::vector<llvm::Value*>& args, const ExprNode& arg,
                        unsigned discCount);
    /// Traps unless the two schematic values carry the same discriminants,
    /// which EP §6.7.3.2 requires for them to be assignment-compatible.
    void emitSchemaDiscMatch(const SchemaRef& dst, const SchemaRef& src);
    /// Bounds of an array-bodied schema, computed from `ref`'s discriminants.
    std::pair<llvm::Value*, llvm::Value*> schemaArrayBounds(const SchemaRef& ref);
    /// LLVM type of the schema body's storage: the element type for an array
    /// body, the whole body otherwise.
    llvm::Type* schemaStorageType(const SchemaRef& ref);
    /// Size in bytes of one schematic value with the given discriminants.
    llvm::Value* schemaBodySize(const plang::Type& schema,
                                const std::vector<llvm::Value*>& discs);
    /// EP §6.7.5.3: new(p, d1..ds) for a pointer whose domain is a schema.
    void emitNewSchema(const ExprNode& ptrArg, const plang::Type& schema,
                       std::span<const std::unique_ptr<ExprNode>> discArgs);
    void emitStrFromCStr(llvm::Value* dst, llvm::Value* cap, llvm::Value* cstr);
    void emitStrFromCStr(llvm::Value* dst, int64_t cap, llvm::Value* cstr) {
        emitStrFromCStr(dst, i64c(cap), cstr);
    }
    void emitStrFromChar(llvm::Value* dst, llvm::Value* cap, llvm::Value* c);
    void emitStrFromChar(llvm::Value* dst, int64_t cap, llvm::Value* c) {
        emitStrFromChar(dst, i64c(cap), c);
    }
    /// Store \p src into the string variable at \p dst, whose capacity is
    /// \p capDst.  A string is a length and a buffer, so which runtime call
    /// this takes depends on what the source is; assignment and the 'value'
    /// initializer both come through here.
    void emitStrStore(llvm::Value* dst, llvm::Value* capDst, const ExprNode& src);
    void emitStrStore(llvm::Value* dst, int64_t capDst, const ExprNode& src) {
        emitStrStore(dst, i64c(capDst), src);
    }

    /// The address of the { length, bytes } struct a string expression denotes,
    /// which is what every string runtime entry point takes.  Whether that is
    /// the expression's own storage or a temporary depends on the expression,
    /// and getting it wrong is the difference between a component and the whole
    /// structure it sits in.
    llvm::Value* emitStrAddr(const ExprNode& e);
    /// One argument of a call to a user-declared procedure or function, given
    /// the LLVM type the callee declared for that position: an address for a
    /// var parameter, a copy for a string, the value otherwise.  \p byRef says
    /// the formal is a variable parameter, which the LLVM type cannot: a value
    /// parameter of pointer type is declared `ptr` there as well.
    llvm::Value* emitCallArg(const ExprNode& arg, llvm::Type* paramTy,
                             bool byRef);

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
    bool hasInitialState(const TypeNode* tn, int depth = 0) const;
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
    void emitStmt(const StmtNode* stmt);
    llvm::BasicBlock* getOrCreateLabel(const std::string& name);
    void resumeAfterTerminator();
    void emitCompound(const CompoundStmt& s);
    void emitAssign(const AssignStmt& s);
    void emitIf(const IfStmt& s);
    void emitWhile(const WhileStmt& s);
    void emitFor(const ForStmt& s);
    void emitForIn(const ForInStmt& s);
    void emitPackUnpack(const CallStmt& s, bool isPack);
    void emitRepeat(const RepeatStmt& s);
    void emitCase(const CaseStmt& s);
    void emitWith(const WithStmt& s);
    void emitCallStmt(const CallStmt& s);
    /// The tail of emitCallStmt: a call to a procedure the program declared,
    /// reached either by falling past the required ones or, when the name is
    /// one of theirs, directly.  See CallStmt::ResolvedBuiltin.
    void emitUserProcCall(const CallStmt& s);

    // ====================================================================
    // Built-in write / writeln / read
    // ====================================================================
    void emitBuiltinWrite(const std::vector<std::unique_ptr<ExprNode>>& args, bool newline);
    void emitWriteArgs(const std::vector<std::unique_ptr<ExprNode>>& args, size_t start,
                       bool newline, llvm::Value* fp, bool binaryTyped);
    void emitBuiltinWriteStr(const std::vector<std::unique_ptr<ExprNode>>& args);
    void emitBuiltinReadStr(const std::vector<std::unique_ptr<ExprNode>>& args);
    /// Whether a value should be written as 'true'/'false'.  A boolean is
    /// normally i1, but the predefined TimeStamp holds its two flags as i8 so
    /// that the record matches its C counterpart byte for byte, and at that
    /// width nothing in the IR distinguishes a boolean from a char — only the
    /// Pascal type does.
    static bool writesAsBoolean(const llvm::Type* ty, const plang::Type* semaTy) {
        if (ty->isIntegerTy(1)) return true;
        return ty->isIntegerTy(8) && semaTy && semaTy->Kind == TypeKind::Boolean;
    }
    /// Whether a value should be written as a character.  A char is normally
    /// i8, but a subrange of char is held in an integer-width slot, and at
    /// that width only the Pascal type says it is not a number.
    static bool writesAsChar(const llvm::Type* ty, const plang::Type* semaTy) {
        if (ty->isIntegerTy(8)) return true;
        if (!ty->isIntegerTy() || !semaTy) return false;
        const plang::Type* t = semaTy;
        while (t->Kind == TypeKind::Subrange && t->SubBase) t = t->SubBase.get();
        return t->Kind == TypeKind::Char;
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
    void emitWriteValue(llvm::Value* val, bool newline, llvm::Value* fp = nullptr,
                        const plang::Type* semaTy = nullptr);
    void emitWriteValueFormatted(llvm::Value* val, llvm::Value* w, llvm::Value* d,
                                  bool newline, llvm::Value* fp,
                                  const plang::Type* semaTy = nullptr);
    static std::string readFnSuffix(llvm::Type* ty);
    void emitBuiltinRead(const std::vector<std::unique_ptr<ExprNode>>& args);
    void emitBuiltinReadln(const std::vector<std::unique_ptr<ExprNode>>& args);

    // ====================================================================
    // Expression emission
    // ====================================================================
    llvm::Value* emitExpr(const ExprNode& e);
    llvm::Value* emitLValue(const ExprNode& e);
    llvm::Value* emitLValueOpt(const ExprNode& e) { return emitLValue(e); }
    llvm::Value* emitBinary(const BinaryExpr& e);
    llvm::Value* emitUnary(const UnaryExpr& e);
    llvm::Value* emitCallExpr(const CallExpr& e);
    /// The tail of emitCallExpr: a functional parameter, or a call to a
    /// function the program declared.  See CallExpr::ResolvedBuiltin.
    llvm::Value* emitUserFuncCall(const CallExpr& e);
    llvm::Value* emitIndexGEP(const IndexExpr& e);
    llvm::Value* emitIndexLoad(const IndexExpr& e);
    llvm::StructType* resolveRecordStructType(const FieldExpr& e);
    /// The type of the field a field expression selects, which for a variant
    /// field is not the type of the struct element it shares with the others.
    llvm::Type* fieldLlvmType(const FieldExpr& e);
    llvm::Value* emitFieldGEP(const FieldExpr& e);
    llvm::Value* emitFieldLoad(const FieldExpr& e);
    llvm::Value* emitDerefLoad(const DerefExpr& e);
    /// EP §6.8.7: emit a typed value constructor (array/record/set).
    /// For set constructors returns an i64 bitmask.
    /// For array/record constructors returns a ptr to a temporary alloca.
    /// EP §6.8.7: a value constructor.  `denoter` gives the shape for a
    /// component-value, which names no type of its own.
    llvm::Value* emitStructuredValue(const StructuredValueExpr& e,
                                     const TypeNode* denoter = nullptr);
    const TypeNode* denoterOf(const TypeNode* tn) const;
    static const TypeNode* fieldDenoter(const RecordTypeNode& rtn,
                                        std::string_view name);
    llvm::Value* ensureI1(llvm::Value* v);
    llvm::Value* toDouble(llvm::Value* v);
    llvm::Value* toI64(llvm::Value* v);
    llvm::Value* coerceToType(llvm::Value* v, llvm::Type* dst);
};
