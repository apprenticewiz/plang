#pragma once

#include "plang/AST/Ast.h"
#include "plang/AST/TypeContext.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Basic/ModuleImports.h"
#include "plang/Sema/SymbolTable.h"
#include "plang/Sema/Type.h"

#include "llvm/ADT/STLFunctionalExtras.h"

#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace plang {

// ---------------------------------------------------------------------------
// Sema
// ---------------------------------------------------------------------------

/// Walks the AST without modifying it, builds a scoped symbol table, resolves
/// types, and collects Diagnostic objects for every semantic error or warning.
/// Never throws; all issues go into the diagnostics list.
class Sema {
public:
    /// All diagnostics (errors and warnings) are appended to the shared vector.
    /// The same vector should be passed to the Scanner and Parser so that the
    /// entire pipeline uses one ordered diagnostic stream.
    explicit Sema(DiagnosticsEngine& Diags, LangOptions Opts = {});

    /// Analyze the entire program.  Returns true iff no errors were collected.
    [[nodiscard]] bool check(const ProgramNode& Prog);

    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const {
        return Diags.diagnostics();
    }
    [[nodiscard]] bool hasErrors() const;

    /// EP §6.11: what each unit's import clauses brought into it, for codegen.
    /// See ImportedName for why codegen cannot derive this itself.
    [[nodiscard]] const ImportOwnerTable& importOwners() const {
        return ImportOwners_;
    }

    /// EP §6.11: the interfaces read from .pmi files, which declare types this
    /// unit lays out and initialises but does not itself declare.  They live
    /// as long as this Sema does.
    [[nodiscard]] std::vector<const ModuleNode*> loadedInterfaces() const;

    /// How many bytes a variable of \p T occupies, or nothing where only
    /// codegen can say.
    ///
    /// Sema needs a number because Turbo writes `const BufSize = 4 *
    /// SizeOf(Integer)` and a constant has to fold, and Sema has no
    /// DataLayout to ask.  So this is a second opinion about storage, and a
    /// second opinion is exactly what goes wrong quietly -- a SizeOf that
    /// disagrees with the layout sizes a GetMem or a BlockRead buffer wrong,
    /// and the corruption surfaces nowhere near here.
    ///
    /// What keeps the two together is that codegen asserts this against
    /// `DataLayout::getTypeAllocSize` of the type it actually built, for every
    /// type it lowers.  A disagreement is an ICE at compile time rather than a
    /// buffer overrun at run time.  See checkSizeAgreement.
    ///
    /// Nothing is returned for a conformant array or an undiscriminated
    /// schema, whose extent is not known until they are passed or allocated.
    /// Byte offsets of a record's FIXED fields (and its tag), in declaration
    /// order.  R4: these come out of the same walk that computes the size, so
    /// there is no second implementation of the layout to disagree with.
    using FieldOffsets = std::vector<std::pair<std::string, uint64_t>>;
    [[nodiscard]] static std::optional<uint64_t> byteSizeOf(
        const Type& T, FieldOffsets* Offsets = nullptr);

    /// What \p T must be aligned to.  Natural alignment, which is what plang
    /// already emits and what FPC uses by default; see byteSizeOf.
    [[nodiscard]] static uint64_t byteAlignOf(const Type& T);

private:
    /// How far one alternative of a variant part reaches; see the definition.
    static uint64_t layoutVariantCase(const VariantCase& VC, bool Packed,
                                      uint64_t Base, uint64_t& Align, bool& Ok,
                                      FieldOffsets* Offsets = nullptr);
public:

private:
    // ---- state ----
    LangOptions              Opts;   // dialect and warning options (owned copy)
    SymbolTable              Symtab;
    DiagnosticsEngine&       Diags; // shared with Scanner and Parser

    // Live activations of checkExpr.  Every recursive re-entry into expression
    // checking -- a binary/unary operand, a call argument, an index/field/
    // deref base -- funnels through checkExpr, so bounding activations there
    // (see ExprDepthScope and MaxExprDepth below) bounds the whole
    // AST-walking recursion against a flat operator chain built specifically
    // to be deep, e.g. `1+1+1+...+1` with tens of thousands of terms.  Unlike
    // deeply NESTED parenthesized input, which the parser's own ExprDepth
    // guard (Parser.h) already catches, precedence-climbing parses a flat
    // chain iteratively, so its AST can be arbitrarily deep with no parser-
    // level ceiling on it -- Sema is the first place that walks it
    // recursively, and without a ceiling here that walk used to exhaust the
    // real C++ stack instead of failing with a diagnostic. Same shape as
    // Parser::ExprDepth; see its comment for the rationale.
    //
    // constBound and buildExtentForm (SemaType.cpp) recurse over the same
    // kind of AST -- a bound written as a flat chain, e.g. an array's
    // `array[1..1+1+...+1]` -- by a different route (constBound/
    // constBoundImpl call each other; buildExtentForm calls itself) that
    // checkExpr's own guard does not cover: checkExpr is run on a bound
    // first and stops safely at MaxExprDepth, but foldBounds then walks the
    // SAME chain again through constBound with nothing stopping it.  Sharing
    // this counter and ceiling, rather than each giving itself an
    // independent one, is what "share the same depth budget as checkExpr"
    // (issue #204) means: whichever of the two is active, recursing through
    // either one now counts against the one ceiling.  const because
    // constBound/constBoundImpl/buildExtentForm are; SchemaBindingUsed_
    // below is the existing precedent for a mutable scratch counter in an
    // otherwise-const fold.
    mutable unsigned          ExprDepth{};
    mutable bool              ExprDepthLimitHit{};
    // 1000 levels of recursion through checkExpr -> checkBinary/checkUnary/
    // ... -> checkExpr, or through constBound -> constBoundImpl ->
    // constBound -> ..., is well under the crash threshold observed
    // empirically on this build's default 8MB stack (a flat chain starts
    // crashing a few thousand terms in), while no legitimate Pascal
    // expression -- handwritten or reasonably generated -- nests anywhere
    // close to this deep.
    static constexpr unsigned MaxExprDepth = 1000;
    struct ExprDepthScope {
        unsigned& N;
        bool&     LimitHit;
        explicit ExprDepthScope(unsigned& Counter, bool& LimitHitFlag)
            : N(Counter), LimitHit(LimitHitFlag) { ++N; }
        ~ExprDepthScope() { if (--N == 0) LimitHit = false; }
    };

    /// Canonical type store — owns built-in singletons and interns structural
    /// types.  Built from Opts, which is why Opts is declared above it: what an
    /// unqualified `integer` is depends on the dialect, and what a pointer is
    /// (Type::Width, for Pointer/Nil/String -- see TypeContext's constructor)
    /// depends on --target.
    TypeContext Ctx_{Opts.defaultIntWidth(), Opts.PointerWidthBits, Opts.turbo()};

