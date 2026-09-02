// CodeGen.cpp — LLVM IR generation using the LLVM C++ API.
//
// No exceptions are thrown or caught in this file; errors are emitted as
// IR comments and the module is printed regardless.

#include "CodeGenImpl.h"

#include "ConstFold.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"

#include <iostream>

// ---------------------------------------------------------------------------
// Codegen public methods
// ---------------------------------------------------------------------------

void Codegen::Impl::optimize() {
    if (optLevel == 0) return;
    // Without the target's layout the passes would compute field offsets that
    // the backend then disagrees with, which corrupts records rather than
    // slowing them down.  Emitting unoptimized code is the safe answer.
    if (mod->getDataLayoutStr().empty()) return;

    llvm::LoopAnalysisManager     lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager    cgam;
    llvm::ModuleAnalysisManager   mam;

    llvm::PassBuilder pb;
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);

    llvm::OptimizationLevel level = llvm::OptimizationLevel::O1;
    if      (optLevel == 2) level = llvm::OptimizationLevel::O2;
    else if (optLevel >= 3) level = llvm::OptimizationLevel::O3;

    pb.buildPerModuleDefaultPipeline(level).run(*mod, mam);
}

Codegen::Codegen(const LangOptions& Opts) : PImpl(std::make_unique<Impl>()) {
    PImpl->langOpts    = Opts;
    PImpl->nilChecks   = Opts.NilChecks;
    PImpl->optLevel    = Opts.OptLevel;
}
Codegen::~Codegen() = default;

void Codegen::setImportOwners(const ImportOwnerTable& Owners) {
    PImpl->importOwners_ = &Owners;
}

void Codegen::setLoadedInterfaces(std::vector<const ModuleNode*> Ifaces) {
    PImpl->loadedInterfaces_ = std::move(Ifaces);
}

void Codegen::setUsedUnits(std::vector<const UnitNode*> Units) {
    PImpl->usedUnits_ = std::move(Units);
}

void Codegen::setSourceManager(const SourceManager& SM, FileID MainFile) {
    PImpl->srcMgr_     = &SM;
    PImpl->mainFileID_ = MainFile;
}

