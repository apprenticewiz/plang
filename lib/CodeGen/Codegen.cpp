// Codegen.cpp — LLVM IR generation using the LLVM C++ API.
//
// No exceptions are thrown or caught in this file; errors are emitted as
// IR comments and the module is printed regardless.

#include "CodegenImpl.h"

#include "plang/Basic/Arith.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"

#include <iostream>

// The value of a constant expression, or nothing when it is not one this can
// work out.  'known' maps lowercase names to LLVM Values (may be null).
//
// Absence is spelled as absence rather than as a number: an array bound that
// did not fold used to come back as the caller's fallback, and a fallback of 0
// makes `array [0..n]` a perfectly ordinary one-element range that nothing
// downstream can tell from a real one.
std::optional<int64_t> tryEvalConstInt(
        const ExprNode& e,
        const std::unordered_map<std::string, llvm::Value*>* known) {
    // R2.  Sema folded this expression in the scope it was WRITTEN in, and
    // that is the answer.  Everything below re-folds it here, against a table
    // holding whatever is innermost where the expression is being lowered --
    // a different question the moment a name is redeclared between the two
    // points, and the source of every finding in class A of
    // docs/single-source-of-truth.md.
    //
    // Sema does not record a value it folded against a schema's probe binding,
    // so a bound over a discriminant still falls through to the code below and
    // is emitted against the discriminants actually in hand.
    if (e.ConstVal) return *e.ConstVal;
    if (auto* n = llvm::dyn_cast<IntLitExpr>(&e))  return n->Value;
    if (auto* n = llvm::dyn_cast<BoolLitExpr>(&e)) return n->Value ? 1 : 0;
    // ISO §6.1.7: a one-character string is a char-type constant, so it is an
    // ordinal and may appear as an array or subrange bound.
    if (auto* n = llvm::dyn_cast<StringLitExpr>(&e))
        if (n->Value.size() == 1)
            return static_cast<int64_t>(
                static_cast<unsigned char>(n->Value[0]));
    if (auto* n = llvm::dyn_cast<IdentExpr>(&e)) {
        if (known) {
            auto it = known->find(toLower(n->Name));
            if (it != known->end())
                if (auto* ci = llvm::dyn_cast_or_null<llvm::ConstantInt>(it->second))
                    // A char constant is held as i8 and its ordinal is 0..255;
                    // Pascal has no negative char.  Reading every entry back
                    // sign-extended made `maxchar` fold to -1 here while Sema
                    // folded it to 255.  Latent rather than live -- a char
                    // bound pairs with another char, so -1 always produced a
                    // non-positive extent and tripped the "did not fold"
                    // fallback into Sema's answer -- but a landmine for any
                    // future caller without that guard.
                    return ci->getBitWidth() == 8
                               ? static_cast<int64_t>(ci->getZExtValue())
                               : ci->getSExtValue();
        }
        return std::nullopt;
    }
    if (auto* n = llvm::dyn_cast<UnaryExpr>(&e)) {
        if (n->Op == TokenKind::Minus)
            if (auto v = tryEvalConstInt(*n->Operand, known)) return -*v;
        if (n->Op == TokenKind::Plus)
            return tryEvalConstInt(*n->Operand, known);
        return std::nullopt;
    }
    if (auto* n = llvm::dyn_cast<BinaryExpr>(&e)) {
        const auto l = tryEvalConstInt(*n->Left,  known);
        const auto r = tryEvalConstInt(*n->Right, known);
        if (!l || !r) return std::nullopt;
        switch (n->Op) {
        case TokenKind::Plus:  return *l + *r;
        case TokenKind::Minus: return *l - *r;
        case TokenKind::Times: return *l * *r;
        case TokenKind::Div:   return *r ? std::optional{*l / *r} : std::nullopt;
        case TokenKind::Mod:   return *r ? std::optional{isoMod(*l, *r)}
                                         : std::nullopt;
        default:               return std::nullopt;
        }
    }
    return std::nullopt;
}

int64_t evalConstInt(const ExprNode& e, int64_t fallback,
                     const std::unordered_map<std::string, llvm::Value*>* known) {
    return tryEvalConstInt(e, known).value_or(fallback);
}