    // Convenience aliases that forward to TypeContext singletons.
    // Kept for backward compat with existing Sema implementation code.
    const std::shared_ptr<Type>& TyInt     = Ctx_.getInteger();
    const std::shared_ptr<Type>& TyReal    = Ctx_.getReal();
    const std::shared_ptr<Type>& TyComplex = Ctx_.getComplex();
    const std::shared_ptr<Type>& TyBool    = Ctx_.getBoolean();
    const std::shared_ptr<Type>& TyChar    = Ctx_.getChar();
    const std::shared_ptr<Type>& TyStr     = Ctx_.getString();
    const std::shared_ptr<Type>& TyNil     = Ctx_.getNil();
    const std::shared_ptr<Type>& TyErr     = Ctx_.getError();
    // -std=turbo only: PChar/PAnsiChar name this same singleton; see
    // TypeContext::getPChar's comment for why it is not getPointer(TyChar).
    const std::shared_ptr<Type>& TyPChar   = Ctx_.getPChar();

    // EP §6.4.3.4: BindingType — populated by registerBuiltins() in EP mode.
    std::shared_ptr<Type> TyBindingType;

    /// EP §6.4.1: whether a type denoter declares bindable variables, either
    /// by carrying the prefix itself or by naming a type that does.
    [[nodiscard]] bool isBindableDenoter(const TypeNode& Node);

    /// EP §6.7.5.6, §6.7.6.8: check a call to bind, unbind or binding.
    void checkBindingCall(const std::string& LowerName, SourceLocation Loc,
                          const std::vector<std::unique_ptr<ExprNode>>& Args);

    // --- EP §6.11: module export table ---
    // Maps lowercase module name to the symbols exported by that module.
    // Populated when processing module interfaces/bodies; consumed by import.
    std::map<std::string, std::vector<Symbol>> ModuleExports_;
    /// EP §6.11.1: everything a module's interface declares, exported or not,
    /// which its own block is given so that it need not be written twice.
    std::map<std::string, std::vector<Symbol>> ModuleInterfaceDecls_;

    /// EP §6.11.3: the import-part an interface wrote for itself, kept so that
    /// re-checking a heading the interface declared — most notably a module
    /// implementation's abbreviated repeat of a routine heading — can see the
    /// same names the interface resolved that heading against, whether or not
    /// the implementation repeats the import itself.
    std::map<std::string, std::vector<ImportClause>> ModuleInterfaceImports_;

    /// EP §6.11.2: the export-list of a module, keyed by lowercased module
    /// name.  A module with an entry here exports exactly what the list names,
    /// under the names the list gives them; one without exports everything it
    /// declares, which is what a module written with no interface does.
    std::map<std::string, std::vector<ExportItem>> ExportLists_;

    /// See importOwners().  Filled in by processImports, which is the only
    /// place that knows both the importing unit and the exporting module.
    ImportOwnerTable ImportOwners_;

    /// The unit whose imports are being processed: a lowercased module name, or
    /// empty for the program.
    std::string CurrentUnit_;
    /// EP §6.8.7.1: the type a component-value written without a type name is
    /// to have — the type of the denoter or component it is standing for.
    /// Null everywhere a value has to name its own type.
    std::shared_ptr<Type> ExpectedValueType_;
    /// True while checking a module block for which an interface of the same
    /// name was read, whose declarations the block is being given.
    bool InModuleImplementation_{false};

    // --- EP §6.11: module processing helpers ---
    void processModuleInterface(const ModuleNode& Mod);
    void processModuleBody     (const ModuleNode& Mod);
    void processImports        (const std::vector<ImportClause>& Imports);
    // Copies what a module declares into ModuleExports_.  Must run while the
    // module block's scope is still on the stack, which is what the BeforePop
    // hook on checkBlock is for.
    void harvestModuleExports  (const ModuleNode& Mod);

    // --- EP §6.11: separate compilation (.pmi file loading) ---

    /// What happened trying to load one candidate .pmi path.  Unreadable
    /// behaves like "this candidate doesn't exist" -- normal control flow
    /// while more search-path entries remain, not an error -- so it carries
    /// no detail.  ParseFailed and WrongModule are real problems with a file
    /// that DOES exist, and the caller decides what to report once every
    /// candidate has been tried, not loadPMI itself: reporting from inside
    /// here would stop the search on the first existing-but-broken file
    /// instead of trying the rest of the search path.
    struct PMILoadResult {
        enum class Status { Ok, Unreadable, ParseFailed, WrongModule } St;
        std::string Detail; ///< location-free text; empty for Ok/Unreadable
    };

    // Load a .pmi interface file for module \p Key (lowercase) from \p Path.
    // Parses the PMI content, resolves types, and populates ModuleExports_[Key].
    PMILoadResult loadPMI(const std::string& Key, const std::string& Path);

    /// The syntax trees of the interfaces loaded from .pmi files.  Types
    /// resolved from them keep pointers into the declarations they came from,
    /// which codegen follows, so they live as long as this Sema does.
    std::vector<std::unique_ptr<ProgramNode>> LoadedInterfaces_;

    // The innermost procedure/function currently being analyzed.
    const ProcDecl*       CurrentProc{nullptr};
    // Resolved return type of CurrentProc (null when not inside a function).
    std::shared_ptr<Type> CurrentRetType;

    /// Lowercased names checkProcBody just defined into CurrentProc's own
    /// parameter scope -- its formal parameters, EP named result variable, and
    /// any conformant-array bound names -- while that scope is the immediately
    /// enclosing one for the checkBlock call about to check its body.
    ///
    /// ISO §6.2.2 treats a procedure or function's formal-parameter-list and
    /// its block as ONE region, so redeclaring a parameter's name as a local
    /// constant, type, variable or nested procedure must be a duplicate-
    /// declaration error. checkProcBody and checkBlock push two separate
    /// SymbolTable scopes for the two halves of that one region (see their
    /// comments), so Symtab.define's own per-scope duplicate check cannot see
    /// the collision -- it looks only at the block's own (innermost) scope,
    /// one level in from where the parameters live. checkBlock cross-checks
    /// its declared names against this set instead. Empty outside a
    /// procedure/function body: a program or module block has no enclosing
    /// parameter scope of its own to collide with.
    std::set<std::string> EnclosingParamNames_;

    /// Every function whose block contains the statement being checked,
    /// outermost first.
    ///
    /// ISO §6.8.2.2 asks only that "the function-block associated with the
    /// function-identifier of an assignment-statement shall contain the
    /// assignment-statement" — contain, not be — so a function nested inside
    /// another may assign the outer one's result, and the whole chain has to be
    /// searchable.  Each frame carries its own record of whether the result was
    /// assigned, since the assignment that satisfies a function may be written
    /// in a function nested within it.
    struct FuncFrame {
        const ProcDecl*       Decl{nullptr};
        std::shared_ptr<Type> RetType;
        bool                  HasResult{false};
    };
    std::vector<FuncFrame> FuncStack;

    /// The frame of the function whose result \p Name denotes, innermost first,
    /// or null when it names no result in scope.
    [[nodiscard]] FuncFrame*       resultFrameFor(const std::string& Name);
    [[nodiscard]] const FuncFrame* resultFrameFor(const std::string& Name) const;