bool Codegen::emit(const ProgramNode& prog, std::ostream& os) {
    PImpl->init(prog.Name); // gotoEngine_ is freshly constructed here, with
                            // an empty label map already -- nothing left to clear.

    PImpl->pushScope(); // global scope

    // -std=turbo only: ExitCode's storage, before anything that might
    // reference it -- a module's own procedures included, which are
    // emitted starting in the very next loop below.
    PImpl->emitPredefinedGlobals();

    // EP §6.11: emit module bodies (globals + procedures) before the program.
    //
    // Each body gets its own naming scope and its own mangling prefix.  A
    // module is an outer scope in the same sense a procedure is, so what it
    // declares is mangled with its name; two modules may each export an `f`,
    // and before this they collided on the single symbol `plang_f`.  The scope
    // keeps them apart on the Pascal side to match: the program reaches an
    // imported name through its import clauses, not by finding whatever
    // happened to be emitted last.
    // EP §6.11: a type or constant an imported interface declares must be
    // visible before ANY module body is emitted, not just the program's own
    // block below.  When the whole program is one compilation, an imported
    // module is also one of prog.Modules and registers its own constants
    // (emitGlobals, in the loop just below) before a later module's body can
    // reference them.  Under genuine separate compilation, though, an
    // imported module contributes nothing to prog.Modules at all -- only a
    // loadedInterfaces_ entry read back from its .pmi -- so if this
    // registration ran only after the modules loop (where it used to live,
    // alongside the declare-only loop for imported procedures), a module
    // compiled here that referenced an imported constant or enum literal in
    // its OWN body found nothing in `consts` yet, fell through to the
    // imported-variable path, and emitted an external reference
    // (`pasg_a$Green`) to a symbol no one -- A included, since enum literals
    // and folded constants are inlined values, not globals -- ever defines,
    // breaking the link.  Registering here, before any body is emitted,
    // fixes that without disturbing the single-invocation case: a module's
    // own later (re)declaration of the same name still wins, since
    // registerInterfaceTypes only fills a name `consts` does not already
    // have, and emitGlobals's own constant loop overwrites unconditionally.
    for (const auto* Iface : PImpl->loadedInterfaces_)
        if (Iface->Body)
            PImpl->registerInterfaceTypes(*Iface->Body, toLower(Iface->Name));
    for (const auto* Iface : PImpl->loadedInterfaces_) {
        if (!Iface->Body) continue;
        PImpl->namePrefix = PlangProcPrefix + toLower(Iface->Name) + PlangScopeSep;
        for (const auto& Proc : Iface->Body->Procs)
            PImpl->emitFunctionDef(*Proc, /*declareOnly=*/true);
    }
    PImpl->namePrefix = PlangProcPrefix;

    std::vector<std::string> InitModules;
    for (auto* Mod : prog.Modules) {
        if (Mod->IsInterface) continue;
        const std::string Unit = toLower(Mod->Name);
        PImpl->pushScope();
        PImpl->currentUnit_  = Unit;
        PImpl->namePrefix    = PlangProcPrefix   + Unit + PlangScopeSep;
        PImpl->globalPrefix  = PlangGlobalPrefix + Unit + PlangScopeSep;
        PImpl->moduleIfaceBlock_   = nullptr;
        PImpl->moduleIfaceImports_ = nullptr;
        if (Mod->Body) {
            PImpl->emitGlobals(*Mod->Body);
            // EP §6.11.1: whatever the module's heading declares is the
            // block's too, and this module is where it lives.
            for (auto* Iface : prog.Modules)
                if (Iface->IsInterface && Iface->Body
                        && eqCI(Iface->Name, Mod->Name)) {
                    PImpl->moduleIfaceBlock_   = Iface->Body.get();
                    PImpl->moduleIfaceImports_ = &Iface->Imports;
                    PImpl->emitInheritedGlobals(*Iface->Body, *Mod->Body);
                    break;
                }
            PImpl->emitAllProcedures(*Mod->Body);
            // Issue #619: see ensureOwnedVmtsDefined's own comment
            // (CodeGenImpl.h) -- a no-op today (TypeKind::Object is Turbo-
            // only, and an EP module is never compiled under -std=turbo),
            // added for the same completeness every other block that can
            // declare a type gets, should that ever change.
            PImpl->ensureOwnedVmtsDefined(*Mod->Body);
        }
        // The lifecycle blocks read the module's own variables, so they are
        // emitted here, while its scope is still standing.  The finaliser goes
        // first because the initialiser ends by registering it.
        if (Mod->FinalStmt)
            PImpl->emitModuleLifecycleFn("__plang_fini_" + Unit,
                                              *Mod->FinalStmt);
        PImpl->emitModuleInitFn(*Mod);
        InitModules.push_back(Mod->Name);
        PImpl->moduleIfaceBlock_   = nullptr;
        PImpl->moduleIfaceImports_ = nullptr;
        PImpl->popScope();
    }
    PImpl->currentUnit_.clear();
    PImpl->namePrefix   = PlangProcPrefix;
    PImpl->globalPrefix = PlangGlobalPrefix;

    // A module compiled on its own is reached only through the program's import
    // clauses, and its initialiser has to be called from here or never at all.
    for (const auto& Clause : prog.Imports) {
        if (isBuiltinModule(Clause.ModuleName)) continue;
        const bool Local = std::any_of(
            InitModules.begin(), InitModules.end(),
            [&](const std::string& N) { return eqCI(N, Clause.ModuleName); });
        if (!Local) InitModules.push_back(Clause.ModuleName);
    }

    // Turbo Tier 4, Cluster A item 1: register the narrow subset of a used
    // unit's interface this item supports (see Codegen::setUsedUnits's own
    // comment) before the program's own globals -- so a same-named constant
    // the program declares for itself still wins (emitGlobals's own loop
    // overwrites `consts` unconditionally, the same "later write wins" rule
    // that already gives a later 'uses'd unit precedence over an earlier
    // one, here extended one more scope level with the program's own
    // top-level consts innermost of all -- exactly mirroring Sema's own
    // scope-stack order for the identical reason).
    PImpl->registerUsedUnitConsts();

    PImpl->emitFileParams(prog.FileParams);
    PImpl->emitGlobals(*prog.Block);
    // ISO §6.8.1: a procedure may goto a label of the program's block, so the
    // buffer that goto returns to has to exist before the procedures do.
    PImpl->openLabelScope(*prog.Block, /*programBlock=*/true);
    PImpl->emitAllProcedures(*prog.Block);
    // Issue #619: see ensureOwnedVmtsDefined's own comment (CodeGenImpl.h)
    // -- an object type declared directly by the program itself (not by
    // any unit) is always self-contained within this one translation unit,
    // so this is a belt-and-suspenders completeness match for the same
    // call every unit/module block gets, not a fix for any observed
    // cross-TU gap here.
    PImpl->ensureOwnedVmtsDefined(*prog.Block);

    // For module-only compilation units (no program body), skip emitting
    // main() so the object file can be linked with a separate program object.
    bool IsModuleOnly = prog.Block->Body == nullptr && !prog.OwnedModules.empty();
    if (!IsModuleOnly)
        PImpl->emitMain(*prog.Block, prog.FileParams, InitModules);
    else
        PImpl->closeLabelScope(); // main, which would have closed it, is absent
    PImpl->popScope();

    // -g: construct whatever deferred debug-info nodes DIBuilder collected
    // (e.g. forward-declared types) before the module is inspected by
    // anything else.  A no-op when Debug is unset.  Also writes the -g
    // schema sidecar; a failure there has already been diagnosed to stderr
    // (issue #396), so fail the whole compile here rather than go on to
    // verify/optimize/emit and report success for a sidecar
    // plang_schema_printers.py cannot actually load -- the same "fail loud"
    // contract the verifyModule failure just below already has.
    if (!PImpl->dbgInfo_->finalize())
        return false;

    // Verify the module before emitting — a failed verify indicates a codegen
    // bug and must not produce output that downstream tools would accept.
    // Verify before optimizing, so a failure names our bug rather than
    // whatever the pipeline made of it.
    std::string errs;
    llvm::raw_string_ostream errStream(errs);
    if (llvm::verifyModule(*PImpl->mod, &errStream)) {
        std::cerr << "plang: internal error: LLVM IR verification failed\n"
                  << errs << "\n";
        return false;
    }

    PImpl->optimize();

    llvm::raw_os_ostream llvmOs(os);
    PImpl->mod->print(llvmOs, nullptr);
    return true;
}