// EP §6.8.2: evaluate a nonvarying (constant) expression to an LLVM Constant.
// Returns null if the expression cannot be folded at compile time (e.g. variable
// references, function calls).  Previously-defined constants are resolved via
// the 'known' map (lowercase name -> llvm::Constant*).
llvm::Constant* evalConst(
        const ExprNode& e,
        const std::unordered_map<std::string, llvm::Value*>& known,
        llvm::LLVMContext& ctx,
        llvm::IntegerType* i64Ty,
        llvm::Type* dblTy) {
    using llvm::dyn_cast;

    if (auto* n = dyn_cast<IntLitExpr>(&e))
        return llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(n->Value), true);
    if (auto* n = dyn_cast<RealLitExpr>(&e))
        return llvm::ConstantFP::get(dblTy, n->Value);
    if (auto* n = dyn_cast<BoolLitExpr>(&e))
        return llvm::ConstantInt::getBool(ctx, n->Value);

    if (auto* n = dyn_cast<IdentExpr>(&e)) {
        auto it = known.find(toLower(n->Name));
        if (it != known.end())
            return dyn_cast<llvm::Constant>(it->second);
        return nullptr;
    }

    if (auto* n = dyn_cast<UnaryExpr>(&e)) {
        auto* vc = evalConst(*n->Operand, known, ctx, i64Ty, dblTy);
        if (!vc || n->Op != TokenKind::Minus) return nullptr;
        if (auto* vi = dyn_cast<llvm::ConstantInt>(vc))
            return llvm::ConstantInt::get(i64Ty,
                static_cast<uint64_t>(-vi->getSExtValue()), true);
        if (auto* vf = dyn_cast<llvm::ConstantFP>(vc))
            return llvm::ConstantFP::get(dblTy,
                -vf->getValueAPF().convertToDouble());
        return nullptr;
    }

    if (auto* n = dyn_cast<BinaryExpr>(&e)) {
        auto* lc = evalConst(*n->Left,  known, ctx, i64Ty, dblTy);
        auto* rc = evalConst(*n->Right, known, ctx, i64Ty, dblTy);
        if (!lc || !rc) return nullptr;

        // Integer × Integer
        auto* li = dyn_cast<llvm::ConstantInt>(lc);
        auto* ri = dyn_cast<llvm::ConstantInt>(rc);
        if (li && ri) {
            int64_t l = li->getSExtValue(), r = ri->getSExtValue();
            switch (n->Op) {
            case TokenKind::Plus:  return llvm::ConstantInt::get(i64Ty, l + r, true);
            case TokenKind::Minus: return llvm::ConstantInt::get(i64Ty, l - r, true);
            case TokenKind::Times: return llvm::ConstantInt::get(i64Ty, l * r, true);
            case TokenKind::Div:   return r ? llvm::ConstantInt::get(i64Ty, l / r, true) : nullptr;
            case TokenKind::Mod:   return r ? llvm::ConstantInt::get(i64Ty, isoMod(l, r), true) : nullptr;
            default:               return nullptr;
            }
        }

        // Real × Real (with widening from int)
        auto toDouble = [](llvm::Constant* c) -> std::optional<double> {
            if (auto* cf = dyn_cast<llvm::ConstantFP>(c))
                return cf->getValueAPF().convertToDouble();
            if (auto* ci = dyn_cast<llvm::ConstantInt>(c))
                return static_cast<double>(ci->getSExtValue());
            return std::nullopt;
        };
        auto lv = toDouble(lc), rv = toDouble(rc);
        if (lv && rv) {
            switch (n->Op) {
            case TokenKind::Plus:   return llvm::ConstantFP::get(dblTy, *lv + *rv);
            case TokenKind::Minus:  return llvm::ConstantFP::get(dblTy, *lv - *rv);
            case TokenKind::Times:  return llvm::ConstantFP::get(dblTy, *lv * *rv);
            case TokenKind::Divide: return llvm::ConstantFP::get(dblTy, *lv / *rv);
            default:                return nullptr;
            }
        }
        return nullptr;
    }
    return nullptr;
}

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
    PImpl->init(prog.Name);
    PImpl->labelBlocks.clear();

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