    /// -std=turbo only (see checkIdent's own comment): the exact IdentExpr
    /// node that is the ROOT of the assignment-statement currently being
    /// checked's own target -- 'result' in 'result := x' and in
    /// 'result.f := x' alike, found by checkAssign walking through any
    /// IndexExpr/FieldExpr wrapper the same way its HasResult marking
    /// already does.  Compared by POINTER identity, not by name: a name
    /// match alone cannot tell 'result.x := 1' (the target) from
    /// 'y := result.x' (an ordinary read of the SAME name, on the same
    /// statement's RHS) apart, and only the target keeps the
    /// assignment-statement's own ResultVariable meaning under Turbo.
    /// Null outside of checking an assignment-statement's target.
    const IdentExpr* CurAssignTargetRoot_{nullptr};

    // --- EP §6.4.7: active schema discriminant bindings ---
    // Populated during schema body resolution; consulted by constBound() and
    // checkIdent() so that discriminant names resolve to their integer values.
    std::unordered_map<std::string, int64_t> ActiveSchemaBindings_;

    // --- EP §6.4.7: where a bare schema-name denotes a type ---
    // A schema-name is a type-denoter only as a parameter-form (§6.7.3.1) and
    // as the domain-type of a pointer (§6.7.5.3).  Elsewhere the discriminants
    // are required, so resolveNamed rejects it.  Nonzero inside those two
    // contexts; managed by AllowSchemaScope.
    int AllowUndiscriminatedSchema_{0};
    /// Depth of pointer domain-types being resolved.  EP §6.4.3.3's `string`
    /// schema is denoted by a bare `string` HERE and not in a parameter's
    /// type, where resolveParamType already gives it the largest capacity so
    /// that an actual of any capacity is accepted.
    int InPointerDomain_{0};

    /// Schema resolutions currently on the stack, by body node and
    /// discriminants.  A schema whose body names itself resolves its own body
    /// while resolving it; the partly-built type is registered here first so
    /// the re-entry finds it instead of recursing.  It is completed in place,
    /// so a pointer that took it while incomplete ends up pointing at the
    /// finished type.
    std::map<std::string, std::shared_ptr<Type>> SchemaInProgress_;
    /// True only while a schema body is being resolved against the PROBE
    /// binding, i.e. for a schema used without its discriminants.  An ordinary
    /// discriminated instantiation `t(300)` fills ActiveSchemaBindings_ too, and
    /// its values are exact -- marking those extents as varying threw away every
    /// compile-time check on them and truncated a string(300) to 255.
    bool ProbeBindingsActive_{false};
    /// R3: the schema's discriminant names in declaration order, so that an
    /// extent form can name them by INDEX.  Set only while a schema body is
    /// being resolved against the probe.
    std::vector<std::string> ProbeDiscNames_;

    /// Holds a depth counter at zero for the extent of a scope, for a position
    /// that is inside a pointer domain-type syntactically but is not one.
    struct ClearSchemaScope {
        explicit ClearSchemaScope(int& C) : C_(C), Saved_(C) { C = 0; }
        ~ClearSchemaScope() { C_ = Saved_; }
        ClearSchemaScope(const ClearSchemaScope&)            = delete;
        ClearSchemaScope& operator=(const ClearSchemaScope&) = delete;
    private:
        int& C_;
        int  Saved_;
    };

    /// Scoped enable for undiscriminated schema-names as type-denoters.
    struct AllowSchemaScope {
        int& N;
        explicit AllowSchemaScope(int& Counter) : N(Counter) { ++N; }
        ~AllowSchemaScope() { --N; }
    };

    // Set by constBound whenever it reads ActiveSchemaBindings_.  Resolving a
    // schema body with probe discriminants uses this to learn whether the
    // body's extent depends on them.
    mutable bool SchemaBindingUsed_{false};

    // Set by constBoundImpl whenever an application of checkedAdd/Sub/Mul/
    // Neg/isoPow declined SPECIFICALLY because the expression's own
    // (possibly Turbo-narrow) resolved width rejected a result that the
    // natural 64-bit width -- what ISO 7185/EP's one Integer type always is
    // -- would have accepted.  Saved/reset/restored around a top-level
    // constBound call the exact same way SchemaBindingUsed_ is: a caller
    // with no bound-specific "not a constant expression" diagnostic of its
    // own (defineConst, Sema.cpp) reads this to tell that apart from a
    // genuinely non-constant expression, which must stay a silent decline
    // exactly as it always has (issue #202) -- only the width-narrowing
    // decline is new, and only Turbo's narrower Integer kinds can trigger it
    // (ISO 7185/EP's Integer is always 64-bit, so this is never set there).
    mutable bool NarrowFoldOverflow_{false};

    // One undiscriminated Type per schema definition; see
    // resolveUndiscriminatedSchema for the key.
    std::unordered_map<std::string, std::shared_ptr<Type>> UndiscSchemaTypes_;
    /// The one EP string schema; see stringSchemaType().
    std::shared_ptr<Type> StringSchemaTy_;

    // --- goto / label nesting checks (ISO §6.8.1) ---

    // Stack of structured-statement pointers (for/while/repeat/case/with/if) that
    // we are currently inside during Phase 6 body traversal.  Pushed in checkStmt,
    // popped after the body is processed.  Used by checkGoto to determine whether
    // a goto would jump INTO a structured statement from outside it.
    std::vector<const StmtNode*> StructStack;

    // --- TP-only: Break/Continue loop-nesting (no ISO/EP equivalent) ---

    /// Depth of while/for/for-in/repeat nesting around the statement
    /// currently being checked.  Break/Continue (checkCallStmt's own arm)
    /// are refused with err_break_outside_loop/err_continue_outside_loop
    /// wherever this is zero.  Incremented/decremented by LoopScope, held
    /// for the extent of each loop's own body check
    /// (checkWhile/checkFor/checkForIn/checkRepeat).
    ///
    /// Saved at 0 and restored around a nested procedure's own body
    /// (checkProcBody, alongside CurrentRetType/CurrentProc/
    /// EnclosingParamNames_, which are already scoped there the same way) --
    /// a Pascal nested procedure's declaration can only appear in its
    /// enclosing block's DECLARATION part, never inside a loop's STATEMENT
    /// body, so checkProcBody's Phase 5b (nested procedure bodies) always
    /// runs before its own block's Phase 6 (where a loop could ever push
    /// this) reaches any loop -- LoopDepth_ is already guaranteed 0 there by
    /// that ordering alone, but is saved/restored explicitly anyway rather
    /// than relying on it, the same defensive-not-implicit stance
    /// EnclosingParamNames_ already takes for a fact its own comment could
    /// have leaned on instead.
    int LoopDepth_{0};

    /// RAII: ++LoopDepth_ for the extent of one loop's own body check.
    struct LoopScope {
        int& Depth;
        explicit LoopScope(int& D) : Depth(D) { ++Depth; }
        ~LoopScope() { --Depth; }
        LoopScope(const LoopScope&)            = delete;
        LoopScope& operator=(const LoopScope&) = delete;
    };

