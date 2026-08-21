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

void Codegen::setSourceManager(const SourceManager& SM, FileID MainFile) {
    PImpl->srcMgr_     = &SM;
    PImpl->mainFileID_ = MainFile;
}

bool Codegen::emit(const ProgramNode& prog, std::ostream& os) {
    PImpl->init(prog.Name); // gotoEngine_ is freshly constructed here, with
                            // an empty label map already -- nothing left to clear.

    PImpl->pushScope(); // global scope

    // EP §6.11: emit module bodies (globals + procedures) before the program.
    //
    // Each body gets its own naming scope and its own mangling prefix.  A
    // module is an outer scope in the same sense a procedure is, so what it
    // declares is mangled with its name; two modules may each export an `f`,
    // and before this they collided on the single symbol `plang_f`.  The scope
    // keeps them apart on the Pascal side to match: the program reaches an
    // imported name through its import clauses, not by finding whatever
    // happened to be emitted last.
    std::vector<std::string> InitModules;
    for (auto* Mod : prog.Modules) {
        if (Mod->IsInterface) continue;
        const std::string Unit = toLower(Mod->Name);
        PImpl->pushScope();
        PImpl->currentUnit_  = Unit;
        PImpl->namePrefix    = PlangProcPrefix   + Unit + PlangScopeSep;
        PImpl->globalPrefix  = PlangGlobalPrefix + Unit + PlangScopeSep;
        PImpl->moduleIfaceBlock_ = nullptr;
        if (Mod->Body) {
            PImpl->emitGlobals(*Mod->Body);
            // EP §6.11.1: whatever the module's heading declares is the
            // block's too, and this module is where it lives.
            for (auto* Iface : prog.Modules)
                if (Iface->IsInterface && Iface->Body
                        && eqCI(Iface->Name, Mod->Name)) {
                    PImpl->moduleIfaceBlock_ = Iface->Body.get();
                    PImpl->emitInheritedGlobals(*Iface->Body, *Mod->Body);
                    break;
                }
            PImpl->emitAllProcedures(*Mod->Body);
        }
        // The lifecycle blocks read the module's own variables, so they are
        // emitted here, while its scope is still standing.  The finaliser goes
        // first because the initialiser ends by registering it.
        if (Mod->FinalStmt)
            PImpl->emitModuleLifecycleFn("__plang_fini_" + Unit,
                                              *Mod->FinalStmt);
        PImpl->emitModuleInitFn(*Mod);
        InitModules.push_back(Mod->Name);
        PImpl->moduleIfaceBlock_ = nullptr;
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

    // EP §6.11: a type an imported interface declares is one this unit lays
    // out and, where the interface says so, initialises — and the declaration
    // it does that from is the one read back from the .pmi.  The program's own
    // declarations come after, so a name it declares itself stays its own.
    for (const auto* Iface : PImpl->loadedInterfaces_)
        if (Iface->Body)
            PImpl->registerInterfaceTypes(*Iface->Body, toLower(Iface->Name));

    // A routine of an imported module is called with whatever hidden arguments
    // its parameters ask for — the bounds of a conformant array, the
    // discriminants of a schema, the frame of a procedural parameter.  Those
    // are read off the heading, so the headings the interface files carry are
    // declared here; a call site that found no declaration would invent one
    // from the shape of the argument list and pass an array where the module
    // reads a pointer and a bound.
    for (const auto* Iface : PImpl->loadedInterfaces_) {
        if (!Iface->Body) continue;
        PImpl->namePrefix = PlangProcPrefix + toLower(Iface->Name) + PlangScopeSep;
        for (const auto& Proc : Iface->Body->Procs)
            PImpl->emitFunctionDef(*Proc, /*declareOnly=*/true);
    }
    PImpl->namePrefix = PlangProcPrefix;

    PImpl->emitFileParams(prog.FileParams);
    PImpl->emitGlobals(*prog.Block);
    // ISO §6.8.1: a procedure may goto a label of the program's block, so the
    // buffer that goto returns to has to exist before the procedures do.
    PImpl->openLabelScope(*prog.Block, /*programBlock=*/true);
    PImpl->emitAllProcedures(*prog.Block);

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
    // anything else.  A no-op, not a null check away, when Debug is unset.
    if (PImpl->DBuilder) PImpl->DBuilder->finalize();

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