// ---------------------------------------------------------------------------
// Turbo Tier 4, Cluster A item 2: standalone unit codegen
// ---------------------------------------------------------------------------

bool Codegen::emitUnit(const UnitNode& Unit, std::ostream& os) {
    PImpl->init(Unit.Name);
    PImpl->pushScope();
    PImpl->emitPredefinedGlobals();

    const std::string UnitLower = toLower(Unit.Name);
    PImpl->currentUnit_ = UnitLower;
    PImpl->namePrefix   = PlangProcPrefix   + UnitLower + PlangScopeSep;
    PImpl->globalPrefix = PlangGlobalPrefix + UnitLower + PlangScopeSep;

    // What this unit's OWN 'uses' (interface and implementation alike --
    // setUsedUnits must have been called with both lists' worth of UnitNodes
    // before this) declares: its constants (folded), variables (extern
    // globals) and procedure/function headings (extern declarations) --
    // see registerUsedUnitConsts' own comment for exactly what this does.
    // Registered before this unit's own globals, so a same-named
    // declaration of its own still overwrites it (emitGlobals' own loop
    // overwrites `consts` unconditionally) -- the same last-uses-wins /
    // own-declaration-wins precedence Sema's own scope stack already gives
    // these identifiers.
    PImpl->registerUsedUnitConsts();

    // Interface declarations first (so the implementation, which may
    // reference them, resolves them the same way Sema's own checkUnit gave
    // the implementation the interface's declarations already in scope),
    // then the implementation's own.  No emitInheritedGlobals-style merge is
    // needed the way an EP module's Body/heading pair needs one: a Turbo
    // unit's InterfaceBlock and ImplementationBlock are two genuinely
    // disjoint declaration lists (Sema rejects redeclaring an interface name
    // in the implementation), so simply emitting both in turn cannot
    // collide.
    if (Unit.InterfaceBlock)      PImpl->emitGlobals(*Unit.InterfaceBlock);
    if (Unit.ImplementationBlock) PImpl->emitGlobals(*Unit.ImplementationBlock);

    // Interface procedure/function HEADINGS are forward declarations for
    // the real definitions the implementation supplies under the same
    // names (UnitNode::InterfaceBlock's own comment) -- declared first,
    // exactly like emitAllProcedures' own forward-declare pass, so that
    // emitAllProcedures(ImplementationBlock) below finds and completes the
    // SAME llvm::Function (found by its mangled name) rather than defining
    // a second, colliding one.
    if (Unit.InterfaceBlock)
        for (const auto& Proc : Unit.InterfaceBlock->Procs)
            PImpl->emitFunctionDef(*Proc, /*declareOnly=*/true);
    // Issue #618: an ABSTRACT method declared on an object type in this
    // unit's own INTERFACE section (the ordinary place to declare one
    // meant to be used from another translation unit) gets its real,
    // defined trap-body stub here too -- emitAllProcedures's own identical
    // pass, just below, only ever sees ImplementationBlock's own types, so
    // without this call an interface-declared abstract method's stub was
    // never emitted in this unit's own object file at all.  Before
    // emitAllProcedures(ImplementationBlock), exactly like
    // ensureAbstractStubsDefined's own comment (CodeGenImpl.h) explains: a
    // method call inside THIS unit's own implementation body may already
    // need this stub's mangled name to resolve.
    if (Unit.InterfaceBlock)
        PImpl->ensureAbstractStubsDefined(*Unit.InterfaceBlock);
    if (Unit.ImplementationBlock)
        PImpl->emitAllProcedures(*Unit.ImplementationBlock);

    // Issue #619: every object type this UNIT declares (interface or
    // implementation section alike -- see ensureOwnedVmtsDefined's own
    // comment, CodeGenImpl.h) must get its real VMT definition from THIS
    // compile, regardless of whether this unit's own code happens to
    // instantiate or reference one itself.  After both emitAllProcedures
    // calls just above (and the interface heading pre-pass before them),
    // so every LOCAL method a slot could need is already at least
    // declared.
    if (Unit.InterfaceBlock)      PImpl->ensureOwnedVmtsDefined(*Unit.InterfaceBlock);
    if (Unit.ImplementationBlock) PImpl->ensureOwnedVmtsDefined(*Unit.ImplementationBlock);

    PImpl->emitUnitInitFn(Unit);

    PImpl->currentUnit_.clear();
    PImpl->namePrefix   = PlangProcPrefix;
    PImpl->globalPrefix = PlangGlobalPrefix;
    PImpl->popScope();

    if (!PImpl->dbgInfo_->finalize())
        return false;

    std::string errs;
    llvm::raw_string_ostream errStream(errs);
    if (llvm::verifyModule(*PImpl->mod, &errStream)) {
        std::cerr << "plang: internal error: LLVM IR verification failed\n"
                  << errs << "\n";
        return false;
    }

    PImpl->optimize();

    llvm::raw_os_ostream llvmOs(os);
    PImpl->mod->print(llvmOs, nullptr);
    return true;
}