    // Maps label name → innermost enclosing structured-statement pointer for labels
    // that are placed inside a structured statement in the current block's body.
    // Populated by a pre-scan (Phase 5.5) before Phase 6; saved/restored across
    // proc-body sub-checks so inner blocks don't clobber the outer block's info.
    std::unordered_map<std::string, const StmtNode*> LabelEnclosingStmt;

    // Fills LabelEnclosingStmt for the statement S, with NestStack carrying the
    // structured statements enclosing it.  Recursion by an ordinary member
    // function rather than a lambda: written as one taking an explicit object
    // parameter, GCC rejects the reference to LabelEnclosingStmt outright
    // through 14 and crashes on it in 15.
    void scanLabelNesting(const StmtNode* S,
                          std::vector<const StmtNode*>& NestStack);

    // The labels the block being checked declares.  A goto naming one of these
    // stays inside the block; anything else it names belongs to an enclosing
    // block and leaves.  The symbol table cannot answer this on its own: a
    // `with` pushes a scope of its own, so the label section's scope is not
    // always the innermost one.
    std::unordered_set<std::string> CurrentBlockLabels;

    // ---- diagnostics ----
    // Legacy raw-string API (backward compat — sets diag::none).
    void error  (SourceLocation Loc, std::string_view Msg);
    void warning(SourceLocation Loc, std::string_view Msg);
    // DiagID API — formats the message from the catalog and records the ID.
    void error  (SourceLocation Loc, DiagID ID,
                 std::initializer_list<std::string_view> Args = {});
    void warning(SourceLocation Loc, DiagID ID,
                 std::initializer_list<std::string_view> Args = {});

    // ---- built-in registration ----
    void registerBuiltins();

    // ---- block processing ----
    // Six-phase walk: labels → consts → type stubs → type bodies
    //   → vars → proc signatures → proc bodies → compound body → label audit
    //
    // BeforePop runs after the body and before the label audit, while the
    // block's scope is still current.  A module body needs that: its exports
    // and its 'to begin do' / 'to end do' statements are written in terms of
    // names the scope is about to discard.
    // locals are stack storage instead.  Phase 4 runs the same byte-size gate
    // either way (#223) -- an oversized local has no relocation to overflow,
    // but hangs the LLVM backend lowering its `alloca` well before it would
    // fit any real stack -- and uses this flag only to choose which of
    // err_global_var_too_large / err_local_var_too_large names the variable's
    // scope accurately.
    // IsModuleBlock: true only for a module's own body (Sema::processModuleBody).
    // Stamped onto each label this block declares (Symbol::LabelInModuleBlock)
    // so checkGoto can refuse a non-local goto from one of the module's own
    // procedures back into it -- see that field's comment for why.
    // IsInterfaceBlock: true for a module INTERFACE's own block.  Every
    // heading there is recorded IsForward regardless of the 'forward'
    // keyword (EP §6.11.2: the heading alone is the whole declaration, its
    // body given later in a separate implementation block) so the
    // forward-declaration completion audit below does not apply to it.
    void checkBlock(const BlockNode& Block,
                    llvm::function_ref<void()> BeforePop = {},
                    bool IsGlobalScope = false,
                    bool IsModuleBlock = false,
                    bool IsInterfaceBlock = false);
    void checkProcSignature(const ProcDecl& Proc);
    void checkProcBody     (const ProcDecl& Proc);
    /// Records which value parameters a body modifies; see ProcDecl::ModifiedParams.
    void recordModifiedParams(const ProcDecl& Proc);

    // ---- type resolution ----
    // Converts an AST TypeNode to a semantic Type.
    // Emits an error and returns TyErr on failure.
    [[nodiscard]] std::shared_ptr<Type> resolveType(const TypeNode& Node);
    /// Body of resolveType; call resolveType so the node gets annotated.
    [[nodiscard]] std::shared_ptr<Type> resolveTypeImpl(const TypeNode& Node);
    /// Records on \p T the discriminants it was resolved under, when T is a
    /// record built from this very node.  One declaration serves every
    /// instantiation, so this is what tells codegen which one it is looking at
    /// -- and it has to be stamped on the probe body too, which reaches the
    /// declaration through resolveTypeImpl and so never passed through
    /// resolveType.
    void stampSchemaBindings(const TypeNode& Node, Type* T) const;
    /// Adds the fields of a variant part, and of the variants nested in it, to
    /// the record type T, so that field access can find them (§6.4.3.3).
    void walkVariantFields(const VariantPart& Vp, Type& T);
    /// ISO §6.6.5.3: checks one of new/dispose's extra arguments -- \p Which
    /// is "new" or "dispose", for the diagnostic -- against \p Vp, the
    /// variant level it selects: the argument must be a value of \p Vp's own
    /// tag type.  Returns the NestedVariant of whichever of \p Vp's arms the
    /// argument names (or null if it named none, or was not itself a
    /// constant), i.e. the level the *next* argument, if any, must answer
    /// for -- so the caller can walk as many levels as arguments were given
    /// and tell a valid path from one with more arguments than the record
    /// has nesting to check them against.
    const VariantPart* checkVariantTagArg(const std::string& Which,
                                          const ExprNode& Arg, const Type& At,
                                          const VariantPart& Vp);
    [[nodiscard]] std::shared_ptr<Type> resolveNamed(const NamedTypeNode& N);
    /// EP §6.6: checks a denoter's 'value' clause against the type it denotes.
    void checkInitialState(const TypeNode& Node, const Type& T);
    /// The type the name denotes, before EP §6.4.2.5's 'restricted' is applied.
    [[nodiscard]] std::shared_ptr<Type> resolveNamedUnrestricted(const NamedTypeNode& N);
    /// resolveType for a formal-parameter type-denoter, where EP §6.7.3.1
    /// admits a bare schema-name as the parameter-form.
    [[nodiscard]] std::shared_ptr<Type> resolveParamType(const TypeNode& Node,
                                                         bool IsVar = false) {
        AllowSchemaScope Guard(AllowUndiscriminatedSchema_);
        auto T = resolveType(Node);
        // EP §6.7.3.1 also admits a bare `string`, which accepts an actual
        // parameter of any capacity.  Everything downstream of here works in
        // terms of a capacity, so give it the largest one rather than leaving
        // it as the capacity-less string that only a literal ever has.
        if (T && T->Kind == TypeKind::String) {
            // A VALUE parameter is a copy, so the widest capacity plang has
            // holds any actual.  A VAR parameter is not a copy: ISO §6.6.3.3
            // requires its actual to be of the parameter's OWN type, so a
            // formal of one fixed capacity matches nothing at all and
            // `procedure p(var s: string)` rejected every actual.  A capacity
            // that arrives with the actual is EP §6.4.3.3's string schema.
            T = IsVar ? stringSchemaType()
                      : Ctx_.getVarString(PlangMaxStringCapacity);
            Node.ResolvedType = T; // codegen lowers the denoter from this
        }
        return T;
    }
    /// EP §6.4.7: builds the undiscriminated type for a schema whose
    /// discriminants arrive at run time.  Emits a diagnostic and returns TyErr
    /// if the body cannot be given a run-time representation.
    [[nodiscard]] std::shared_ptr<Type> resolveUndiscriminatedSchema(
        Symbol& Sym, const NamedTypeNode& N);
    /// EP §6.4.7: resolves Sym's discriminant parameter type names into
    /// SchemaDeclParams (and records SchemaBodyNode/DeclLoc), from the
    /// TypeDef stashed at SchemaDeclTypeDef.  Idempotent -- guarded by
    /// SchemaParamsResolved -- because this runs both from an explicit sweep
    /// over every schema in the block (Sema.cpp) and on demand from
    /// SchemaTypeNode resolution and resolveUndiscriminatedSchema below,
    /// whichever reaches a given schema first.  See Sema.cpp's Phase 3b(ii)
    /// for why a fixed position in the phase order cannot serve both needs.
    void resolveSchemaParams(Symbol& Sym);

    /// EP §6.4.3.3: `string` is a schema with one discriminant, its capacity.
    /// A bare `string` denotes it where any bare schema-name may be written --
    /// a pointer's domain type, a parameter's type -- and the capacity comes
    /// from new() or from the actual parameter.
    [[nodiscard]] std::shared_ptr<Type> stringSchemaType();

    // Extract a compile-time integer value from a constant expression.
    // Returns nothing when the expression is not a constant, so that a caller
    // cannot mistake "no value" for a bound.  Consults ActiveSchemaBindings_ so
    // schema discriminant names are recognized.
    [[nodiscard]] std::optional<int64_t> constBound(const ExprNode& E) const;
    /// EP §6.4.7 R3: \p E as arithmetic over discriminant indices with every
    /// other leaf folded here, in the scope the declaration was written in.
    /// Nothing in the result names anything.
    [[nodiscard]] std::optional<Type::ExtentForm> buildExtentForm(
        const ExprNode& E, const std::vector<std::string>& Discs) const;
    /// The body of constBound.  Call constBound, which also records the answer
    /// on the node for codegen to use instead of folding it a second time.
    [[nodiscard]] std::optional<int64_t> constBoundImpl(const ExprNode& E) const;

    /// constBound's real-valued sibling: a real literal, a real-typed named
    /// constant, or unary +/- over either.  Not a general real evaluator --
    /// see ExprNode::ConstRealVal for why this exists at all.  Records the
    /// answer on E.ConstRealVal, the same way constBound records onto
    /// E.ConstVal.
    [[nodiscard]] std::optional<double> constRealBound(const ExprNode& E) const;

    /// Reports the first statement of each run in this sequence that no path
    /// can reach, a run being ended by a label that a goto could land on.
    void warnUnreachable(const std::vector<std::unique_ptr<StmtNode>>& Stmts);

    // ---- Definite assignment (§6.5.1, §6.8.3.9, §6.6.2) ----
    //
    // Reading a variable that has not been given a value is an error §5.1 f) 1)
    // lets go unreported, and plang's does go unreported: the value is whatever
    // the storage held.  This walk reports the cases it can see, which is not
    // all of them and is not meant to be.
    //
    // The walk is a definite-assignment analysis: a variable is warned about
    // only where no path to the read assigns it, so branches merge by
    // intersection and a loop body is entered with what held before it.  Where
    // the flow cannot be followed the block is abandoned rather than guessed
    // at, which is what makes a warning from here worth believing.

    /// What is known at a point in the walk.
    struct FlowState {
        /// Variables that every path to here has given a value.
        std::set<std::string> Assigned;
        /// Control variables of for-statements that have finished: §6.8.3.9
        /// makes these undefined again, and says so in its own words.
        std::set<std::string> UndefAfterFor;
        /// Whether the enclosing function's result has been assigned.
        bool ResultAssigned = false;
        /// Whether this path has already left for good -- a call to Halt,
        /// RunError, Exit, Break, or Continue (builtinAlwaysTransfers,
        /// BuiltinIDs.h), the same set alwaysTransfers (SemaStmt.cpp) treats
        /// as leaving unreachable code behind.  Set by flowStmt's CallStmt
        /// arm and read only by mergeWith: nothing downstream of this walk
        /// stops processing a dead path (the same as SemaStmt.cpp's own
        /// unreachable-code check does not stop checkStmt), so a redundant
        /// warning inside genuinely dead code is possible and not new -- it
        /// is Halt's existing behavior extended to four more builtins, not a
        /// change to it.  What Dead changes is only what a path contributes
        /// at the join with a live sibling: nothing, since it was never
        /// going to arrive there.
        bool Dead = false;

        /// Combines this with what another branch leaves behind, giving what
        /// is still known where the two meet.  A dead side (one that left via
        /// Halt/Exit/Break/Continue/RunError before reaching this point)
        /// contributes nothing: the merged state is simply the live side's,
        /// unchanged, since the dead side was never actually going to arrive
        /// here to disagree with it.  Only when both sides are dead -- so
        /// that neither disagreement nor agreement about what reaches this
        /// point actually matters -- does this fall back to the ordinary
        /// intersect/unite merge below.
        void mergeWith(const FlowState& Other);
    };

    /// Runs the walk over a block's body and reports what it finds.  Does
    /// nothing for a block whose flow it cannot follow.
    void checkDefiniteAssignment(const BlockNode& Block);
    void flowStmt(const StmtNode* S, FlowState& St);
    void flowSeq(const std::vector<std::unique_ptr<StmtNode>>& Stmts, FlowState& St);
    /// Reports any read of a tracked variable in E that St says has no value.
    void flowRead(const ExprNode* E, FlowState& St);
    /// Notes that E has been given a value, if E names a tracked variable.
    void flowWrite(const ExprNode* E, FlowState& St);

    /// Variables the current walk is watching.  Anything not in here is either
    /// not a plain local, or is reachable from somewhere the walk cannot see.
    std::set<std::string> FlowTracked_;
    /// Names that denote the result of the function whose body is being
    /// walked — the function's own identifier and, in Extended Pascal, the
    /// result variable it may have been given.
    std::set<std::string> FlowResultNames_;
    /// Warned about already, so that a variable read in a loop is reported
    /// once rather than once for each place the walk passes the read.
    std::set<std::string> FlowReported_;

    /// Whether a Break (TP-only) has been seen in the body of the innermost
    /// while/for/for-in/repeat statement currently being walked -- the same
    /// nesting LoopDepth_ (Sema.h, checkFor et al.) tracks for a different
    /// question, kept separately because this one is scoped and consumed by
    /// the flow walk alone.  Saved, reset to false, and restored by each of
    /// flowStmt's four loop arms around walking their own body, so a Break
    /// belongs only to its own innermost loop and does not leak to one it is
    /// nested inside.  Only the ForStmt arm reads it back out: unlike falling
    /// off the end of the body, TP's Break leaves the control variable
    /// holding whatever value it had, not undefined (§6.8.3.9's rule is about
    /// a for-statement finishing by exhausting its range, which a Break
    /// preempts), so a for-loop that can be left this way must not mark its
    /// control variable UndefAfterFor on that account.  While/Repeat/For-in
    /// still reset and restore it, purely to keep a Break inside THEIR body
    /// from being mistaken for one in an enclosing for-loop's.
    bool FlowLoopBroke_ = false;

    /// Set when a path being walked reaches a bare Halt, Exit, or RunError
    /// (never Break/Continue, which end a loop iteration rather than the
    /// function) with the result not yet assigned on THAT path.  mergeWith's
    /// dead-side rule means a path like this may never be folded into a
    /// later join at all -- it is exactly the kind of path the rule exists
    /// to drop, since nothing after the join can be reached along it -- so
    /// checking only the final FlowState at the end of the walk, the way
    /// warn_result_not_always_set otherwise does, would miss it.  This is
    /// the site where that check is made instead, kept outside the
    /// FlowState algebra on purpose so a dead path's own answer survives
    /// being dropped from every later merge.  Reset per checkDefiniteAssignment
    /// call, the same as FlowTracked_/FlowResultNames_/FlowReported_.
    bool FlowResultMaybeUnset_ = false;

    /// Warns when a constant assigned to a subrange variable lies outside it,
    /// which the run-time check is certain to catch wherever it is reached.
    void warnIfConstantOutOfRange(const Type& Dst, const ExprNode& Src);

    /// warnIfConstantOutOfRange's sibling for a set's members: a set-literal
    /// element (or range endpoint) that is a compile-time constant, and lies
    /// outside ElemBase's own ordinal range, is certain to trap once codegen's
    /// RangeCheckGuards check runs. Recurses through E the way adoptSetType
    /// does, so a loose set combined with `+`/`*`/etc. is covered the same as
    /// a bare set-literal.
    void warnIfSetLitOutOfRange(const Type& ElemBase, const ExprNode& E);

    /// Warns when one operand's type settles a comparison on its own, so that
    /// the other operand is not being consulted: `i > 99` where i is a 1..10.
    /// Quiet unless the range was written into the program — a subrange or an
    /// enumeration — since the implicit bounds of char and boolean would make
    /// this fire on ordinary code.
    void warnIfComparisonIsSettled(const BinaryExpr& E, const Type& Lt,
                                   const Type& Rt);

    /// Whether a value of this type is or holds a file (ISO §6.6.3.2).  A file
    /// is a window onto something outside the program, so the language refuses
    /// wherever copying one would have to mean copying that: a value parameter,
    /// an assignment, a function result.
    [[nodiscard]] static bool typeContainsFile(const Type& T);

    // ---- ISO §6.6.3.1: procedural and functional parameters ----
    // Whether two formal parameters written in different headings denote the
    // same type; used by congruity and by the forward-declaration check.
    [[nodiscard]] bool sameParamType(const std::shared_ptr<Type>& A,
                                     const std::shared_ptr<Type>& B) const;
    // Congruity of two parameter lists (ISO §6.6.3.6).
    [[nodiscard]] bool congruousSignature(const Type& A, const Type& B) const;
    // Congruity of two conformant array schemas (EP §6.7.3.7).
    [[nodiscard]] bool congruousConformant(const Type& A, const Type& B) const;
    // Checks the procedure name supplied for a procedural formal parameter.
    void checkProcedureActual(const Type& Formal, const std::string& ParamName,
                              const ExprNode& Arg);

    // ---- Turbo procedural TYPES and VALUES ----
    // Whether \p Id is a bare identifier that may stand for the routine it
    // names (rather than for a call to it): true only when Symtab finds a
    // non-parameter, non-nested SymbolKind::Proc under that name.  Used to
    // decide, from syntax alone, whether a caller should route through
    // checkRoutineValue instead of the ordinary checkExpr/checkIdent call
    // reading.
    [[nodiscard]] bool isRoutineNameCandidate(const IdentExpr& Id) const;
    // \p Id is being used where a routine's own value is wanted -- the direct
    // operand of `@`, or the direct RHS of an assignment to a procedural
    // variable.  Resolves it to that routine's own callable Type (marking
    // Id.Resolution = RoutineReference), or reports why it cannot be one --
    // a procedural PARAMETER (its capture status is not known here) or a
    // NESTED routine (no frame slot exists to carry one) -- and returns
    // TyErr.  Caller-checked (isRoutineNameCandidate) rather than
    // self-guarding with a null return, so every call site is explicit about
    // when it is and is not asking this question.
    [[nodiscard]] std::shared_ptr<Type> checkRoutineValue(const IdentExpr& Id);

    // Folds both bounds of an index type or subrange, reporting ID against
    // whichever is not constant.  Nothing means the type cannot be formed.
    [[nodiscard]] std::optional<std::pair<int64_t, int64_t>>
    foldBounds(const ExprNode& Low, const ExprNode& High,
               const Type& Base, DiagID LowID, DiagID HighID);

    // ISO §6.4.2.2/§6.4.3.2: reports err_bound_types_differ (against High's
    // location) and returns false when LoTy and HiTy are both ordinal but
    // not of the same ordinal type.  True (nothing to report here) when
    // either side is already an error, or is not ordinal at all -- that half
    // of the question belongs to the caller, which already has its own
    // not-ordinal diagnostic in scope.
    [[nodiscard]] bool boundsShareOrdinalType(const Type& LoTy, const ExprNode& High,
                                              const Type& HiTy);

    // ---- statement checking ----
    void checkStmt      (const StmtNode*   Stmt);
    void checkCompound  (const CompoundStmt& S);
    void checkAssign    (const AssignStmt&   S);
    void checkIf        (const IfStmt&       S);
    void checkWhile     (const WhileStmt&    S);
    void checkFor       (const ForStmt&      S);
    void checkRepeat    (const RepeatStmt&   S);
    void checkCallStmt  (const CallStmt&     S);
    void checkWith      (const WithStmt&     S);
    void checkGoto      (const GotoStmt&     S);
    void checkLabeled   (const LabeledStmt&  S);
    void checkCase      (const CaseStmt&     S);

    // Scan a statement tree for for-loop control variable threats (ISO §6.8.3.9).
    // When `Callables` is null the symbol table is used to resolve var-param flags.
    // When `Callables` is non-null (inter-procedural mode) the symbol table scope
    // for the for-loop's block is already closed; use the supplied AST proc list.
    // `Shadowed` is true once a `with` between the for-statement and `Stmt`
    // exposes a field or discriminant spelled like `VarName`: a bare reference
    // to that spelling from there down denotes the field, not the control
    // variable (see withExposesName), so none of the checks apply until the
    // shadowing `with` is left.
    void checkForBody(const StmtNode* Stmt, const std::string& VarName,
                      SourceLocation ForLoc,
                      const std::vector<const ProcDecl*>* Callables = nullptr,
                      bool Shadowed = false);

    /// Static type of a `with`-clause record expression, resolved without
    /// checkExpr's diagnostics.  checkForBody runs before the body is
    /// type-checked for real, so calling checkExpr here would re-evaluate an
    /// expression checkWith is about to and double-report anything wrong with
    /// it.  Returns null wherever the answer is not immediately at hand; that
    /// is always a safe answer, since the caller only uses this to rule a name
    /// safely OUT of being the control variable.
    std::shared_ptr<Type> quietWithRecordType(const ExprNode* E);

    /// Does a `with` over \p S's record(s) expose \p Name as a field or a
    /// schema discriminant, so that a bare reference to it inside the with
    /// body denotes THAT rather than any same-spelled outer variable?
    bool withExposesName(const WithStmt& S, const std::string& Name);

    /// Does \p P declare \p Name of its own, so that the name denotes something
    /// other than the outer variable everywhere inside it?  ISO §6.8.3.9 forbids
    /// a threat to "the variable denoted by the control-variable", and a
    /// same-spelled local is a different variable.
    static bool hidesName(const ProcDecl& P, const std::string& Name);

    /// Scan \p P, and the procedures it encloses, for threats to \p VarName.
    /// \p Siblings is the declaration part P was found in, used to resolve which
    /// procedure a call names.
    void checkProcForThreats(
        const ProcDecl& P, const std::string& VarName, SourceLocation ForLoc,
        const std::vector<std::unique_ptr<ProcDecl>>& Siblings);

    void checkForIn  (const ForInStmt& S);
    void checkSetBaseRange(const Type& Base, SourceLocation Loc);

    // ---- expression checking ----
    // Returns the semantic type of the expression; never nullptr (uses TyErr
    // on failure to suppress cascades).
    [[nodiscard]] std::shared_ptr<Type> checkExpr    (const ExprNode& E);
    [[nodiscard]] std::shared_ptr<Type> checkIdent   (const IdentExpr& E);
    [[nodiscard]] std::shared_ptr<Type> checkIndex   (const IndexExpr& E);
    [[nodiscard]] std::shared_ptr<Type> checkField   (const FieldExpr& E);
    [[nodiscard]] std::shared_ptr<Type> checkDeref   (const DerefExpr& E);
    [[nodiscard]] std::shared_ptr<Type> checkBinary  (const BinaryExpr& E);
    [[nodiscard]] std::shared_ptr<Type> checkUnary   (const UnaryExpr& E);
    [[nodiscard]] std::shared_ptr<Type> checkCallExpr(const CallExpr& E);
    /// SizeOf/High/Low's sole argument: either a TYPE NAME -- one of the
    /// five primitive keywords (parsed as a synthetic IdentExpr; see
    /// Parser::parseSizeHighLowArg) or an ordinary user-defined type name
    /// (an IdentExpr indistinguishable, syntactically, from a variable
    /// reference) -- or, exactly like real FPC, an ordinary VALUE
    /// expression, whose own type answers the question instead (SizeOf(x),
    /// High(someArray)).  Returns the resolved Type this builtin should
    /// answer about; TyErr on failure.  Every other builtin's arguments are
    /// plain checkExpr() calls -- this is the one call site that has to
    /// tell "a type" and "a value" apart before it knows which to ask for.
    [[nodiscard]] std::shared_ptr<Type> resolveTypeArgOrValue(const ExprNode& Arg);
    /// TP-only: TypeName '(' expr ')'.  Resolves TypeName via resolveNamed --
    /// the same name resolution an ordinary type-denoter goes through, so a
    /// cast to a built-in keyword type (Integer, Real, ...) or to a
    /// user-defined TypeAlias/Schema is accepted or refused exactly as
    /// `var x: TypeName` already would be -- and accepts the cast when
    /// either of two independent rules holds: both types are ordinal-or-real
    /// (a VALUE conversion), or the two types are the same size
    /// (Sema::byteSizeOf) — a VARIABLE reinterpretation. Which rule actually
    /// applies to a given occurrence is decided later, by isLValue: a
    /// same-size cast of an lvalue operand is ALSO usable as an lvalue
    /// itself (see isLValue's TypeCastExpr case), while a cast whose sizes
    /// differ can only ever be read as a converted value.
    [[nodiscard]] std::shared_ptr<Type> checkTypeCast(const TypeCastExpr& E);
    /// Diagnoses a required function called with the wrong number of
    /// arguments.  Built-ins bypass the ordinary signature check, so without
    /// this an `abs()` reaches codegen and is indexed out of bounds there.
    /// Returns false when a diagnostic was emitted.
    [[nodiscard]] bool checkBuiltinArity(BuiltinID ID,
                                         const std::string& LowerName,
                                         SourceLocation Loc, size_t NumArgs);
    /// Diagnoses use of a built-in name the active dialect does not declare
    /// (Sym.NotInDialect, set at registration from Builtins.def's own
    /// Dialects mask -- see registerBuiltins).  Named checkEPOnly from when
    /// every such name was Extended Pascal's alone; picks between
    /// err_ep_required_name and err_turbo_required_name by asking
    /// builtinDialects(Sym.BuiltinKind) which dialect(s) the name is
    /// ACTUALLY declared for, rather than assuming, so a Turbo-only name
    /// (Assert, the first one) is not told it is an Extended Pascal
    /// extension -- which would also be wrong in the specific way that
    /// implies -std=iso10206 accepts it, when no dialect but Turbo does.
    /// Returns false when a diagnostic was emitted.
    [[nodiscard]] bool checkEPOnly(const Symbol& Sym, SourceLocation Loc);
    /// Diagnoses a name from Turbo Pascal's real-mode DOS surface (Seg, Ofs,
    /// Mem, Intr, ...; the full list is isRealModeDosName's, in SemaExpr.cpp)
    /// used where ordinary lookup has already failed to resolve it to any
    /// declaration -- built-in, imported, or the program's own.  Only ever
    /// worth asking under -std=turbo (no other dialect has ever had these
    /// names) and only once Symtab.lookup has already come back null, so a
    /// program's own `var Mem: Integer` always wins and never reaches this
    /// check at all.  These names are deliberately NOT Symbol table entries;
    /// this is the one place that knows the list.  Returns true (and emits
    /// err_turbo_real_mode_facility) when Name is one of them; the caller is
    /// then done and must not also emit its own generic undefined-X
    /// diagnostic for the same name.
    [[nodiscard]] bool checkRealModeDosName(const std::string& Name, SourceLocation Loc);
    /// Diagnoses a read-parameter of a type §6.9.2 does not read into.
    void checkReadParamType(const Type& T, SourceLocation Loc);
    /// TargetHint: a bounded Set type this literal is known to be assigned
    /// into (e.g. from checkAssignStmt), even though it has no E.TypeName of
    /// its own -- suppresses the loose-literal element-count check below,
    /// since the runtime representation will use TargetHint's own bounds,
    /// not a window derived from the literal's raw element values. nullptr
    /// (the default, used by checkExpr's generic dispatch) preserves the
    /// original context-free behavior.
    [[nodiscard]] std::shared_ptr<Type> checkSetLit  (const SetLiteralExpr& E,
                                                       const std::shared_ptr<Type>& TargetHint = nullptr);
    [[nodiscard]] std::shared_ptr<Type> checkStructuredValue(const StructuredValueExpr& E);

    /// TP-only: a typed constant's initializer must be a compile-time
    /// constant -- CodeGen bakes it into a real llvm::Constant, not code that
    /// runs to compute it (see CGTypedConst.cpp).  Walks \p E, which has
    /// already been through checkExpr/checkStructuredValue (so every arm's
    /// ResolvedType is set), and for each SCALAR leaf calls constBound /
    /// constRealBound -- exactly the same fold CodeGen will read back off
    /// ConstVal/ConstRealVal, so "Sema accepted it" and "CodeGen can lower
    /// it" cannot disagree.  Recurses into a StructuredValueExpr's own arms
    /// (record and array alike), and for an array whose element count is
    /// known, additionally checks that a purely positional literal (Turbo's
    /// own array-literal form -- no EP-style index label) supplies exactly
    /// that many elements.  \p Name is the constant's own name, for the
    /// diagnostic.
    void checkTypedConstFoldable(const ExprNode& E, const std::string& Name);

    /// The span of a set-constructor's ordinals, when they all fold; nothing
    /// otherwise.  See checkSetLit.
    [[nodiscard]] std::optional<std::pair<int64_t, int64_t>>
    literalSetWindow(const SetLiteralExpr& E);

    // ---- ISO §6.7.1: a set-constructor takes its type from the context ----
    /// True when E's set type came from a set-constructor rather than from a
    /// declared type, so the surrounding context decides which set type it is.
    [[nodiscard]] static bool isLooseSet(const ExprNode& E);
    /// Retypes a loose set expression, and the loose set expressions within it,
    /// as Want.  Codegen reads the annotation to choose the bit layout, so this
    /// is what makes `s := [-1, 3]` agree with `s: set of -1..10`.
    static void adoptSetType(const ExprNode& E, const std::shared_ptr<Type>& Want);
    /// adoptSetType for the operand of a two-operand form, when exactly one
    /// side is loose and the other names a definite set type.
    static void unifyLooseSets(const ExprNode& L, const ExprNode& R,
                               const std::shared_ptr<Type>& Lt,
                               const std::shared_ptr<Type>& Rt);

    /// EP §6.9.2.2: reports a string value whose length is already known to
    /// exceed the capacity of the variable it is being assigned to.  The rest
    /// of the cases are checked when the assignment runs.
    void checkStringCapacity(const Type& Dst, const ExprNode& Src);

    // ---- helpers ----

    // Returns true if the expression is a syntactic lvalue (can appear on the
    // left of ':=' or be passed as a 'var' parameter).
    [[nodiscard]] bool isLValue(const ExprNode& E) const;

    // Returns true iff a and b are the same canonical type (pointer equality
    // on types routed through the TypeContext; error type suppresses cascades).
    [[nodiscard]] bool isIdenticalType(const std::shared_ptr<Type>& a,
                                       const std::shared_ptr<Type>& b) const;

    /// "Can a value of type Src be assigned to a variable of type Dst?"
    /// ISO §6.4.6.
    ///
    /// \p ExactBounds is set where the recursion is asking whether two types are
    /// the *same* rather than whether one may be assigned to the other — the
    /// components of a structured type, where two subranges of differing bounds
    /// mean two different sizes and two different layouts.  At the top level the
    /// bounds are not part of the question: §6.4.6 c) and d) make a value
    /// outside the destination's interval an error to be reported when the
    /// assignment happens, not a type error.
    /// \p Depth bounds the structural comparison of two records that share a
    /// name without sharing a declaration; a record reachable from itself
    /// through a pointer would otherwise recurse without end.
    /// EP §6.7.3.1 / §6.11.2: diagnose writing through a protected parameter or
    /// a protected imported variable, whatever access path is written on top of
    /// it.  Called wherever a variable is WRITTEN, not only in an assignment.
    void checkNotProtected(const ExprNode& Target, SourceLocation Loc);

    /// The protected symbol \p Target's access path bottoms out at, or null.
    /// The walk checkNotProtected does, without the diagnostic -- so a `with`
    /// over a protected record-access can mark the fields it exposes.
    Symbol* protectedBaseOf(const ExprNode& Target);

    [[nodiscard]] bool isAssignCompatible(const Type& Dst, const Type& Src,
                                          bool ExactBounds = false,
                                          int Depth = 0) const;

    /// ISO §6.6.3.8: "may an array of type Actual be passed to a conformant
    /// array parameter declared with schema Formal?"  A separate question from
    /// assignment: an array conforms to a schema whatever bounds it was
    /// declared with, and is still not a value that may be assigned to one.
    [[nodiscard]] bool isConformable(const Type& Formal,
                                     const Type& Actual) const;
    /// ISO §6.7.3.8: reports whichever of isConformable's three conditions
    /// (actual-is-an-array, packedness (d), index-type compatibility (a), or
    /// finally element type) is the first -- in the same order, and at the
    /// same nesting level -- to actually fail between Formal and Actual.
    /// Mirrors isConformable's own recursion into inner dimensions exactly,
    /// so a mismatch buried in an INNER dimension of a multi-dimensional
    /// conformant-array parameter gets the same precise diagnostic an OUTER
    /// one does, rather than always falling through to the generic
    /// element-type message.  Only meaningful once isConformable(Formal,
    /// Actual) is already known false.
    void diagnoseConformMismatch(const std::string& ParamName, SourceLocation Loc,
                                 const Type& Formal, const Type& Actual);
    /// EP §6.4.2.5: reports reaching into a value of a restricted type, and
    /// answers whether the attempt was made.
    bool rejectRestrictedComponent(const ExprNode& E, const Type& T);
    /// True when an assignment's target names the enclosing function rather
    /// than a variable, so the value goes to that activation's result.
    [[nodiscard]] bool isFunctionResultTarget(const ExprNode& Target) const;

    // Result type of '+', '-', '*' given two numeric operand types.
    [[nodiscard]] std::shared_ptr<Type> numericResult(const Type& L, const Type& R) const;

    // Check arity and argument types for a user-defined callable.
    void checkCallArgs(const Symbol& Sym, SourceLocation CallLoc,
                       std::span<const std::unique_ptr<ExprNode>> Args);

    // Validate kind, arity, and argument types for a user-defined callable.
    // `ExpectFunction` = true for call-expressions, false for call-statements.
    // Returns the declared return type, or TyErr on error.
    [[nodiscard]] std::shared_ptr<Type>
    checkUserDefinedCall(const Symbol& Sym, SourceLocation CallLoc,
                         std::span<const std::unique_ptr<ExprNode>> Args,
                         bool ExpectFunction);

    // Push an empty scope, define each field of a record type as a Var symbol,
    // and return the depth so they can be popped after the with body.
    // Returns the number of scopes pushed (0 on error).
    int pushWithScope(const WithStmt& S);
};

} // namespace plang
