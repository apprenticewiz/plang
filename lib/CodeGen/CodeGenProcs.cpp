#include <set>
#include "CodeGenImpl.h"
using namespace plang;

// ====================================================================
// Globals
// ====================================================================

// Program file-parameters (ISO §6.10) are registered by Sema as text file
// variables, so they need matching module-level storage or any use of them
// fails to resolve during lowering.
void Codegen::Impl::emitFileParams(const std::vector<std::string>& names) {
    for (const auto& nm : names) {
        // -std=turbo only: 'input'/'output' already have shared storage
        // from emitPredefinedGlobals (the runtime's own plang_input/
        // plang_output, called before this from Codegen::emit) -- a
        // program that still writes the legal-but-unusual ISO heading
        // `program p(input, output);` under Turbo must not get a SECOND,
        // per-object global here, which would silently detach this
        // object's own 'input'/'output' from the shared storage every
        // other Turbo mechanism (plang_bind_std, Assign/Reset/Rewrite)
        // reads and writes -- see Sema::registerBuiltins' Input/Output
        // comment for the matching reconciliation on the Sema side.
        if (langOpts.turbo()) {
            const std::string lo = toLower(nm);
            if (lo == "input" || lo == "output") continue;
        }
        auto* ty   = fileStructType();
        auto* zero = llvm::Constant::getNullValue(ty);
        auto* gv   = new llvm::GlobalVariable(*mod, ty, /*isConst=*/false,
                                               llvm::GlobalValue::ExternalLinkage,
                                               zero, PlangGlobalPrefix + nm);
        defVar(nm, gv, ty, /*typeNode=*/nullptr);
    }
}

// -std=turbo only: see this method's own declaration (CodeGenImpl.h) and
// Sema::registerBuiltins' comment on the ExitCode Symbol for the whole
// design.  Declares, rather than defines: the actual storage is the
// runtime's own plang_tp_exitcode (runtime/plang_sys.cpp), one definition
// shared by every object a Turbo compilation produces -- the main file and
// any extraInputFiles, each compiled by its own Codegen::emit() call.
// Passing no Initializer to GlobalVariable's constructor is what keeps this
// a declaration rather than a second definition that would collide with
// the runtime's at link time; registerInterfaceTypes, just above, declares
// a module's own externally-defined global the same way.
void Codegen::Impl::emitPredefinedGlobals() {
    if (!langOpts.turbo()) return;
    auto* ty = llvm::Type::getIntNTy(ctx, langOpts.defaultIntWidth());
    auto* gv = new llvm::GlobalVariable(*mod, ty, /*isConst=*/false,
                                         llvm::GlobalValue::ExternalLinkage,
                                         /*Initializer=*/nullptr, "plang_tp_exitcode");
    defVar("ExitCode", gv, ty, /*typeNode=*/nullptr);

    // RandSeed -- declared (never defined) exactly like ExitCode just above;
    // see Sema::registerBuiltins' RandSeed comment for the whole mechanism.
    // Fixed i32, NOT langOpts.defaultIntWidth(): real Turbo Pascal's own
    // RandSeed is a LongInt regardless of Integer's own dialect width, and
    // Sema::registerBuiltins gives the RandSeed Symbol Ctx_.getInt(32, true)
    // rather than TyInt for the identical reason -- both sides have to agree
    // on 32 bits the same way ExitCode's two sides agree on Integer's width.
    auto* seedTy = llvm::Type::getInt32Ty(ctx);
    auto* seedGv = new llvm::GlobalVariable(*mod, seedTy, /*isConst=*/false,
                                         llvm::GlobalValue::ExternalLinkage,
                                         /*Initializer=*/nullptr, "plang_tp_randseed");
    defVar("RandSeed", seedGv, seedTy, /*typeNode=*/nullptr);

    // HeapError/ExitProc/ErrorAddr: Turbo procedural VALUES' own storage is
    // a flat `ptr` slot exactly like a user-declared procedural variable's
    // (VarEntry::isProcVar's own comment; ErrorAddr is an ordinary Pointer,
    // the same shape either way) -- but typeNode is null here, same as
    // ExitCode just above, so isProcVar/procType are never set: neither
    // HeapError nor ExitProc is ever CALLED from Pascal source (see
    // Sema::registerBuiltins' own comment on why), only ASSIGNED to and read
    // back, and neither of those consults isProcVar/procType at all -- only
    // an indirect CALL through the variable would (ClosureAndCallABI::
    // emitProcVarCall), which this deliberately never emits for either.
    static constexpr std::pair<const char*, const char*> PtrPredefinedGlobals[] = {
        {"HeapError", "plang_tp_heaperror"},
        {"ExitProc",  "plang_tp_exitproc"},
        {"ErrorAddr", "plang_tp_erroraddr"},
    };
    for (const auto& [PascalName, LinkName] : PtrPredefinedGlobals) {
        auto* g = new llvm::GlobalVariable(*mod, ptrTy, /*isConst=*/false,
                                            llvm::GlobalValue::ExternalLinkage,
                                            /*Initializer=*/nullptr, LinkName);
        defVar(PascalName, g, ptrTy, /*typeNode=*/nullptr);
    }

    // FileMode: see Sema::registerBuiltins' FileMode Symbol comment for the
    // whole design -- this is exactly ExitCode's own mechanism just above,
    // reused rather than reinvented.  A declaration, not a definition: the
    // one real plang_tp_filemode lives in runtime/plang_sys.cpp, shared by
    // every object a Turbo compilation produces.
    auto* fmGv = new llvm::GlobalVariable(*mod, ty, /*isConst=*/false,
                                           llvm::GlobalValue::ExternalLinkage,
                                           /*Initializer=*/nullptr, "plang_tp_filemode");
    defVar("FileMode", fmGv, ty, /*typeNode=*/nullptr);

    // InOutRes: see Sema::registerBuiltins' InOutRes Symbol comment for the
    // whole design -- ExitCode's own mechanism again, but at a fixed i64
    // rather than langOpts.defaultIntWidth(), the same reason RandSeed's
    // seedTy just above is a fixed i32 rather than `ty`: the one real
    // plang_tp_inoutres (runtime/plang_sys.cpp) is int64_t regardless of
    // Integer's own dialect width, a deliberate divergence from Borland's
    // 16-bit Word documented at length on that global's own definition.
    auto* iorTy = llvm::Type::getInt64Ty(ctx);
    auto* iorGv = new llvm::GlobalVariable(*mod, iorTy, /*isConst=*/false,
                                            llvm::GlobalValue::ExternalLinkage,
                                            /*Initializer=*/nullptr, "plang_tp_inoutres");
    defVar("InOutRes", iorGv, iorTy, /*typeNode=*/nullptr);

    // Input/Output: see Sema::registerBuiltins' own comment on these two
    // Symbols for the whole design -- ExitCode's exact "one real definition
    // in the runtime (runtime/plang_sys.cpp's plang_input/plang_output),
    // every compiled Turbo object only DECLARES it" mechanism, but under
    // fileStructType() (a whole PascalFile struct) rather than a scalar
    // LLVM type like every other entry in this function.  typeNode is
    // null, exactly like emitFileParams gives ISO/EP's own 'input'/
    // 'output' storage just below (there is no Sema-type table CodeGen can
    // hand a real FileTypeNode from here, and nothing downstream needs
    // one -- FileVarHelpers::isFileVar's own fallback to `ve->type ==
    // fileStructType()` is what a null typeNode still resolves through).
    auto* fileTy = fileStructType();
    auto* inGv = new llvm::GlobalVariable(*mod, fileTy, /*isConst=*/false,
                                           llvm::GlobalValue::ExternalLinkage,
                                           /*Initializer=*/nullptr, "plang_input");
    defVar("Input", inGv, fileTy, /*typeNode=*/nullptr);
    auto* outGv = new llvm::GlobalVariable(*mod, fileTy, /*isConst=*/false,
                                            llvm::GlobalValue::ExternalLinkage,
                                            /*Initializer=*/nullptr, "plang_output");
    defVar("Output", outGv, fileTy, /*typeNode=*/nullptr);
}

// Attaches 'input' and 'output' to the standard streams.  Any other
// file-parameter is left closed, so rewrite/reset give it temporary storage
// like an internal file.
//
// -std=turbo: a no-op for 'input'/'output' regardless of what this
// program's own heading lists -- emitTurboStdIOBinds, just below, is what
// binds THOSE two under Turbo, unconditionally, since Turbo needs them
// bound whether or not the heading ever mentions them at all.  Binding
// them here too (this loop's ISO/EP behavior, unchanged below) would just
// be a second, redundant plang_bind_std call on the exact same shared
// storage -- harmless, but this skip keeps the "who binds Input/Output
// under Turbo" answer in exactly one place.
void Codegen::Impl::emitFileParamBinds(const std::vector<std::string>& names) {
    if (langOpts.turbo()) return;
    for (const auto& nm : names) {
        std::string lo = toLower(nm);
        if (lo != "input" && lo != "output") continue;
        auto* ve = findVar(nm);
        if (!ve) codegenICE("file-parameter '" + nm + "' has no storage");
        auto* fn = getExternFnN("plang_bind_std",
            llvm::Type::getVoidTy(ctx), {ptrTy, i8Ty});
        builder.CreateCall(fn, {ve->ptr,
            llvm::ConstantInt::get(i8Ty, lo == "input" ? 1 : 0)});
    }
}

// -std=turbo only: binds the predefined Input/Output variables
// (Sema::registerBuiltins, emitPredefinedGlobals just above) to
// stdin/stdout, unconditionally -- whether or not this program's own
// heading lists 'input'/'output' at all (see emitFileParamBinds' own
// comment for why that loop skips them entirely under Turbo instead).
// Reuses plang_bind_std, the exact runtime entry point ISO/EP's own
// file-parameter mechanism already calls for the identical purpose.
void Codegen::Impl::emitTurboStdIOBinds() {
    if (!langOpts.turbo()) return;
    auto* fn = getExternFnN("plang_bind_std", llvm::Type::getVoidTy(ctx), {ptrTy, i8Ty});
    auto bind = [&](const char* nm, int8_t isInput) {
        auto* ve = findVar(nm);
        if (!ve) codegenICE(std::string("predefined '") + nm + "' has no storage");
        builder.CreateCall(fn, {ve->ptr, llvm::ConstantInt::get(i8Ty, isInput)});
    };
    bind("Input", 1);
    bind("Output", 0);
}

// The value a named constant stands for, or null when it needs a basic block
// to compute (an EP general constant expression).
llvm::Value* Codegen::Impl::constantValueOf(const ConstDef& cd) {
    if (auto* n = llvm::dyn_cast<StringLitExpr>(cd.Value.get())) {
        // Written with quotes, but a one-character literal is a char, and a
        // longer one is a string — the same split emitExpr makes for a literal
        // appearing directly in an expression.
        if (n->Value.size() == 1)
            return llvm::ConstantInt::get(i8Ty,
                static_cast<unsigned char>(n->Value[0]));
        // ISO §6.3: the constant stands for the value, so the name has to
        // emit what writing the literal in its place would have emitted.  The
        // {length, bytes} struct is how EP's string(n) is held; an ISO
        // string-type is the bytes alone, and reading one as the other found
        // the length where the first character should have been.
        return exprIsVarStr(*cd.Value) ? internStrStruct(n->Value)
                                       : internStrPtr(n->Value);
    }
    // R2.  Sema folded this in the scope it was written in, and it folds more
    // than codegen's evaluator does -- `const k = 2 pow 3` reaches here as an
    // expression evalConst cannot handle at all.  That used to fabricate 0 and
    // the program carried on with a constant of the wrong value; making the
    // failure loud is what turned it up.
    //
    // Only for an ordinal-typed constant: ConstVal is an int64_t, and a real or
    // a string constant still needs evalConst below.
    if (cd.Value->ConstVal && cd.Value->ResolvedType
            && cd.Value->ResolvedType->isOrdinal())
        return llvm::ConstantInt::get(
            llvmTypeOfSemaType(*cd.Value->ResolvedType), 
            static_cast<uint64_t>(*cd.Value->ConstVal), /*isSigned=*/true);
    return evalConst(*cd.Value, consts, ctx, i64Ty, dblTy);
}

// EP §6.11.1: a module's block is given the declarations of its heading, so
// what the heading declares and the block does not still belongs to this
// module and needs its storage and its names here.  A block that declares one
// of them again — the way plang's modules were written before a heading could
// declare for them — keeps its own, which is the one everything else in the
// block already refers to.
void Codegen::Impl::emitInheritedGlobals(const BlockNode& iface,
                                          const BlockNode& own) {
    auto declaredInOwn = [&](const std::string& nm) {
        for (const auto& vg : own.Vars)
            for (const auto& n : vg.Names)
                if (eqCI(n, nm)) return true;
        return false;
    };

    for (const auto& td : iface.Types) {
        if (typeAliases.count(toLower(td.Name))) continue;
        typeAliases[toLower(td.Name)] = td.Type.get();
        registerEnumValues(td.Type.get());
    }
    // EP §6.4.7: a schema the heading declares is the same schema inside the
    // block, and nothing of that type can be laid out without its
    // discriminants — the module could not compile its own exported schema.
    registerSchemaDefs(iface);
    for (const auto& cd : iface.Consts) {
        // A required constant's name may be declared again, and then it is
        // the declaration that stands, not the constant the language gives.
        if (consts.count(toLower(cd.Name)) && !isRequiredConst(toLower(cd.Name)))
            continue;
        // EP §6.8.7: a whole array, record or set lives in storage, and the
        // module whose heading declares it is the one that gives it that
        // storage; an importer only refers to what is defined here.
        if (llvm::isa<StructuredValueExpr>(cd.Value.get())) {
            if (!findVar(cd.Name)) emitStructuredConst(cd);
            continue;
        }
        if (llvm::Value* cv = constantValueOf(cd)) defineConst(cd.Name, cv);
        else if (!findVar(cd.Name))               emitRuntimeConst(cd);
    }
    for (const auto& vg : iface.Vars) {
        registerEnumValues(vg.Type.get());
        llvm::Type* ty = llvmTypeOf(vg.Type.get(), nullptr);
        for (const auto& nm : vg.Names) {
            if (declaredInOwn(nm)) continue;
            auto* gv = new llvm::GlobalVariable(
                *mod, ty, /*isConst=*/false, llvm::GlobalValue::ExternalLinkage,
                llvm::Constant::getNullValue(ty), globalPrefix + nm);
            defVar(nm, gv, ty, vg.Type.get());
            if (!currentUnit_.empty())
                moduleGlobals_[currentUnit_ + "." + toLower(nm)] =
                    VarEntry{ gv, ty, vg.Type.get() };
        }
    }
}

// The constants and types an interface declares, so that a unit which only
// imports them can fold the one, lay out the other, and start its variables
// where they are to start.
void Codegen::Impl::registerInterfaceTypes(const BlockNode& iface,
                                           const std::string& unit) {
    // Constants first: a bound of an exported type is often one of them.
    for (const auto& cd : iface.Consts) {
        // A required constant's name may be declared again, and then it is
        // the declaration that stands, not the constant the language gives.
        if (consts.count(toLower(cd.Name)) && !isRequiredConst(toLower(cd.Name)))
            continue;
        if (llvm::isa<StructuredValueExpr>(cd.Value.get())) continue;
        if (llvm::Value* cv = constantValueOf(cd)) defineConst(cd.Name, cv);
    }
    for (const auto& td : iface.Types) {
        if (typeAliases.count(toLower(td.Name))) continue;
        typeAliases[toLower(td.Name)] = td.Type.get();
        registerEnumValues(td.Type.get());
    }
    // A schema read from an interface file is instantiated by the importer,
    // which needs its discriminants as much as its body.
    registerSchemaDefs(iface);

    // A variable the interface declares lives in the module's object file, and
    // is reached here through a declaration of the symbol.  It is recorded with
    // the type-denoter it was declared with: an index range, a string's
    // capacity and a file's element type are all written there and nowhere in
    // the LLVM type, so an importer without it read `array[1..2]` as 0..1.
    for (const auto& vg : iface.Vars) {
        if (!vg.Type) continue;
        llvm::Type* ty = llvmTypeOf(vg.Type.get(), nullptr);
        for (const auto& nm : vg.Names) {
            const std::string gname = PlangGlobalPrefix + unit + PlangScopeSep + nm;
            auto* gv = mod->getGlobalVariable(gname);
            if (!gv)
                gv = new llvm::GlobalVariable(*mod, ty, /*isConst=*/false,
                                              llvm::GlobalValue::ExternalLinkage,
                                              nullptr, gname);
            moduleGlobals_[unit + "." + toLower(nm)] =
                VarEntry{ gv, ty, vg.Type.get() };
        }
    }
}

// Turbo Tier 4, Cluster A item 1: see Codegen::setUsedUnits's own comment
// (CodeGen.h) for the original design.  Since item 2, a Turbo unit compiled
// through -c gets a real object file of its own (the same one item 1's
// temporary loader used to stand in for), so this is no longer the only
// path an imported declaration can codegen through.  Only SCALAR, FOLDABLE
// constants -- the one case that needs no storage at all, an immediate
// value baked directly into whoever reads it -- are registered explicitly
// here. A unit-exported variable, procedure/function, or a typed constant
// is left alone by THIS function on purpose, but is not left unhandled: TP
// typed constant: real static storage, not a foldable value, so Sema
// registers it as a SymbolKind::Var (Symbol::IsTypedConst) rather than a
// SymbolKind::Const -- the exact same kind an ordinary interface variable
// gets -- and CodeGenTypes.cpp's resolveImportedVar already declares an
// external global for any Var symbol with no same-compile-unit
// moduleGlobals_ entry, using the Sema-resolved type and the SAME mangled
// name (CGLinkage::mangledGlobal) the defining unit's own compile emitted
// storage under (CGTypedConst.cpp). That generic fallback is what makes an
// imported typed OR structured constant read back correctly with no
// separate registration loop needed here -- confirmed by round-tripping
// sized-int, string[N], record, and array typed constants end-to-end
// through a real -c/link/run cycle (Tier 4 Cluster A item 3).  A plain
// EP-only structured constant (no ConstDef::Type, StructuredValueExpr
// value) has never been reachable from a Turbo unit's own const section --
// checkStructuredValue's TypeName-driven resolution is EP-only grammar
// (see StructuredValueExpr's own comment) -- so it stays out of scope here.
void Codegen::Impl::registerUsedUnitConsts() {
    for (const UnitNode* Unit : usedUnits_) {
        if (!Unit->InterfaceBlock) continue;
        const std::string UnitLower = toLower(Unit->Name);
        for (const auto& cd : Unit->InterfaceBlock->Consts) {
            // TP typed constant: real static storage, handled generically
            // through resolveImportedVar instead -- see this function's own
            // comment above.
            if (cd.Type) continue;
            // A whole array/record/set value needs storage of its own too.
            if (llvm::isa<StructuredValueExpr>(cd.Value.get())) continue;
            llvm::Value* cv = constantValueOf(cd);
            if (!cv) continue; // does not fold at compile time; out of scope here
            // 'uses' order: a later-processed unit's same-named constant
            // simply overwrites an earlier one's, matching Sema's own
            // last-uses-wins scope-stack precedence exactly.
            defineConst(cd.Name, cv);
            // Explicit qualification: 'UnitName.Identifier' -- the parser
            // has already folded this into one dotted IdentExpr wherever
            // 'UnitName' is a name QualifiedModules_ knows about (every
            // 'uses'd unit; see ParseUnit.cpp's own comment), so a second,
            // never-overwritten entry under the dotted name is all CodeGen
            // needs: emitExpr's ordinary `Consts.find(toLower(n->Name))`
            // already handles it with no new lookup logic at all.
            defineConst(Unit->Name + "." + cd.Name, cv);
        }

        // Turbo Tier 4, Cluster A item 2: interface variables become
        // external globals, mangled pasg_<unit>$<name> -- CGLinkage's own
        // scheme (moduleScope/PlangGlobalPrefix), the SAME mangling
        // Codegen::emitUnit gives this unit's own real storage when IT is
        // compiled.  Declared (never defined) here, exactly like
        // registerInterfaceTypes does for an EP module read back from a
        // .pmi -- the actual storage exists in whichever object really
        // compiled this unit; this is only ever a reference to it.  A
        // program's own bare `V` resolving to this happens through
        // resolveImportedVar's existing, dialect-agnostic fallback
        // (CodeGenTypes.cpp): it already declares an external global by
        // mangledGlobal(name) whenever moduleGlobals_ has no same-compile-
        // unit entry, using CGLinkage::importOwner/importLinkName -- which
        // now resolve correctly for a Turbo 'uses' too, because
        // Sema::pushUnitUsesScopes populates ImportOwners_ for one exactly
        // as EP's own processImports already does.  Registering the global
        // explicitly here (rather than relying solely on that lazy fallback)
        // also makes 'UnitName.Identifier' qualification resolve without
        // any change to moduleGlobals_'s own "owner.bare" lookup shape.
        for (const auto& vg : Unit->InterfaceBlock->Vars) {
            if (!vg.Type) continue;
            llvm::Type* ty = llvmTypeOf(vg.Type.get(), nullptr);
            for (const auto& nm : vg.Names) {
                const std::string gname =
                    PlangGlobalPrefix + UnitLower + PlangScopeSep + nm;
                auto* gv = mod->getGlobalVariable(gname);
                if (!gv)
                    gv = new llvm::GlobalVariable(
                        *mod, ty, /*isConst=*/false,
                        llvm::GlobalValue::ExternalLinkage, nullptr, gname);
                moduleGlobals_[UnitLower + "." + toLower(nm)] =
                    VarEntry{ gv, ty, vg.Type.get() };
            }
        }

        // Interface procedure/function headings: declared (never defined)
        // under this unit's own mangling prefix, exactly like
        // Codegen::emit's own loadedInterfaces_ loop already does for an EP
        // module's .pmi-loaded interface (CodeGen.cpp) -- so that a CALL to
        // one (CGLinkage::findMangledProc, which computes this SAME mangled
        // name) finds a real llvm::Function to call rather than an internal
        // error.  namePrefix is restored immediately after, so nothing else
        // this function does (or anything the caller does afterward) is
        // affected by the swap.
        if (!Unit->InterfaceBlock->Procs.empty()) {
            const std::string SavedPrefix = namePrefix;
            namePrefix = PlangProcPrefix + UnitLower + PlangScopeSep;
            for (const auto& proc : Unit->InterfaceBlock->Procs)
                emitFunctionDef(*proc, /*declareOnly=*/true);
            namePrefix = SavedPrefix;
        }
    }
}

void Codegen::Impl::emitGlobals(const BlockNode& block) {
    // Register user-defined type aliases so llvmTypeOfName can resolve them.
    // These come first because a structured constant names one of them.
    for (const auto& td : block.Types) {
        typeAliases[toLower(td.Name)] = td.Type.get();
        registerEnumValues(td.Type.get());
    }

    // Register constants — evaluated in definition order so later constants
    // can reference earlier ones (EP §6.8.2 general constant expressions).
    for (const auto& cd : block.Consts) {
        // TP-only: a typed constant (ConstDef::Type set) is a real variable
        // with static storage and a compile-time initializer -- see
        // emitGlobalTypedConst's own comment -- so it takes neither of the
        // two paths below, which are both for an actual, unassignable
        // constant.
        if (cd.Type) {
            emitGlobalTypedConst(cd);
            continue;
        }
        // EP §6.8.7: a constant that is an array, record or set value is a
        // whole structure, so it needs storage rather than a value the way a
        // scalar constant does.  It is a global that no one may assign to,
        // laid down once before the program body runs.
        if (llvm::isa<StructuredValueExpr>(cd.Value.get())) {
            emitStructuredConst(cd);
            continue;
        }
        llvm::Value* cv = constantValueOf(cd);
        // EP §6.8.2: a value that has to be computed is computed where the
        // unit's code runs — in main for a program, and for a module in its
        // initialiser, which needs storage to leave the answer in.  This said
        // so and then only did it for a module: a `!currentUnit_.empty()`
        // guard here used to leave a PROGRAM's unfoldable constant (`const c
        // = cmplx(3.0, 4.0);` outside any module) with no fallback at all --
        // an internal error rather than the computed value the comment above
        // already promised.  R2's old fabricated-0 fallback was worse still
        // (see emitRuntimeConst's own history); the honest answer is to defer
        // to the value actually computed where the unit's code runs, which
        // for a program is now emitMain calling emitRuntimeConstInits() the
        // same way a module's initialiser already does.
        if (!cv) { emitRuntimeConst(cd); continue; }
        defineConst(cd.Name, cv);
    }
    for (const auto& vg : block.Vars) registerEnumValues(vg.Type.get());
    registerSchemaDefs(block);

    // Global variable declarations.
    for (const auto& vg : block.Vars) {
        llvm::Type* ty = llvmTypeOf(vg.Type.get(), nullptr);

        // TP-only: 'absolute' at program scope.  Sema (checkBlock's Phase 4)
        // has already required exactly one name and an addressable target;
        // what CodeGen still has to do is find that target's ADDRESS with no
        // builder to emit instructions through -- this runs before any
        // function (including main) exists, the same reason emitPredefinedGlobals
        // and friends never call emitLValue either.  A bare variable name
        // costs nothing to address (its VarEntry::ptr already IS the
        // address, whether a GlobalVariable or -- for a module -- an
        // imported declaration), so that one case is supported directly;
        // 'absolute' naming a COMPONENT of a global ('absolute B[2]') is not
        // yet, and is refused with an internal error rather than silently
        // doing the wrong thing.  The identical local case (emitBlockAllocas,
        // above) has no such limit: it runs with a real entry block already
        // open, so emitLValue there can address any designator.
        if (vg.AbsoluteExpr) {
            auto* id = llvm::dyn_cast<IdentExpr>(vg.AbsoluteExpr.get());
            const VarEntry* target = id ? findVar(id->Name) : nullptr;
            if (!target)
                codegenICE("'absolute' at program scope currently supports only "
                           "a bare variable name as its target, not a component "
                           "of one");
            defVar(vg.Names[0], target->ptr, ty, vg.Type.get());
            continue;
        }

        for (const auto& nm : vg.Names) {
            auto* zero = llvm::Constant::getNullValue(ty);
            auto* gv   = new llvm::GlobalVariable(*mod, ty, /*isConst=*/false,
                                                   llvm::GlobalValue::ExternalLinkage,
                                                   zero, globalPrefix + nm);
            defVar(nm, gv, ty, vg.Type.get());
            if (!currentUnit_.empty())
                moduleGlobals_[currentUnit_ + "." + toLower(nm)] =
                    VarEntry{ gv, ty, vg.Type.get() };
        }
    }
}

// ====================================================================
// Procedures and functions
// ====================================================================

void Codegen::Impl::emitAllProcedures(const BlockNode& block) {
    // ISO §6.6.1: a 'forward' declaration exists so that a procedure can be
    // called before its body appears, which is what makes mutual recursion
    // expressible.  Its signature therefore has to be in the module before any
    // body is emitted — otherwise the call site, finding no such function,
    // invents a declaration from the shape of the argument list, and the real
    // definition arrives to find the name taken.
    for (const auto& proc : block.Procs)
        if (proc->IsForward) emitFunctionDef(*proc, /*declareOnly=*/true);

    // Turbo Tier 5, Cluster A item 4: a method call's target is resolved by
    // Sema from the object type's own method table, independent of where --
    // or even whether yet -- its out-of-line body appears in this block, so
    // a method call may textually precede the body it calls.  Unlike an
    // ordinary procedure (which needs an explicit 'forward' for that), every
    // method heading gets its signature (and paramMeta_) registered before
    // ANY body in this block is emitted, exactly the way the 'forward'
    // pre-pass just above does.
    for (const auto& proc : block.Procs)
        if (!proc->OwnerType.empty()) emitFunctionDef(*proc, /*declareOnly=*/true);

    // Turbo Tier 5, Cluster A item 7: every ABSTRACT method declared by an
    // object type in this block gets a real, defined trap-body function
    // here -- see emitAbstractMethodStub's own comment for why an abstract
    // heading needs this dedicated pass rather than falling out of the
    // ordinary block.Procs loop just above (it is never IN block.Procs:
    // there is no out-of-line body, ever, for one).  Must run before any
    // body below is emitted, exactly like the method pre-pass just above,
    // since a method call or a VMT build reached from inside one of those
    // bodies may need this name to already resolve.
    for (const auto& td : block.Types) {
        if (!td.Type || !td.Type->ResolvedType
                || td.Type->ResolvedType->Kind != TypeKind::Object)
            continue;
        for (const auto& M : td.Type->ResolvedType->ObjectMethods)
            if (M.IsAbstract)
                emitAbstractMethodStub(M, td.Type->ResolvedType->Name);
    }

    for (const auto& proc : block.Procs) {
        if (proc->IsForward) continue;
        emitFunctionDef(*proc);
    }
}

// Turbo Tier 5, Cluster A item 7: see this method's own declaration
// (CodeGenImpl.h) for the whole design.
void Codegen::Impl::emitAbstractMethodStub(const Type::Method& M,
                                            const std::string& OwnerTypeName) {
    std::string mangledName = mangledMethod(OwnerTypeName, M.Name);
    if (mod->getFunction(mangledName)) return;  // already defined somehow

    // Self, then every declared parameter -- a 'var'/'const'-by-reference
    // parameter is a pointer, everything else its own value type, mirroring
    // (not sharing, since there is no ProcDecl here to hand the ordinary
    // signature-building path -- an abstract heading is never in
    // block.Procs, see this function's own header comment) the same shape
    // every other method's own signature already has. The exact ABI details
    // ordinary parameter marshaling worries about (struct-by-value
    // thresholds, sret, schema/conformant-array widening, ...) do not
    // matter here: this function is never linked against a real
    // implementation and never successfully returns a value to anything --
    // every caller that looks up this SAME mangled name reads ITS
    // FunctionType back off THIS declaration to marshal arguments, so all
    // that is required is internal self-consistency, not byte-for-byte
    // agreement with some other translation unit's own convention.
    std::vector<llvm::Type*> paramTys;
    paramTys.push_back(ptrTy);  // Self
    for (const auto& P : M.Params)
        paramTys.push_back(P.IsVar || P.IsConst ? ptrTy
                            : (P.Ty ? llvmTypeOfSemaType(*P.Ty) : ptrTy));
    llvm::Type* retTy = (M.IsFunction && M.RetType)
                             ? llvmTypeOfSemaType(*M.RetType)
                             : llvm::Type::getVoidTy(ctx);
    auto* fnTy = llvm::FunctionType::get(retTy, paramTys, /*isVarArg=*/false);
    auto* fn = llvm::Function::Create(fnTy, llvm::GlobalValue::InternalLinkage,
                                       mangledName, *mod);
    auto* entry = llvm::BasicBlock::Create(ctx, "entry", fn);
    llvm::IRBuilder<> stubB(entry);
    stubB.CreateCall(
        getExternFnN("plang_tp_runerror", llvm::Type::getVoidTy(ctx), {i64Ty}),
        {llvm::ConstantInt::get(i64Ty, 211, /*isSigned=*/true)});
    stubB.CreateUnreachable();
}

llvm::Value* Codegen::Impl::selfFieldPtr(llvm::Value* base, const Type& T,
                                          const std::string& fieldName,
                                          llvm::Type*& outTy) {
    const RecordLayout* L = layoutOfObject(T);
    if (!L) return nullptr;
    auto* zero = llvm::ConstantInt::get(i32Ty, 0);
    if (auto it = L->Fields.find(toLower(fieldName)); it != L->Fields.end()) {
        const auto& P = it->second;
        auto* fp = builder.CreateGEP(L->Ty, base,
                       {zero, llvm::ConstantInt::get(i32Ty, P.Index)},
                       "self." + fieldName);
        if (P.InVariant && P.Offset != 0)
            fp = builder.CreateConstGEP1_64(i8Ty, fp, P.Offset, "self." + fieldName);
        outTy = P.Ty;
        return fp;
    }
    if (!T.Parent) return nullptr;
    // Element 0 of every layoutOfObject struct is the nested ancestor
    // sub-object, unshifted (a struct's first element always starts at its
    // own byte offset 0), so recursing at the SAME address with the
    // ancestor's own struct type as the GEP's source type reaches its
    // fields at their real offsets.
    auto* parentPtr = builder.CreateGEP(L->Ty, base, {zero, zero},
                                        "self.parent");
    return selfFieldPtr(parentPtr, *T.Parent, fieldName, outTy);
}

void Codegen::Impl::emitFunctionDef(const ProcDecl& proc, bool declareOnly) {
    // Save outer context. CGFunction snapshots curFunc/curRetAlloca/
    // curRetType/curFuncName/namePrefix/typeAliases/consts/
    // requiredConsts/curFuncScopeDepth/the schema-defs snapshot (restored
    // unconditionally by its destructor, on every exit path including the
    // declareOnly early return below) and owns curStaticLink/
    // outerVarNames/outerVarBindings/valueConformantCopies_ outright,
    // fresh for this activation -- see its own declaration for why
    // ownership needs no save/clear/restore dance the way the four
    // locals it replaces used to.
    CGFunction curFn(*this);
    auto  savedIP         = builder.saveIP();
    // Restored automatically on every exit path (the declareOnly early
    // return below, or the normal end of this function) by the destructor --
    // see FunctionLabelScope's own comment for why this one field gets RAII
    // instead of a fourth save-and-two-manual-restores like its neighbors.
    LabelGotoEngine::FunctionLabelScope labelScope(*gotoEngine_);

    // Turbo Tier 5, Cluster A item 4: a method's out-of-line body
    // ('procedure T.M; ...', proc.OwnerType non-empty) mangles from its
    // OWNING type and method name (CGLinkage::mangledMethod), never from
    // namePrefix -- namePrefix walks LEXICAL nesting, and a method's
    // "enclosing scope" for mangling purposes is the object type it was
    // declared on, not whatever procedure or module textually surrounds the
    // 'procedure T.M; ...' declaration in the source (there usually is
    // none; every method body lives at the same block level its object
    // type's own declaration does).  isMethod is exactly Sema's own
    // ProcDecl::OwnerType-non-empty test (SymbolKind::Method's own comment).
    const bool isMethod = !proc.OwnerType.empty();
    if (isMethod && !proc.ResolvedOwnerType)
        codegenICE("method '" + proc.OwnerType + "." + proc.Name
                   + "' reached CodeGen with no resolved owning type -- "
                     "Sema::checkMethodBody should have set this or "
                     "reported a diagnostic that stopped CodeGen from "
                     "running at all");
    std::string mangledName = isMethod ? mangledMethod(proc.OwnerType, proc.Name)
                                       : namePrefix + proc.Name;
    namePrefix = mangledName + PlangScopeSep;
    curFuncName = proc.Name;

    // ISO §6.6.1: where the parameters and result type were written.  For the
    // defining occurrence of a forward-declared procedure that is the
    // declaration, whose heading this body does not repeat.
    const ProcDecl& hd = proc.heading();

    // Determine whether this is a nested procedure (has an outer function).
    // A method is never lexically nested the way this flag means (every
    // method body is walked from emitAllProcedures' own top-level
    // block.Procs loop, whatever block that is), so isNested and isMethod
    // are mutually exclusive in practice; nothing below assumes otherwise.
    bool isNested = (curFn.OuterFunc() != nullptr);

    // If nested, collect all outer-scope variables that this procedure may access.
    // We collect ALL visible outer variables (conservative; avoids escape analysis).
    std::vector<std::pair<std::string, VarEntry>> outerVars;
    std::vector<size_t>                           outerVarDepths;
    if (isNested) {
        // Walk the scope stack innermost-out, keeping each variable's DEPTH.
        // Two levels may declare the same name, and then the frame has two
        // slots spelled alike; without the depth there is nothing to tell them
        // apart at the call site, and both were filled with the innermost.
        for (size_t di = scopes.size(); di-- > 0;)
            for (const auto& [nm, ve] : scopes[di]) {
                outerVars.push_back({nm, ve});
                outerVarDepths.push_back(di);
            }
    }

    // Build LLVM parameter types.
    // For nested procedures, prepend an implicit ptr %static_link first.
    std::vector<llvm::Type*>      paramTypes;
    std::vector<std::string>      paramNames;
    std::vector<bool>             paramIsVar;
    std::vector<llvm::Type*>      paramValTypes;
    std::vector<const TypeNode*>  paramTypeNodes; // AST TypeNode for each param slot

    // Turbo Tier 5, Cluster A item 4: 'Self' -- an implicit leading pointer
    // parameter to the receiver's own storage, prepended the same way (and
    // for the same structural reason) a nested procedure's static link is
    // prepended just below: neither is a Pascal-declared parameter, and a
    // call site builds it separately from the Pascal-argument marshalling
    // loop (see CGFuncCall::emitMethodCallExpr / CGProcCall::
    // emitMethodCallStmt).  Pushed first, ahead of the static-link check,
    // though the two never actually co-occur (a method is never isNested --
    // see isNested's own comment just above).
    if (isMethod) paramTypes.push_back(ptrTy);

    if (isNested && !outerVars.empty())
        paramTypes.push_back(ptrTy); // static link (frame pointer from outer scope)

    // One entry per AST param position, recording everything a call site
    // needs about it; see Impl::ParamMeta.
    std::vector<ParamMeta> paramMeta;

    // Parallel to paramMeta: the schema type for each schematic param
    // (paramMeta[i].schemaDiscCount != 0).  Not part of ParamMeta itself --
    // used only locally below, in this same function, to size and type the
    // parameter's allocated storage; nothing outside emitFunctionDef reads a
    // schema parameter's Type* the way it reads the other facts.
    std::vector<const plang::Type*> schemaTypes;

    for (const auto& pg : hd.Params) {
        // Turbo untyped parameter (`procedure P(var x)`): pg.Type is
        // deliberately null (ParamGroup::Type's own comment), so every one
        // of the dyn_cast<...>(pg.Type.get())/*pg.Type derefs below would
        // either misfire or crash outright -- handled here, first, before
        // any of them run.  Always a plain incoming pointer: nothing beyond
        // an address ever flows through one (Sema rejects every use except
        // a variable typecast or a relay to another untyped formal --
        // SemaExpr.cpp's checkCallArgs/checkTypeCast), so there is no
        // element type to give it beyond an opaque ptr, and no copy to make.
        if (!pg.Type) {
            for (const auto& nm : pg.Names) {
                paramTypes.push_back(ptrTy);
                paramNames.push_back(nm);
                paramIsVar.push_back(true);
                paramValTypes.push_back(ptrTy);
                paramTypeNodes.push_back(nullptr);
                paramMeta.push_back(ParamMeta{ .byRef = true });
                schemaTypes.push_back(nullptr);
            }
            continue;
        }

        // ISO §6.6.3.1: a procedural parameter takes two pointers — the entry
        // point and the frame its body reads outer variables through.  Both
        // have to travel together: the frame is built from names visible where
        // the procedure is passed, which is not where it is finally called.
        //
        // pg.Type is not always literally a ProcedureTypeNode: Turbo Pascal's
        // only legal spelling for a procedural PARAMETER is a NAMED type
        // (`op: BinOp`, `type BinOp = function(...): Integer`) -- `fpc -Mtp`
        // rejects the inline `op: function(...): Integer` spelling in a
        // parameter list outright -- so pg.Type.get() is then a NamedTypeNode
        // whose Denotes chain has to be walked to reach the real
        // ProcedureTypeNode, exactly the way CGSymbolTable::defVar already
        // does for a procedural VARIABLE's declared type (resolveProcTypeAlias,
        // shared with it -- see that function's own comment, CGSymbolTable.h).
        // A bare llvm::dyn_cast on pg.Type.get() alone missed every named-type
        // case, leaving ParamMeta::procType null for that argument position and
        // letting the call site (CGCallMarshal::marshalArgs) fall through to
        // treating a bare routine-name actual as an implicit zero-argument call
        // instead of taking its address (issue #543).
        if (auto* pt = resolveProcTypeAlias(pg.Type.get())) {
            for (const auto& nm : pg.Names) {
                paramTypes.push_back(ptrTy);          // entry point
                paramNames.push_back(nm);
                paramIsVar.push_back(false);
                paramValTypes.push_back(ptrTy);
                paramTypeNodes.push_back(pg.Type.get());

                paramTypes.push_back(ptrTy);          // frame
                paramNames.push_back(nm + ".frame");
                paramIsVar.push_back(false);
                paramValTypes.push_back(ptrTy);
                paramTypeNodes.push_back(nullptr);

                paramMeta.push_back(ParamMeta{ .procType = pt, .byRef = pg.IsVar });
                schemaTypes.push_back(nullptr);
            }
            continue;
        }

        // A parameter whose form is a bare schema-name takes the body pointer
        // plus one i64 per discriminant.
        const plang::Type* schTy = nullptr;
        if (pg.Type && pg.Type->ResolvedType
                && pg.Type->ResolvedType->Kind == TypeKind::Schema)
            schTy = pg.Type->ResolvedType.get();
        if (schTy) {
            const auto n = static_cast<unsigned>(schTy->SchemaDiscs.size());
            // The body pointer's value type: element type for an array body so
            // that indexing GEPs stride correctly, whole body otherwise.
            const plang::Type* body = schTy->SchemaBody.get();
            llvm::Type* valTy = i64Ty;
            if (body && !body->isError())
                valTy = (body->Kind == TypeKind::Array && body->ElemType)
                            ? llvmTypeOfSemaType(*body->ElemType)
                            : llvmTypeOfSemaType(*body);
            for (const auto& nm : pg.Names) {
                // Always passed by address: the size is not known to the ABI.
                // A value parameter copies the body in the callee prologue.
                paramTypes.push_back(ptrTy);
                paramNames.push_back(nm);
                paramIsVar.push_back(pg.IsVar);
                paramValTypes.push_back(valTy);
                paramTypeNodes.push_back(pg.Type.get());

                for (const auto& d : schTy->SchemaDiscs) {
                    paramTypes.push_back(i64Ty);
                    paramNames.push_back(d.Name + "." + nm);
                    paramIsVar.push_back(false);
                    paramValTypes.push_back(i64Ty);
                    paramTypeNodes.push_back(nullptr);
                }

                paramMeta.push_back(ParamMeta{ .schemaDiscCount = n, .byRef = pg.IsVar });
                schemaTypes.push_back(schTy);
            }
            continue;
        }

        bool isConformant = llvm::dyn_cast<ConformantArrayTypeNode>(pg.Type.get()) != nullptr;
        if (isConformant) {
            // For each name in the param group, emit: ptr + (i64 lo, i64 hi) per dim.
            // Walk the nested ConformantArrayTypeNode chain to find all dims and the
            // innermost concrete element type.
            std::vector<std::pair<std::string,std::string>> dims; // (lo, hi) per dim
            llvm::Type* elemTy = i64Ty; // fallback
            // Turbo's own open-array form (array of T) -- always exactly one
            // dimension, whose (never-read) Specs placeholder is overridden
            // below with a PER-NAME synthesized bound pair rather than
            // shared across the whole group the way EP's own is; see
            // ConformantArrayTypeNode::IsOpenArray's own comment for why.
            bool isOpenArr = false;
            {
                const TypeNode* cur = pg.Type.get();
                while (auto* cn = llvm::dyn_cast<ConformantArrayTypeNode>(cur)) {
                    if (cn->IsOpenArray) isOpenArr = true;
                    for (const auto& spec : cn->Specs)
                        dims.push_back({spec.Lo, spec.Hi});
                    cur = cn->Element.get();
                }
                // cur now points to the concrete element TypeNode.
                if (cur) elemTy = llvmTypeOfNode(*cur);
            }

            for (const auto& nm : pg.Names) {
                // Open array: this name's OWN bound pair, independent of any
                // other name sharing this parameter group (confirmed against
                // fpc -Mtp: `a, b: array of Integer` size independently at
                // the call site) -- see openArrayLowBoundName/
                // openArrayHighBoundName's own comment (AstType.h).  Single
                // dimension only, matching parseTurboOpenArrayParamType.
                std::vector<std::pair<std::string,std::string>> nameDims = dims;
                if (isOpenArr && !nameDims.empty())
                    nameDims[0] = {openArrayLowBoundName(nm), openArrayHighBoundName(nm)};

                // Array ptr
                paramTypes.push_back(ptrTy);
                paramNames.push_back(nm);
                paramIsVar.push_back(false); // handled specially
                paramValTypes.push_back(elemTy);
                paramTypeNodes.push_back(pg.Type.get());

                // lo and hi for each dimension
                for (const auto& [loNm, hiNm] : nameDims) {
                    paramTypes.push_back(i64Ty);
                    paramNames.push_back(loNm + "." + nm); // unique name in LLVM IR
                    paramIsVar.push_back(false);
                    paramValTypes.push_back(i64Ty);
                    paramTypeNodes.push_back(nullptr);

                    paramTypes.push_back(i64Ty);
                    paramNames.push_back(hiNm + "." + nm);
                    paramIsVar.push_back(false);
                    paramValTypes.push_back(i64Ty);
                    paramTypeNodes.push_back(nullptr);
                }

                // Record conformant dims for this AST arg position.
                paramMeta.push_back(ParamMeta{ .conformantDims = nameDims,
                                               .byRef = pg.IsVar,
                                               .isOpenArray = isOpenArr });
                schemaTypes.push_back(nullptr);
            }
        } else {
            llvm::Type* vt = llvmTypeOfNode(*pg.Type);
            const int64_t sb =
                (pg.Type->ResolvedType
                 && pg.Type->ResolvedType->Kind == TypeKind::Set)
                    ? setOffsetOf(*pg.Type->ResolvedType) : 0;
            // Turbo const parameter, structured type (record/array/set):
            // passed BY REFERENCE like a var parameter's pointer, but
            // without the ability to write through it -- Sema has already
            // rejected every assignment to it (checkNotProtected/
            // IsConstParam), so nothing here needs to re-enforce read-only;
            // this only has to choose the same efficient calling convention
            // the feature exists for, rather than copying the whole value in
            // the way a plain or EP-protected value parameter still does
            // (see isStructuredForConstByRef's own comment, Sema/Type.h).  A
            // scalar or string const parameter gets no such treatment: there
            // is no efficiency to gain, so it stays an ordinary value copy.
            const bool constByRef = pg.IsConst && pg.Type->ResolvedType
                && !pg.Type->ResolvedType->isError()
                && isStructuredForConstByRef(*pg.Type->ResolvedType);
            const bool passByRef = pg.IsVar || constByRef;
            for (const auto& nm : pg.Names) {
                paramTypes.push_back(passByRef ? ptrTy : vt);
                paramNames.push_back(nm);
                paramIsVar.push_back(passByRef);
                paramValTypes.push_back(vt);
                paramTypeNodes.push_back(pg.Type.get());
                paramMeta.push_back(ParamMeta{ .setBase = sb, .byRef = passByRef });
                schemaTypes.push_back(nullptr);
            }
        }
    }

    // ISO §6.6.3.3: whether the prologue below may copy at least one
    // parameter to the heap -- a conformant array taken by value.  (Whether
    // it actually does also depends on Sema's ModifiedParams, checked per
    // parameter below; over-approximating here just means an unused mark and
    // a no-op unwind for a value conformant array that is only ever read,
    // which is cheaper than re-deriving that check twice.)  Gates capturing
    // the shadow-stack mark this activation's own copies -- and only
    // theirs -- get disposed from, at the end of this function.
    const bool hasValueConformantParam = std::any_of(
        paramMeta.begin(), paramMeta.end(),
        [](const ParamMeta& pm) {
            return !pm.conformantDims.empty() && !pm.byRef;
        });

    // Return type.
    llvm::Type* retTy = llvm::Type::getVoidTy(ctx);
    if (hd.IsFunction && hd.ReturnType) {
        retTy = llvmTypeOfNode(*hd.ReturnType);
    }
    curRetType = (retTy->isVoidTy()) ? nullptr : retTy;
    // TP-only: Exit(value)'s own use, alongside curRetType -- see
    // curRetSemaType's own comment (CodeGenImpl.h).
    curRetSemaType = (hd.IsFunction && hd.ReturnType) ? hd.ReturnType->ResolvedType
                                                       : nullptr;

    // Turbo Tier 5, Cluster A item 6: a constructor's own hidden success
    // flag -- see curCtorOkAlloca's own comment (CodeGenImpl.h) for the
    // whole design.  hd.IsConstructor rather than proc.IsConstructor: hd is
    // the HEADING (the in-class declaration for an out-of-line body), and
    // that is where the 'constructor' keyword was actually written.
    const bool isConstructor = isMethod && hd.IsConstructor;
    if (isConstructor) retTy = i1Ty;

    // Create function, or take over the declaration already standing under
    // this name.  A procedure declared 'forward' is called before its body is
    // reached, so the pre-pass in emitAllProcedures has put a declaration
    // there; asking LLVM for the name a second time would yield 'plang_b.1'
    // and leave the call pointing at a declaration nothing ever defines.
    auto* funcTy = llvm::FunctionType::get(retTy, paramTypes, false);
    auto* func   = mod->getFunction(mangledName);
    if (func && (!func->isDeclaration() || func->getFunctionType() != funcTy))
        func = nullptr; // a definition, or a signature we cannot fill in
    if (!func)
        func = llvm::Function::Create(funcTy, llvm::Function::ExternalLinkage,
                                      mangledName, mod.get());
    curFunc = func;

    // Record this function's outer variable list and nested status so call
    // sites use the canonical slot ordering (not a second unordered_map walk).
    if (isNested && !outerVars.empty()) {
        nestedFunctions_.insert(mangledName);
        std::vector<std::string> names;
        names.reserve(outerVars.size());
        for (const auto& [nm, ve] : outerVars)
            names.push_back(nm);
        funcOuterVarNames_[mangledName]  = std::move(names);
        funcOuterVarDepths_[mangledName] = outerVarDepths;
    }

    // Parameter metadata a call site needs in order to pass the hidden
    // arguments — conformant bounds, schema discriminants, and the closure
    // pair of a procedural parameter.  Recorded here rather than alongside the
    // body so that the forward pre-pass leaves it behind too: the call being
    // fixed up is one that runs before the body is emitted.
    // Not std::move: the "copy Pascal-declared parameters to allocas" code
    // below reads this same local paramMeta again, in this same call --
    // moving from it here would leave that read seeing an empty vector.
    paramMeta_[mangledName] = paramMeta;

    // A forward declaration contributes its signature and stops; the body
    // arrives with the defining occurrence.
    if (declareOnly) {
        builder.restoreIP(savedIP);
        return;
    }

    // -g: scoped under the enclosing procedure's own DISubprogram when
    // nested, so a debugger's backtrace nests the way the Pascal source
    // does; under the file itself for a top-level procedure.  Deliberately
    // nearestSubprogramScope(), not currentScope(): if a sibling declared
    // earlier in the same enclosing body already shadowed a captured
    // variable (see CGDebugInfo::enterShadowScope), currentScope() would
    // be that sibling's DILexicalBlock, and parenting THIS procedure's own
    // DISubprogram under it produces DISubprogram -> DILexicalBlock ->
    // DISubprogram nesting -- a scope shape llc's DWARF AsmPrinter
    // SIGSEGVs on once any optimization pass runs (confirmed only at -O0
    // by luck: -O0 emits the identical metadata but never walks the path
    // that crashes).  A shadowing lexical block only disambiguates a
    // DILocalVariable's parent (see enterShadowScope's own comment); it
    // has no bearing on where a child PROCEDURE DECLARATION's DISubprogram
    // attaches, so skip straight past it to the nearest real DISubprogram.
    // This procedure really is nested inside that lexical span either way,
    // and either way this procedure's own body gets its own fresh
    // DISubprogram as ITS scope below, not the enclosing one directly.  hd
    // is the heading the parameters and result type were written on (see
    // above), which for a 'forward' declaration's defining occurrence is
    // the declaration -- the body's own line is a better bet for where a
    // breakpoint on the procedure should land than the forward heading's.
    // dbgScope's destructor restores the enclosing scope on every exit
    // path from here on, so nothing below needs a matching manual restore.
    llvm::DILocalScope* enclosingScope = dbgInfo_->nearestSubprogramScope();
    llvm::DIScope* scope = (isNested && enclosingScope)
        ? static_cast<llvm::DIScope*>(enclosingScope)
        : static_cast<llvm::DIScope*>(dbgInfo_->getFile());
    CGDebugInfo::ScopeGuard dbgScope(
        *dbgInfo_, dbgInfo_->emitFunctionStart(func, scope, proc.Name, proc.Loc));

    // Name 'Self' (first arg of a method), ahead of the static-link check
    // just below -- the two never actually co-occur (see isNested's own
    // comment above), but Self is pushed first into paramTypes and must be
    // consumed first here too, for the same reason.
    auto funcArgIt = func->arg_begin();
    llvm::Value* selfArg = nullptr;
    if (isMethod) {
        funcArgIt->setName("self");
        selfArg = &*funcArgIt;
        ++funcArgIt;
    }

    // Name the static-link parameter (first arg of nested procs).
    llvm::Value* staticLinkArg = nullptr;
    if (isNested && !outerVars.empty()) {
        funcArgIt->setName("static_link");
        staticLinkArg = &*funcArgIt;
        ++funcArgIt;
    }

    // Name the remaining (Pascal-declared) parameters.
    size_t ai = 0;
    for (auto it = funcArgIt; it != func->arg_end(); ++it, ++ai)
        if (ai < paramNames.size())
            it->setName(paramNames[ai] + ".param");

    // Create entry block.
    auto* entry = llvm::BasicBlock::Create(ctx, "entry", func);
    builder.SetInsertPoint(entry);

    pushScope();
    // Everything pushed from here on is inside the body -- a with-statement,
    // in practice.  The function's own scope is NOT included: it holds the
    // result cell under the function's name, put there so a nested function
    // can assign it, and finding that would defeat the test.
    curFuncScopeDepth = scopes.size();

    // Turbo Tier 5, Cluster A item 4: bind 'Self' and every field of this
    // method's owning object type (ancestor-inherited included) as bare Var
    // symbols pointing into the Self struct -- the CodeGen mirror of
    // Sema::pushMethodSelfScope, using the SAME idiom CGWith.cpp's ordinary
    // Record branch already uses for 'with r do' (GEP through a layout,
    // defVar the result), just through layoutOfObject/selfFieldPtr's own
    // recursive-ancestor walk instead of a flat one.  Bound directly (no
    // FieldExpr, no synthetic WithStmt): Sema already resolved every bare
    // field name in this body straight to a Var symbol during
    // pushMethodSelfScope, so all CodeGen has to do is give that same name
    // an address here, in the identical outer scope Sema exposed it in
    // (pushed before the parameter/local scope the statements below open,
    // exactly mirroring Sema's own nesting).
    if (isMethod) {
        llvm::Type* selfTy = llvmTypeOfSemaType(*proc.ResolvedOwnerType);
        auto* selfStructTy = llvm::dyn_cast_or_null<llvm::StructType>(selfTy);
        if (!selfStructTy)
            codegenICE("method '" + proc.OwnerType + "." + proc.Name
                       + "': its own object type has no struct layout");
        defVar("Self", selfArg, selfStructTy);
        for (const auto& F : proc.ResolvedOwnerType->RecordFields) {
            llvm::Type* fieldTy = nullptr;
            auto* fp = selfFieldPtr(selfArg, *proc.ResolvedOwnerType, F.Name, fieldTy);
            if (!fp)
                codegenICE("method '" + proc.OwnerType + "." + proc.Name
                           + "': field '" + F.Name
                           + "' has no address in its own object layout");
            defVar(F.Name, fp, fieldTy);
        }
    }

    // If nested, expose outer variables via the static link.
    // The frame struct is { ptr, ptr, ... } — one ptr per outer variable,
    // each pointing to the outer alloca so reads and writes go through.
    curFn.StaticLink = staticLinkArg;
    if (staticLinkArg && !outerVars.empty()) {
        // Frame is { ptr, ptr, ... } — one slot per outer var.
        std::vector<llvm::Type*> ptrFields(outerVars.size(), ptrTy);
        auto* frameTy = llvm::StructType::get(ctx, ptrFields);

        auto* zero = llvm::ConstantInt::get(i32Ty, 0);
        // outerVars is innermost-first, so the FIRST occurrence of a name is
        // the one that name denotes here.  Every slot is still loaded and
        // recorded -- the outer ones have to travel on to a callee that
        // captured them -- but only the innermost takes the name, or a
        // grandparent's variable would answer to it and the body would read
        // straight past its parent's.
        std::set<std::string> Named;
        std::vector<std::string> ConformantCaptures;
        std::vector<std::string> SchemaCaptures;
        for (size_t fi = 0; fi < outerVars.size(); ++fi) {
            const auto& [nm, ve] = outerVars[fi];
            auto* fidx    = llvm::ConstantInt::get(i32Ty, (unsigned)fi);
            // Load the pointer to the outer alloca from the frame slot.
            auto* ptrSlot = builder.CreateGEP(frameTy, staticLinkArg,
                                              {zero, fidx}, "frame.slot." + nm);
            auto* outerPtr = builder.CreateLoad(ptrTy, ptrSlot, "outer." + nm);

            VarEntry Bound = ve;
            Bound.ptr = outerPtr;
            if (Named.insert(toLower(nm)).second) {
                // Register it as the variable's alloca so reads/writes go through.
                // nm is scopes[di]'s own key, case-folded like every scope-map
                // key; ve.displayName is what the programmer actually wrote,
                // carried for exactly this re-registration (see VarEntry).
                //
                // -g: outerPtr is a load result, not an alloca -- unstable,
                // per defVar's own comment on debugIndirectPtr.  Spilling it
                // to a debug-only alloca here (never read back by anything
                // but the declare defVar builds from it) is -g-gated and has
                // no effect at all otherwise: no store, no alloca, nothing.
                llvm::Value* debugAddr = nullptr;
                if (dbgInfo_->isActive()) {
                    auto* slot = createEntryAlloca(ptrTy, ve.displayName + ".dbg");
                    builder.CreateStore(outerPtr, slot);
                    debugAddr = slot;
                }
                defVar(ve.displayName, outerPtr, ve.type, ve.typeNode, debugAddr);
                // ISO §6.6.3.1: an outer procedural parameter is still one here.
                // Only the address travels, which is why the pair was spilled to
                // a cell; what it means has to be carried across separately.
                if (ve.isProcParam) {
                    auto& nve       = scopes.back()[toLower(nm)];
                    nve.isProcParam = true;
                    nve.procType    = ve.procType;
                }
                // §6.6.3.7.1: a conformant array parameter is still one to a
                // procedure nested inside the one that received it.  defVar
                // carries the address and the type and nothing else, so the
                // conformant flag and the bound names were dropped and the
                // nested procedure indexed the block with NO lower bound
                // subtracted: `x[k]` there named the element before the one it
                // names in the parent, and the last subscript ran past the end.
                //
                // The bound variables are the parent's own locals, so this
                // activation captured them too; their addresses here are
                // filled in below, once every capture is bound.
                if (ve.isConformantArray) {
                    auto& nve = scopes.back()[toLower(nm)];
                    nve.isConformantArray = true;
                    nve.conformantLoName  = ve.conformantLoName;
                    nve.conformantHiName  = ve.conformantHiName;
                    nve.conformantElemTy  = ve.conformantElemTy;
                    nve.conformantDims    = ve.conformantDims;
                    ConformantCaptures.push_back(toLower(nm));
                }
                // EP §6.4.7: a schema formal is still one to a procedure nested
                // inside the one that received it -- the same omission as the
                // conformant case above, and with the same consequence.
                if (ve.schemaTy) {
                    auto& nve           = scopes.back()[toLower(nm)];
                    nve.schemaTy        = ve.schemaTy;
                    nve.schemaDiscNames = ve.schemaDiscNames;
                    SchemaCaptures.push_back(toLower(nm));
                }
                Bound = scopes.back()[toLower(nm)];
            }
            curFn.OuterVarNames.push_back(nm);
            // Kept apart from the scope, which a local of the same name will
            // overwrite, and keyed by depth so that two levels sharing a name
            // stay two variables.
            curFn.OuterVarBindings[{outerVarDepths[fi], toLower(nm)}] = Bound;
        }
        // Every capture is bound now, and nothing local has shadowed anything
        // yet, so this is where a captured conformant array's bounds are the
        // ones it was passed with.
        // The discriminant cells are captures like any other, so their
        // addresses are only right once every capture is bound.
        for (const auto& SN : SchemaCaptures) {
            auto& nve = scopes.back()[SN];
            nve.schemaDiscs.clear();
            for (const auto& DN : nve.schemaDiscNames)
                if (const auto* D = findVar(DN))
                    nve.schemaDiscs.push_back(
                        builder.CreateLoad(i64Ty, D->ptr, "disc.captured"));
        }
        for (const auto& CN : ConformantCaptures) {
            auto& nve = scopes.back()[CN];
            nve.conformantDimPtrs.clear();
            for (const auto& [LoNm, HiNm] : nve.conformantDims) {
                const auto* L = findVar(LoNm);
                const auto* H = findVar(HiNm);
                nve.conformantDimPtrs.emplace_back(L ? L->ptr : nullptr,
                                                   H ? H->ptr : nullptr);
            }
        }
    }

    // Alloca for function result (Pascal assigns to function name, or named result variable).
    if (curRetType) {
        curRetAlloca = builder.CreateAlloca(curRetType, nullptr, proc.Name + ".result");
        builder.CreateStore(llvm::Constant::getNullValue(curRetType), curRetAlloca);
        // EP §6.7.2: if a named result variable was declared, register it as a
        // variable alias pointing to the same alloca.
        if (!hd.ResultName.empty())
            defVar(hd.ResultName, curRetAlloca, curRetType);
        // ISO §6.8.2.2 lets a function nested inside this one assign this
        // result, so the cell has to be reachable from there.  Entering it
        // under the function's own name puts it in the scope a nested function
        // collects its outer variables from, and it then travels down the
        // static link like any other local.  Within this function itself
        // nothing changes: the name is matched against curFuncName first.
        defVar(proc.Name, curRetAlloca, curRetType);
    } else {
        curRetAlloca = nullptr;
    }

    // Turbo Tier 5, Cluster A item 6: a constructor begins "ok" -- see
    // curCtorOkAlloca's own comment (CodeGenImpl.h).  'Fail' (CGProcCall)
    // is the only thing that ever stores false here.
    if (isConstructor) {
        curCtorOkAlloca = builder.CreateAlloca(i1Ty, nullptr, proc.Name + ".ctor.ok");
        builder.CreateStore(llvm::ConstantInt::get(i1Ty, 1), curCtorOkAlloca);
    } else {
        curCtorOkAlloca = nullptr;
    }

    // TP-only: where Exit branches to -- see CGFunction::ExitBB's own
    // comment for why this is created unconditionally, before the body
    // (which may contain an Exit) is emitted below.
    curFn.ExitBB = llvm::BasicBlock::Create(ctx, "exit", func);

    // Taken before any of this activation's own by-value conformant array
    // copies (below) are made, so unwinding back to it at the end of this
    // function's body frees exactly those and nothing another activation
    // pushed -- see ConfArrStack's own comment (runtime/plang_sys.cpp).
    llvm::Value* confArrEntryMark = nullptr;
    if (hasValueConformantParam)
        confArrEntryMark = builder.CreateCall(getConfArrMarkFn(), {}, "vcc.mark");

    // Copy Pascal-declared parameters to allocas.
    // paramMeta has one entry per Pascal param name; each LLVM arg sequence
    // consumed depends on whether the param is conformant or not.
    // flatIdx: current offset into paramNames/paramValTypes/paramIsVar.
    {
        auto it = funcArgIt; // LLVM arg iterator (after static link)
        size_t flatIdx = 0;
        for (size_t ci = 0; ci < paramMeta.size() && it != func->arg_end(); ++ci) {
            const std::string& nm  = paramNames[flatIdx];
            const auto&        dims = paramMeta[ci].conformantDims;

            // ISO §6.6.3.1: procedural param — the entry point and its frame.
            if (paramMeta[ci].procType) {
                llvm::Value* fnPtr = &*it; ++it;
                llvm::Value* frame = &*it; ++it;
                // Spilled to a cell so the pair has an address: a nested
                // procedure reaches an outer variable through a static link,
                // which carries addresses and nothing else.
                auto* cell = createEntryAlloca(procPairTy(), nm + ".closure");
                storeProcPair(cell, fnPtr, frame);
                defVar(nm, cell, procPairTy());
                // defVar/declareLocal's ordinary TypeNode path can't
                // describe this one: pt's own ResolvedType is the
                // procedural parameter's SIGNATURE, not the two-pointer
                // closure-pair storage `cell` actually holds -- see
                // declareProcParam's own comment.
                dbgInfo_->declareProcParam(nm, paramMeta[ci].procType, cell);
                auto& ve       = scopes.back()[toLower(nm)];
                ve.isProcParam = true;
                ve.procType    = paramMeta[ci].procType;
                flatIdx += 2;
                continue;
            }

            // EP §6.4.7: schema param — consume the body pointer, then one i64
            // per discriminant.  Both value and var parameters take the address
            // because the body's size is not known to the caller's ABI.
            if (paramMeta[ci].schemaDiscCount > 0) {
                llvm::Value* bodyPtr = &*it; ++it;
                std::vector<llvm::Value*> discs;
                for (unsigned d = 0; d < paramMeta[ci].schemaDiscCount; ++d) {
                    discs.push_back(&*it); ++it;
                }
                // EP §6.7.3.2: a value parameter is a variable of its own, so
                // take a copy sized from the discriminants just received.
                if (!paramIsVar[flatIdx]) {
                    auto* bytes = schemaBodySize(*schemaTypes[ci], discs);
                    auto* copy  = builder.CreateAlloca(i8Ty, bytes, nm + ".copy");
                    const llvm::Align align =
                        mod->getDataLayout().getPrefTypeAlign(paramValTypes[flatIdx]);
                    copy->setAlignment(align);
                    builder.CreateMemCpy(copy, align, bodyPtr, align, bytes);
                    bodyPtr = copy;
                }
                // paramTypeNodes[flatIdx] resolves (EP §6.4.7) to the
                // undiscriminated Schema itself -- bodyPtr is the whole
                // object's own base address regardless of what
                // paramValTypes[flatIdx] narrows to for GEP striding, so
                // describing it by that TypeNode gives -g a real (if
                // probe-approximate; see debugTypeOfSemaType's own Schema
                // case) DIType instead of none at all.  The debug
                // declaration itself is suppressed here and built
                // separately, through declareSchemaParamRef below: bodyPtr
                // is the object's BODY, not its header (issue #142), which
                // is a different shape than declareLocal's ordinary
                // TypeNode path assumes for a Schema/ExtentVaries type.
                defVar(nm, bodyPtr, paramValTypes[flatIdx], paramTypeNodes[flatIdx],
                       /*debugIndirectPtr=*/nullptr, /*suppressDebugDecl=*/true);
                dbgInfo_->declareSchemaParamRef(nm, paramTypeNodes[flatIdx], discs, bodyPtr);
                auto& ve       = scopes.back()[toLower(nm)];
                ve.schemaTy    = schemaTypes[ci];
                // Spilled to cells, and named, so that a procedure nested
                // inside this one can reach them: the static link carries
                // addresses, and these values are this activation's arguments.
                // Without it the nested binding kept no discriminants at all
                // and laid the object out from the PROBE -- writing v.s at
                // offset 8 instead of 72, straight through the array, exit 0
                // and no diagnostic.  Every visible variable is captured, so
                // naming them here is all it takes to carry them across.
                for (size_t d = 0; d < discs.size(); ++d) {
                    const std::string dn = nm + PlangScopeSep + "disc"
                                         + std::to_string(d);
                    auto* slot = createEntryAlloca(i64Ty, dn);
                    builder.CreateStore(discs[d], slot);
                    defVar(dn, slot, i64Ty);
                    ve.schemaDiscNames.push_back(dn);
                }
                ve.schemaDiscs = std::move(discs);
                flatIdx += 1 + paramMeta[ci].schemaDiscCount;
                continue;
            }

            if (!dims.empty()) {
                // Conformant array param: consume ptr arg + 2*D i64 args.
                llvm::Value* arrPtrArg = &*it; ++it;
                llvm::Type*  elemTy    = paramValTypes[flatIdx];

                std::string firstLo = dims[0].first;
                std::string firstHi = dims[0].second;

                // Allocas for each dimension's lo and hi bounds.  Their
                // addresses are kept as well as their names: a subscript must
                // reach this activation's bound, and the name can be answered
                // by any scope that opens later.
                std::vector<std::pair<llvm::Value*, llvm::Value*>> dimPtrs;
                for (const auto& [loNm, hiNm] : dims) {
                    llvm::Value* loArg = &*it; ++it;
                    llvm::Value* hiArg = &*it; ++it;

                    // Turbo open array: this activation's own bound is
                    // ALWAYS Low=0 / High=(extent-1), whatever lower bound
                    // the actual itself happened to be declared with --
                    // confirmed empirically against fpc -Mtp (Sum(x3) where
                    // x3 is `array[1..3] of Integer` sees Low(a)=0,
                    // High(a)=2 inside Sum).  Normalized ONCE here, on
                    // entry, from whatever raw (lo, hi) the caller pushed
                    // (pushConformantArgs, ClosureAndCallABI.cpp, which
                    // pushes the actual's own raw declared bounds for a
                    // plain array, or -- relaying an open array this
                    // activation already received -- forwards THAT
                    // parameter's own already-normalized slot, so
                    // normalizing again here is a no-op the second time,
                    // not a second subtraction).  The base pointer itself
                    // (arrPtrArg, below) needs no matching adjustment: it
                    // already addresses the actual's first stored element
                    // regardless of what bound it was declared with, and
                    // emitConformantElemPtr indexes off (subscript - lo),
                    // which is just the subscript once lo is 0.
                    llvm::Value* loVal = loArg;
                    llvm::Value* hiVal = hiArg;
                    if (paramMeta[ci].isOpenArray) {
                        hiVal = builder.CreateSub(hiArg, loArg, "oa.high");
                        loVal = llvm::ConstantInt::get(i64Ty, 0);
                    }

                    auto* loA = createEntryAlloca(i64Ty, loNm + ".addr");
                    builder.CreateStore(loVal, loA);
                    defVar(loNm, loA, i64Ty);

                    auto* hiA = createEntryAlloca(i64Ty, hiNm + ".addr");
                    builder.CreateStore(hiVal, hiA);
                    defVar(hiNm, hiA, i64Ty);

                    dimPtrs.emplace_back(loA, hiA);
                }

                // ISO §6.6.3.3: a value parameter is a variable of its own
                // that the actual is assigned to, so a conformant array passed
                // by value is a COPY and assigning to it must not reach the
                // actual.
                //
                // Two things make the copy awkward, and both are answered here
                // rather than by copying unconditionally.  Its extent is only
                // known at the call, so it cannot be an entry alloca; and a
                // dynamic one is as big as the actual, so a 100 kB array passed
                // down twenty activations exhausted the stack and the program
                // died with no diagnostic.
                //
                // So: a body that never modifies the formal cannot tell whether
                // it was copied, and gets none -- which is the case for every
                // array merely read or relayed, including the recursion above.
                // Sema works out which formals are modified, because deciding
                // it needs each callee's signature: handing the formal on as
                // somebody else's `var` parameter modifies it and handing it on
                // by value does not.
                //
                // Where a copy IS needed it goes on the heap, freed at the end
                // of the body, so its size is bounded by memory rather than by
                // the stack.
                llvm::Value* dataPtr = arrPtrArg;
                const bool ByValue = !paramMeta[ci].byRef;
                const bool Modified =
                    proc.heading().ModifiedParams.count(toLower(nm)) > 0;
                if (ByValue && Modified) {
                    llvm::Value* count = llvm::ConstantInt::get(i64Ty, 1);
                    for (const auto& [loA, hiA] : dimPtrs) {
                        auto* lo  = builder.CreateLoad(i64Ty, loA, "cp.lo");
                        auto* hi  = builder.CreateLoad(i64Ty, hiA, "cp.hi");
                        auto* ext = builder.CreateAdd(
                            builder.CreateSub(hi, lo, "cp.span"),
                            llvm::ConstantInt::get(i64Ty, 1), "cp.ext");
                        count = builder.CreateMul(count, ext, "cp.count");
                    }
                    // An empty conformant array is a legal shape to pass, and a
                    // negative count would ask for an enormous block.
                    auto* zero64 = llvm::ConstantInt::get(i64Ty, 0);
                    count = builder.CreateSelect(
                        builder.CreateICmpSGT(count, zero64, "cp.pos"),
                        count, zero64, "cp.n");
                    const uint64_t esz =
                        mod->getDataLayout().getTypeAllocSize(elemTy).getFixedValue();
                    const auto align = mod->getDataLayout().getABITypeAlign(elemTy);
                    auto* bytes = builder.CreateMul(
                        count, llvm::ConstantInt::get(i64Ty, esz), "cp.bytes");
                    auto* copy = builder.CreateCall(getRuntimeNewFn(), {bytes},
                                                    nm + ".copy");
                    builder.CreateMemCpy(copy, align, arrPtrArg, align, bytes);
                    // Recorded on the shadow stack, not just locally: a
                    // non-local goto out of this procedure (ISO §6.8.1)
                    // abandons this activation via _longjmp, skipping the
                    // disposal at the end of this function entirely, and
                    // only that stack lets the landing pad it jumps to find
                    // this copy anyway (LabelGotoEngine::closeLabelScope).
                    builder.CreateCall(getConfArrPushFn(), {copy});
                    dataPtr = copy;
                }
                defVar(nm, dataPtr, elemTy);
                {
                    auto& ve             = scopes.back()[toLower(nm)];
                    ve.isConformantArray = true;
                    ve.conformantLoName  = firstLo;
                    ve.conformantHiName  = firstHi;
                    ve.conformantElemTy  = elemTy;
                    ve.conformantDims.assign(dims.begin(), dims.end());
                    ve.conformantDimPtrs = std::move(dimPtrs);
                }

                // Advance flatIdx past: 1 (array ptr) + 2*D (lo/hi pairs).
                flatIdx += 1 + 2 * dims.size();
            } else {
                // Regular (non-conformant) param.
                bool            isV  = paramIsVar[flatIdx];
                llvm::Type*     vt   = paramValTypes[flatIdx];
                const TypeNode* tn   = flatIdx < paramTypeNodes.size()
                                        ? paramTypeNodes[flatIdx] : nullptr;
                if (isV) {
                    // -g: &*it is the raw incoming Argument, not an alloca --
                    // unstable, per defVar's own comment on debugIndirectPtr.
                    // Same -g-gated debug-only spill as the capture loop.
                    llvm::Value* debugAddr = nullptr;
                    if (dbgInfo_->isActive()) {
                        auto* slot = createEntryAlloca(ptrTy, nm + ".dbg");
                        builder.CreateStore(&*it, slot);
                        debugAddr = slot;
                    }
                    defVar(nm, &*it, vt, tn, debugAddr);
                } else {
                    // ISO §6.6.3.2: a value parameter is a variable of its
                    // own that the actual argument is assigned to, so
                    // §6.4.6's requirement that a subrange-typed
                    // destination's value lie within its declared bounds
                    // applies here exactly as it does to an ordinary
                    // assignment (see CGAssign.cpp) -- nothing enforced it
                    // here, so an out-of-range actual arrived in the copy
                    // uncaught.  A var parameter is excluded: it aliases the
                    // caller's own storage rather than copying into one of
                    // its own, so it is not an assignment and nothing new is
                    // checked here for that path.
                    // Lo == Hi (e.g. `5..5`) is a legal singleton subrange,
                    // not a sentinel for "no real bounds" -- Kind ==
                    // Subrange already guarantees these are real, so
                    // excluding Lo == Hi here would just let a value actual
                    // arrive in a singleton-subrange formal uncaught.
                    llvm::Value* argVal = &*it;
                    if (const auto& rt = tn ? tn->ResolvedType : nullptr;
                            rt && rt->Kind == TypeKind::Subrange
                            && argVal->getType()->isIntegerTy())
                        // argVal is the raw incoming Argument, at the
                        // formal's OWN (possibly narrower-than-i64) LLVM
                        // width -- rt's own signedness (not a guess from
                        // that width) has to widen it before the compare,
                        // or a legal negative value formal'd as a signed
                        // narrow Turbo ordinal (e.g. `x: -100..100`) got
                        // zero-extended into a huge positive one and failed
                        // this check spuriously (issue #177's sibling
                        // audit; see CGAssign.cpp's own two identical
                        // fixes for the assignment-statement shape of the
                        // same bug).
                        emitRangeCheck(argVal, rt->SubLo, rt->SubHi,
                                       /*isIndex=*/false, tn->Loc,
                                       !ordinalIsUnsigned(rt.get()));
                    auto* a = createEntryAlloca(vt, nm + ".addr");
                    builder.CreateStore(argVal, a);
                    defVar(nm, a, vt, tn); // pass typeNode so bounds are known
                }
                ++it;
                ++flatIdx;
            }
        }
    }

    // Emit body (recurse into nested procs first).
    if (proc.Body) {
        emitBlockAllocas(*proc.Body);
        // Before the nested procedures, so that the buffer a goto in one of
        // them longjmps to is among the variables their frame carries.
        openLabelScope(*proc.Body, /*programBlock=*/false);
        emitAllProcedures(*proc.Body);
        if (proc.Body->Body) emitCompound(*proc.Body->Body);
        closeLabelScope();
    }

    // TP-only: Exit (CGProcCall) branches to ExitBB from anywhere in the
    // body; the ordinary "fall off the end" path (this brIfNeeded) is routed
    // through the same block, so the epilogue below runs in exactly one
    // place regardless of which of the two reached it.  A non-local goto out
    // of THIS procedure terminates the block its own way
    // (LabelGotoEngine's longjmp landing pad) and is excluded here for the
    // same reason it always was: isTerminated() is false only on a path that
    // reaches the end of the body -- or an explicit Exit -- normally.
    brIfNeeded(exitBlock());
    builder.SetInsertPoint(exitBlock());

    // Give back the heap a value conformant array parameter was copied into.
    // Unwinding to the mark taken before this activation's own copies were
    // pushed frees exactly those and nothing else: whatever this body called
    // has, by now, either returned normally and unwound its own copies the
    // same way, or left by a non-local goto whose landing pad already
    // unwound what it abandoned (LabelGotoEngine::closeLabelScope) -- so
    // nothing but this activation's own copies can still be above the mark
    // here.  A non-local goto out of THIS procedure never reaches ExitBB at
    // all (see above), but that is fine: the same copies are still on the
    // shadow stack for whichever ancestor's landing pad the goto reaches to
    // free instead.
    if (confArrEntryMark)
        builder.CreateCall(getConfArrUnwindFn(), {confArrEntryMark});

    if (curRetType && curRetAlloca) {
        auto* rv = builder.CreateLoad(curRetType, curRetAlloca, "retval");
        builder.CreateRet(rv);
    } else if (isConstructor && curCtorOkAlloca) {
        // Turbo Tier 5, Cluster A item 6: the real TP7/BP ABI -- see
        // curCtorOkAlloca's own comment.
        auto* ok = builder.CreateLoad(i1Ty, curCtorOkAlloca, "ctor.ok.result");
        builder.CreateRet(ok);
    } else {
        builder.CreateRetVoid();
    }

    popScope();

    // Restore outer context. curFunc/curRetAlloca/curRetType/curFuncName/
    // namePrefix/typeAliases/consts/requiredConsts/curFuncScopeDepth/the
    // schema-defs snapshot/curStaticLink/outerVarNames/outerVarBindings
    // are restored unconditionally by curFn's destructor below, at the
    // end of this scope.
    builder.restoreIP(savedIP);
}

/// ISO §6.4.2.3: the identifiers of an enumerated type denote constants of it.
/// The type need not have been given a name to do that, so a `var e: (x, y)`
/// introduces x and y just as a named type declaration would.
/// Registers the enumerations a variant part introduces, as its tag type or as
/// the type of a field in one of its alternatives, and does the same for the
/// variants nested inside it.
void Codegen::Impl::registerVariantEnumValues(const VariantPart& vp) {
    registerEnumValues(vp.TagType.get());
    for (const auto& vc : vp.Cases) {
        for (const auto& f : vc.Fields) registerEnumValues(f.Type.get());
        if (vc.NestedVariant) registerVariantEnumValues(*vc.NestedVariant);
    }
}

void Codegen::Impl::registerEnumValues(const TypeNode* tn) {
    if (!tn) return;
    if (auto* etn = llvm::dyn_cast<EnumTypeNode>(tn)) {
        int64_t ord = 0;
        for (const auto& val : etn->Values)
            defineConst(val,
                llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(ord++), true));
        return;
    }
    // An enumeration can also appear as a component or as an index type.
    if (auto* atn = llvm::dyn_cast<ArrayTypeNode>(tn)) {
        registerEnumValues(atn->Index.get());
        registerEnumValues(atn->Element.get());
        return;
    }
    if (auto* rtn = llvm::dyn_cast<RecordTypeNode>(tn)) {
        for (const auto& f : rtn->Fields) registerEnumValues(f.Type.get());
        // A variant part can introduce an enumeration too, as its tag type or
        // as the type of a field in one of the alternatives.
        if (rtn->Variant) registerVariantEnumValues(*rtn->Variant);
    }
}

void Codegen::Impl::emitBlockDecls(const BlockNode& block) {
    for (const auto& td : block.Types) {
        typeAliases[toLower(td.Name)] = td.Type.get();
        registerEnumValues(td.Type.get());
    }
    for (const auto& vg : block.Vars) registerEnumValues(vg.Type.get());
    registerSchemaDefs(block);

    // Register block-local constants in definition order.
    // For EP general constant expressions that can't be compile-time folded,
    // fall back to emitExpr() since we're inside a basic block.
    for (const auto& cd : block.Consts) {
        // TP-only: a typed constant (ConstDef::Type set) gets static storage
        // of its own -- a GlobalVariable, not a per-activation alloca -- see
        // emitLocalStaticConst's own comment for why and for the guard
        // below.  findVar first: this function runs once for a genuinely
        // local typed constant (through emitBlockAllocas, from
        // emitFunctionDef) but ALSO a second time, redundantly, when emitMain
        // calls it directly on the program's own top-level block, which
        // emitGlobals(*prog.Block) already gave this exact constant its
        // storage in -- the same reason emitInheritedGlobals guards its own
        // structured-constant call with findVar just below.
        if (cd.Type) {
            if (!findVar(cd.Name)) emitLocalStaticConst(cd);
            continue;
        }
        // EP §6.8.7: a structured constant is a whole array, record or set, so
        // it lives in storage of its own like the global case does.
        if (llvm::isa<StructuredValueExpr>(cd.Value.get())) {
            const TypeNode* tn = nullptr;
            llvm::Type*     ty = structuredConstType(cd, tn);
            auto* a = createEntryAlloca(ty, cd.Name + ".addr");
            if (auto* val = emitExpr(*cd.Value))
                builder.CreateMemCpy(a, llvm::MaybeAlign(), val, llvm::MaybeAlign(),
                                     mod->getDataLayout().getTypeAllocSize(ty));
            defVar(cd.Name, a, ty, tn);
            continue;
        }
        if (llvm::Value* cv = constantValueOf(cd)) { defineConst(cd.Name, cv); continue; }
        // EP §6.8.2 lets a constant be a general constant expression, and one
        // this cannot fold has to be computed where the code runs.  The value
        // used to be put straight into `consts`, which is flat and outlives
        // this function -- so a nested procedure emitted afterwards read an
        // instruction belonging to another function and the module did not
        // verify: "Referring to an instruction in another function!" on a
        // perfectly legal program.
        //
        // It goes in storage instead, and is defVar'd like any other local, so
        // a nested procedure reaches it through the static link the same way it
        // reaches everything else its enclosing procedure declared.
        if (llvm::Value* val = emitExpr(*cd.Value)) {
            llvm::Type* ty = val->getType();
            auto* slot = createEntryAlloca(ty, cd.Name + ".const");
            builder.CreateStore(val, slot);
            defVar(cd.Name, slot, ty);
            continue;
        }
        codegenICE("constant '" + cd.Name + "' has no value that Sema folded "
                   "or that codegen can emit");
    }
}

void Codegen::Impl::emitBlockAllocas(const BlockNode& block) {
    // Before the variables, not after: `type t = integer; var v: t` has to see
    // this block's t rather than an enclosing block's.
    emitBlockDecls(block);
    for (const auto& vg : block.Vars) {
        llvm::Type* ty = llvmTypeOf(vg.Type.get(), nullptr);

        // TP-only: 'absolute' overlays this declaration's storage onto an
        // existing variable's (or a component of one), so it gets no alloca
        // of its own -- see AbsoluteExpr's own comment (AstDecl.h).  Sema
        // (checkBlock's Phase 4) has already required exactly one name and an
        // addressable target; emitLValue works here because, unlike
        // emitGlobals' own var loop, this runs with a real entry block and
        // builder position already in place (emitFunctionDef created both
        // before calling emitBlockAllocas), so any designator -- 'absolute
        // B', 'absolute B[2]', 'absolute R.Field' -- can be addressed, not
        // only a bare name the way the global case is currently limited to.
        if (vg.AbsoluteExpr) {
            auto* addr = emitLValue(*vg.AbsoluteExpr);
            if (!addr) codegenICE("'absolute' target has no storage to overlay onto");
            defVar(vg.Names[0], addr, ty, vg.Type.get());
            continue;
        }

        for (const auto& nm : vg.Names) {
            auto* a = createEntryAlloca(ty, nm + ".addr");
            builder.CreateStore(llvm::Constant::getNullValue(ty), a);
            defVar(nm, a, ty, vg.Type.get());
        }
        // For string(N) vars: init length to 0.
        if (int64_t cap = declaredStrCapacity(vg.Type.get()); cap > 0) {
            auto* fn = getStrFn("plang_str_init",
                llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty});
            for (const auto& nm : vg.Names) {
                const auto* ve = findVar(nm);
                if (ve) builder.CreateCall(fn, {ve->ptr,
                    llvm::ConstantInt::get(i64Ty, cap, true)});
            }
        }
        emitVarValueInit(vg);
    }
}

// The capacity of a string-typed declaration, or 0 when it is not a string.
int64_t Codegen::Impl::declaredStrCapacity(const TypeNode* tn) {
    if (!tn) return 0;
    if (auto* sn = llvm::dyn_cast<StringTypeNode>(tn)) {
        if (auto v = tryEvalConstInt(*sn->Capacity, &consts)) return *v;
        // A capacity that does not fold here is not fabricated as
        // PlangMaxStringCapacity (255): that fallback is the same fabrication
        // R2 deleted at the sibling sites below and in SchemaLayoutEngine.
        // Sema resolves every StringTypeNode -- including one reached through
        // a foreign denoter whose capacity is a discriminant -- and records
        // ITS OWN answer on ResolvedType even where the AST expression itself
        // has no ConstVal (a discriminant read is deliberately never folded
        // onto the shared node, see Sema::constBound).  Falling back to that
        // answer instead is exactly what the "written through a name" arm
        // below already does for the same reason.
        if (tn->ResolvedType && tn->ResolvedType->Kind == TypeKind::VarString)
            return tn->ResolvedType->StrCapacity;
        return 0;
    }
    // Written through a name, so the syntax says nothing; Sema resolved it.
    if (tn->ResolvedType && tn->ResolvedType->Kind == TypeKind::VarString)
        return tn->ResolvedType->StrCapacity;
    return 0;
}

namespace {
// Same fresh-copy convention CGProcCall.cpp/CGFuncCall.cpp's own
// methodOwnerType uses (see either one's comment) -- walks Start's own
// ancestor chain for the Type NAMED TypeName (case-insensitive), which for
// a VmtSlotEntry::ImplementingType or an InheritedCallStmt::ImplementingType
// is always Start itself or one of its ancestors, never anything unrelated.
const Type* typeNamedInChain(const Type& Start, const std::string& TypeName) {
    for (const Type* Cur = &Start; Cur; Cur = Cur->Parent.get())
        if (eqCI(Cur->Name, TypeName)) return Cur;
    return nullptr;
}
// Same fresh-copy convention as methodEntryOf in CGProcCall.cpp/
// CGFuncCall.cpp, restricted to methods DECLARED BY Owner itself (never an
// inherited one -- see Type::ObjectMethods's own comment), which is exactly
// what a VmtSlotEntry::ImplementingType names.
const Type::Method* methodNamedOn(const Type& Owner, const std::string& MethodName) {
    for (const auto& M : Owner.ObjectMethods)
        if (eqCI(M.Name, MethodName)) return &M;
    return nullptr;
}
// Issue #511: whether T either IS a TypeKind::Object, or has one nested
// ANYWHERE in its own structure -- a record/object field (including one
// inherited from an ancestor: RecordFields is already the flattened
// ancestor-then-own list, see its own comment, Type.h), or an array's
// element type, arbitrarily deep.  Mirrors Sema::typeContainsFile's own
// recursive shape exactly (SemaExpr.cpp) -- the identical question about
// TypeKind::File instead of TypeKind::Object -- duplicated rather than
// shared because that one lives in Sema and answers a different question
// (ISO §6.6.3.3's file-by-value rule) that just happens to need the same
// walk.  Used to gate stampFieldVptrs at every level, so a large record or
// array with no object anywhere in it costs nothing beyond this one check.
bool typeContainsObject(const Type& T) {
    switch (T.Kind) {
    case TypeKind::Object: return true;
    case TypeKind::Record:
        for (const auto& F : T.RecordFields)
            if (F.Ty && typeContainsObject(*F.Ty)) return true;
        return false;
    case TypeKind::Array:
        return T.ElemType && typeContainsObject(*T.ElemType);
    default: return false;
    }
}
} // namespace

// Turbo Tier 5, Cluster A item 5 / Cluster B item 8: see this function's own
// declaration (CodeGenImpl.h) for the whole design.
llvm::GlobalVariable* Codegen::Impl::getOrCreateVmt(const Type& T) {
    if (T.Kind != TypeKind::Object || T.VmtSlots.empty()) return nullptr;
    if (auto it = vmtGlobals_.find(&T); it != vmtGlobals_.end()) return it->second;

    std::vector<llvm::Constant*> elems;
    elems.reserve(T.VmtSlots.size());
    for (const auto& slot : T.VmtSlots) {
        std::string mangledName =
            mangledMethod(slot.ImplementingType, slot.MethodName, slot.DeclaringModule);
        auto* fn = mod->getFunction(mangledName);
        if (!fn) {
            // Cluster B item 8: not an error -- the implementing type may be
            // declared in a unit this translation unit only reaches through
            // a re-parsed .tui, whose own out-of-line bodies were never (and
            // are never going to be) part of this compile at all.  Declare
            // it, from the implementing type's own resolved Method entry,
            // exactly the way any other cross-unit reference in this
            // codebase is declared rather than defined.
            const Type* Impl = typeNamedInChain(T, slot.ImplementingType);
            const Type::Method* M = Impl ? methodNamedOn(*Impl, slot.MethodName) : nullptr;
            if (!Impl || !M)
                codegenICE("VMT for object type '" + T.Name + "' needs '"
                           + mangledName + "', implemented by '"
                           + slot.ImplementingType + "." + slot.MethodName
                           + "', which cannot be found in its own ancestor "
                             "chain -- Sema should have refused this "
                             "hierarchy already");
            fn = declareImportedMethod(*M, mangledName);
        }
        elems.push_back(fn);
    }
    auto* arrTy = llvm::ArrayType::get(ptrTy, elems.size());
    auto* init  = llvm::ConstantArray::get(arrTy, elems);
    auto* gv = new llvm::GlobalVariable(
        *mod, arrTy, /*isConstant=*/true, llvm::GlobalValue::InternalLinkage,
        init, mangledVmt(T.Name, T.DeclaringModule));
    vmtGlobals_[&T] = gv;
    return gv;
}

// Turbo Tier 5, Cluster B item 8: see this function's own declaration
// (CodeGenImpl.h) for the whole design.
llvm::Function* Codegen::Impl::declareImportedMethod(const Type::Method& M,
                                                      const std::string& mangledName) {
    if (auto* existing = mod->getFunction(mangledName)) return existing;

    std::vector<llvm::Type*> paramTys;
    paramTys.push_back(ptrTy); // Self

    for (const auto& P : M.Params) {
        if (P.IsUntyped) { paramTys.push_back(ptrTy); continue; }
        if (!P.Ty)
            codegenICE("imported method '" + mangledName + "' has a "
                       "parameter '" + P.Name + "' with no resolved type");
        if (P.Ty->Kind == TypeKind::ConformantArray
                || P.Ty->Kind == TypeKind::Procedure
                || P.Ty->Kind == TypeKind::Function)
            codegenICE("imported method '" + mangledName + "' has a "
                       "conformant-array or procedural-type parameter '"
                       + P.Name + "' -- not yet supported across a unit "
                         "boundary (see declareImportedMethod's own "
                         "comment, CodeGenImpl.h)");
        const bool constByRef = P.IsConst && !P.Ty->isError()
            && isStructuredForConstByRef(*P.Ty);
        const bool passByRef = P.IsVar || constByRef;
        paramTys.push_back(passByRef ? ptrTy : llvmTypeOfSemaType(*P.Ty));
    }

    llvm::Type* retTy = llvm::Type::getVoidTy(ctx);
    if (M.IsConstructor) retTy = i1Ty; // hidden success flag -- see emitFunctionDef
    else if (M.IsFunction && M.RetType) retTy = llvmTypeOfSemaType(*M.RetType);

    auto* fnTy = llvm::FunctionType::get(retTy, paramTys, /*isVarArg=*/false);
    return llvm::Function::Create(fnTy, llvm::GlobalValue::ExternalLinkage,
                                  mangledName, mod.get());
}

// Turbo Tier 5, Cluster A item 5: see this function's own declaration
// (CodeGenImpl.h) for the whole design.
void Codegen::Impl::stampVptr(llvm::Value* ptr, const Type& T) {
    if (T.Kind != TypeKind::Object) return;
    auto off = vptrOffsetOf(T);
    if (!off) return;
    auto* vmt = getOrCreateVmt(T);
    if (!vmt) return;
    auto* slot = builder.CreateGEP(i8Ty, ptr,
        {llvm::ConstantInt::get(i64Ty, *off)}, "vptr.slot");
    builder.CreateStore(vmt, slot);
}

// Issue #511: see this function's own declaration (CodeGenImpl.h) for the
// whole design -- the nested-member counterpart to stampVptr just above.
void Codegen::Impl::stampFieldVptrs(llvm::Value* ptr, const Type& T, int depth) {
    if (!ptr || depth > 16 || !typeContainsObject(T)) return;

    if (T.Kind == TypeKind::Array) {
        auto* arrTy = llvm::dyn_cast_or_null<llvm::ArrayType>(llvmTypeOfSemaType(T));
        if (!arrTy) return; // no IndexType/ElemType to lay out (llvmTypeOfSemaType's own guard)
        const uint64_t count = arrTy->getNumElements();
        const bool elemIsObject = T.ElemType->Kind == TypeKind::Object;
        auto stampElem = [&](llvm::Value* elemPtr) {
            if (elemIsObject) stampVptr(elemPtr, *T.ElemType);
            stampFieldVptrs(elemPtr, *T.ElemType, depth + 1);
        };
        // Every element begins the same way, so one body serves them all; see
        // emitInitialState's own array branch (same shape, same threshold) for
        // why this is written out per element only while that stays the
        // smaller of the two, and run as a loop once the array is long enough
        // to matter.
        if (count <= 8) {
            for (uint64_t i = 0; i < count; ++i) {
                auto* gep = builder.CreateGEP(arrTy, ptr,
                    {llvm::ConstantInt::get(i64Ty, 0),
                     llvm::ConstantInt::get(i64Ty, i)}, "vptr.e");
                stampElem(gep);
            }
            return;
        }
        auto* fn   = builder.GetInsertBlock()->getParent();
        auto* head = llvm::BasicBlock::Create(ctx, "vptr.head", fn);
        auto* body = llvm::BasicBlock::Create(ctx, "vptr.body", fn);
        auto* done = llvm::BasicBlock::Create(ctx, "vptr.done", fn);
        auto* iv   = createEntryAlloca(i64Ty, "vptr.i");
        builder.CreateStore(llvm::ConstantInt::get(i64Ty, 0), iv);
        builder.CreateBr(head);
        builder.SetInsertPoint(head);
        auto* i = builder.CreateLoad(i64Ty, iv, "vptr.i.cur");
        builder.CreateCondBr(
            builder.CreateICmpULT(i, llvm::ConstantInt::get(i64Ty, count)),
            body, done);
        builder.SetInsertPoint(body);
        auto* gep = builder.CreateGEP(arrTy, ptr,
            {llvm::ConstantInt::get(i64Ty, 0), i}, "vptr.e");
        stampElem(gep);
        builder.CreateStore(
            builder.CreateAdd(i, llvm::ConstantInt::get(i64Ty, 1)), iv);
        builder.CreateBr(head);
        builder.SetInsertPoint(done);
        return;
    }

    const RecordLayout* L = T.Kind == TypeKind::Object ? layoutOfObject(T)
                           : T.Kind == TypeKind::Record ? layoutOfRecord(T)
                                                         : nullptr;
    if (!L || !L->Ty->isSized()) return;
    auto* zero = llvm::ConstantInt::get(i32Ty, 0);

    size_t ownStart = 0;
    if (T.Kind == TypeKind::Object && T.Parent) {
        // Same nested-ancestor-embedding idiom as selfFieldPtr/objectFieldPtr
        // (CGFieldAccess.cpp): element 0 of every layoutOfObject struct is the
        // WHOLE ancestor sub-object, unshifted -- so a field the ANCESTOR
        // declares (never re-listed by this level's own layout -- see
        // layoutOfObject's own comment) is reached by recursing at the SAME
        // address under the ancestor's own type, not by an extra GEP.
        auto* parentPtr = builder.CreateGEP(L->Ty, ptr, {zero, zero}, "vptr.parent");
        stampFieldVptrs(parentPtr, *T.Parent, depth + 1);
        ownStart = T.Parent->RecordFields.size();
    }

    for (size_t i = ownStart; i < T.RecordFields.size(); ++i) {
        const auto& F = T.RecordFields[i];
        if (!F.Ty || !typeContainsObject(*F.Ty)) continue;
        auto fit = L->Fields.find(toLower(F.Name));
        if (fit == L->Fields.end()) continue;
        const auto& P = fit->second;
        if (P.Index >= L->Ty->getNumElements()) continue;
        auto* fp = builder.CreateGEP(L->Ty, ptr,
            {zero, llvm::ConstantInt::get(i32Ty, P.Index)}, "vptr.f");
        if (P.InVariant && P.Offset != 0)
            fp = builder.CreateConstGEP1_64(i8Ty, fp, P.Offset, "vptr.f");
        // F's OWN slot (when F.Ty is itself Kind::Object) and whatever F.Ty
        // holds nested even deeper are two separate questions -- exactly the
        // same pairing this function's own caller (emitVarValueInit) uses for
        // the outermost instance, applied recursively one level in.
        if (F.Ty->Kind == TypeKind::Object) stampVptr(fp, *F.Ty);
        stampFieldVptrs(fp, *F.Ty, depth + 1);
    }
}

// EP §6.6: the value clause of a variable declaration, or of the denoter the
// variable is declared with.
void Codegen::Impl::emitVarValueInit(const VarGroup& vg) {
    if (!vg.InitExpr) {
        // The declaration says nothing, but the type may: `var x: t` begins in
        // whatever state t was declared to begin in.
        if (hasInitialState(vg.Type.get()))
            for (const auto& nm : vg.Names)
                if (const auto* ve = findVar(nm))
                    emitInitialState(ve->ptr, ve->type, vg.Type.get());
    } else {
        for (const auto& nm : vg.Names)
            if (const auto* ve = findVar(nm))
                storeInitialValue(ve->ptr, ve->type, vg.Type.get(), *vg.InitExpr);
    }

    // Turbo Tier 5, Cluster A item 5: stamp this concrete type's own VMT
    // global address into every declared instance's `_vptr` slot, so that a
    // virtual call dispatched through it -- directly, or via '@X'/'^'
    // derived from it -- reaches the right override from the moment the
    // variable exists.  Runs AFTER whatever value/initial-state init just
    // happened above (not before), so a `value` clause or initial-state
    // fill that touches this object's own storage can never clobber a vptr
    // this already set -- see stampVptr's own comment for scope (a directly
    // declared local/global only; New/Init/Fail is item 6's job).
    if (vg.Type && vg.Type->ResolvedType) {
        const Type& RT = *vg.Type->ResolvedType;
        if (RT.Kind == TypeKind::Object)
            for (const auto& nm : vg.Names)
                if (const auto* ve = findVar(nm))
                    stampVptr(ve->ptr, RT);
        // Issue #511: the call above reaches only a directly declared OBJECT
        // variable's own top-level slot.  A record, array, or object with an
        // object-typed member anywhere inside its own structure needs each
        // of THOSE stamped too -- see stampFieldVptrs's own comment.  Same
        // "after value/initial-state init" ordering as above, and for the
        // same reason (nothing stamped here can be a store this already-run
        // init could go on to clobber).
        if (typeContainsObject(RT))
            for (const auto& nm : vg.Names)
                if (const auto* ve = findVar(nm))
                    stampFieldVptrs(ve->ptr, RT);
    }
}

// Puts one written value into the storage of a variable of the denoter's type.
void Codegen::Impl::storeInitialValue(llvm::Value* ptr, llvm::Type* ty,
                                      const TypeNode* tn, const ExprNode& value) {
    if (!ptr) return;

    // A string is a length and a buffer, not a value that fits in a register,
    // so it cannot be stored the way the other types are — a plain store of
    // the pointer to the literal's temporary left the variable's own buffer
    // untouched and its length whatever the pointer bits happened to say.
    // Whether this is a string is a question about the type, not about how the
    // declaration was written: 'type st = string(12); var a: st' is one just
    // as much as 'var a: string(12)' is.
    if (int64_t cap = declaredStrCapacity(tn); cap > 0) {
        emitStrStore(ptr, cap, value);
        return;
    }

    // ISO §6.4.3.2: a packed array[1..n] of char takes a string value, which
    // is a run of characters and not something a store can put there either.
    if (tn && tn->ResolvedType && isCharStringType(*tn->ResolvedType)) {
        emitCharStrStore(ptr, charStringLength(*tn->ResolvedType), value);
        return;
    }

    llvm::Value* val = nullptr;
    if (value.ConstVal) {
        // Sema::checkInitialState folded this in the scope the 'value'
        // clause was actually WRITTEN in -- which may be a different scope
        // than wherever this call is materializing it, since this value
        // came from a foreign TypeNode reached through writtenInitialState's
        // Denotes-chain walk.  emitExpr resolves any identifier in an
        // expression against whatever scope is CURRENTLY being lowered, not
        // the one the expression was written in, so a bare `value Foo`
        // clause read whichever Foo the materializing procedure happened to
        // have rather than the one in scope where the clause was written.
        // Materializing the already-folded constant sidesteps that
        // resolution entirely, the same way every other constant-expression
        // use site in codegen prefers Sema's answer over re-evaluating.
        val = i64c(*value.ConstVal);
    } else if (value.ConstRealVal) {
        // The real-valued sibling of the ConstVal case just above; same
        // reasoning, see ExprNode::ConstRealVal.
        val = llvm::ConstantFP::get(dblTy, *value.ConstRealVal);
    } else if (auto* sv = llvm::dyn_cast<StructuredValueExpr>(&value);
            sv && sv->TypeName.empty())
        val = emitStructuredValue(*sv, tn); // a component-value names no type
    else
        val = emitExpr(value);
    if (!val) return;

    const bool isAggregate = value.ResolvedType &&
        (value.ResolvedType->Kind == TypeKind::Array ||
         value.ResolvedType->Kind == TypeKind::Record);
    if (isAggregate && val->getType()->isPointerTy()) {
        builder.CreateMemCpy(ptr, llvm::MaybeAlign(), val, llvm::MaybeAlign(),
                             mod->getDataLayout().getTypeAllocSize(ty));
        return;
    }
    builder.CreateStore(coerceToType(val, ty), ptr);
}

// The 'value' clause the denoter carries, reached through however many names
// stand between the declaration and the type: `type k = integer value 3;
// type j = k; var n: j` begins at 3 like every other variable of k does.
// EP §6.4.7: the body denoter a schema instantiation stands for, or null when
// this is not one.  Only a SchemaTypeNode -- `t(5)` -- is answered: an
// UNDISCRIMINATED `^t` reaches its body through a NamedTypeNode and has no
// static layout to initialize through, which is a separate piece of work.
const TypeNode* Codegen::Impl::schemaInstanceBody(const TypeNode* tn) const {
    auto* stn = llvm::dyn_cast_or_null<SchemaTypeNode>(tn);
    if (!stn || !stn->ResolvedBody) return nullptr;
    return schemaBodyNodeOf(*stn->ResolvedBody);
}

const ExprNode* Codegen::Impl::writtenInitialState(const TypeNode* tn,
                                                   const TypeNode** carrier) const {
    for (int hops = 0; tn && hops < 32; ++hops) {
        if (tn->InitialState) {
            if (carrier) *carrier = tn;
            return tn->InitialState.get();
        }
        auto* named = llvm::dyn_cast<NamedTypeNode>(tn);
        if (!named) break;
        // Sema's answer, recorded where the name was written.  This walked
        // `typeAliases` -- flat, keyed by spelling, no scope chain -- so every
        // hop was re-bound in whatever procedure was being lowered: an inner
        // `ca` supplied the value clause AND its length for an outer type's
        // variable, memcpying 400 bytes into a 4-byte allocation.  It also ran
        // the other way, dropping an initialization the type really has when
        // the inner homonym had none.
        if (!named->Denotes || named->Denotes == tn) break;
        tn = named->Denotes;
    }
    return nullptr;
}

// EP §6.6: a structured type begins in the state its components begin in, so
// a record with one initialized field has an initial state of its own even
// though nothing was written beside the record.
/// The shape a denoter has, resolving a type NAME the way Sema resolved it.
///
/// Both callers reach their argument by recursing into a FOREIGN declaration --
/// a record's `fd.Type.get()`, an array's `atn->Element.get()` -- so the names
/// in it were written in that declaration's scope and not in the procedure
/// being lowered.  They used denoterOf, which walks `typeAliases`: flat, keyed
/// by spelling, rebuilt per procedure.  A homonym in the procedure therefore
/// supplied the shape, and the record branch below then called layoutOf on the
/// WRONG record and GEP'd the real object with its offsets -- a `value` clause
/// seven fields deep in an inner homonym wrote 999 at offset 56 of a 16-byte
/// allocation.  It runs the other way too, dropping an initialization the type
/// really has, which is silent.
///
/// The sibling writtenInitialState was fixed for exactly this and follows
/// NamedTypeNode::Denotes; this is the descent beside it, which was not.
const TypeNode* Codegen::Impl::initialStateShapeOf(const TypeNode* tn) const {
    for (int hops = 0; tn && hops < 32; ++hops) {
        auto* named = llvm::dyn_cast<NamedTypeNode>(tn);
        if (!named) return tn;
        if (!named->Denotes || named->Denotes == tn) return tn;
        tn = named->Denotes;
    }
    return tn;
}

bool Codegen::Impl::hasInitialState(const TypeNode* tn, int depth) const {
    if (!tn || depth > 16) return false;
    if (writtenInitialState(tn)) return true;
    const TypeNode* shape = initialStateShapeOf(tn);
    // EP §6.4.7 with §6.6: `t(5)` is written as a schema instantiation, and
    // what it denotes is the schema's body.  Neither this nor emitInitialState
    // looked through one, so a `value` clause inside a schema body was dropped
    // and `record k: integer value 9 end` began at zero in every instance.
    if (const TypeNode* body = schemaInstanceBody(shape))
        return hasInitialState(body, depth + 1);
    if (auto* rtn = llvm::dyn_cast_or_null<RecordTypeNode>(shape)) {
        for (const auto& fd : rtn->Fields)
            if (hasInitialState(fd.Type.get(), depth + 1)) return true;
        return false;
    }
    if (auto* atn = llvm::dyn_cast_or_null<ArrayTypeNode>(shape))
        return hasInitialState(atn->Element.get(), depth + 1);
    return false;
}

void Codegen::Impl::emitInitialState(llvm::Value* ptr, llvm::Type* ty,
                                     const TypeNode* tn, int depth,
                                     const Type* semaRec) {
    if (!ptr || !tn || depth > 16) return;

    const TypeNode* carrier = nullptr;
    if (const ExprNode* init = writtenInitialState(tn, &carrier)) {
        storeInitialValue(ptr, ty, carrier, *init);
        return;
    }

    const TypeNode* shape = initialStateShapeOf(tn);
    // See hasInitialState.  The body is laid out under THIS instantiation's
    // discriminants: a body holding a `string(n)` has a different shape per
    // instance, and initializing it through the unbound layout would write the
    // fields at offsets belonging to no instance at all.
    if (auto* stn = llvm::dyn_cast_or_null<SchemaTypeNode>(shape)) {
        const TypeNode* body = schemaInstanceBody(stn);
        if (!body) return;
        if (stn->ResolvedBody && stn->ResolvedBody->SchemaBody) {
            SchemaBindingScope bind(*cgTypes_, *stn->ResolvedBody->SchemaBody);
            // #197: stn->ResolvedBody is THIS denoter's own cached
            // resolution, good here because it was just re-established by
            // the SchemaBindingScope above -- carry it down as semaRec so
            // the record branch below can hand it to layoutOf instead of
            // laying the body out from its declaration node alone.
            emitInitialState(ptr, ty, body, depth + 1,
                              stn->ResolvedBody->SchemaBody.get());
        }
        return;
    }
    if (auto* rtn = llvm::dyn_cast_or_null<RecordTypeNode>(shape)) {
        // #197: semaRec (this instance's own Sema record, when the recursion
        // has one) makes layoutOf prefer Sema's per-field types over each
        // field's own denoter.  Without it, a field that is itself a schema
        // instantiation -- `x: inner(n)` -- is sized from whichever
        // instantiation of `inner` Sema resolved last across the WHOLE
        // program, not this one, shifting every GEP into the fields that
        // follow it (issue #197; see CGTypes.cpp's R4 comment on layoutOf
        // for the sibling bug this matches).
        const auto& L  = layoutOf(*rtn, semaRec);
        auto*       st = L.Ty;
        for (const auto& fd : rtn->Fields) {
            if (!hasInitialState(fd.Type.get(), depth + 1)) continue;
            for (const auto& nm : fd.Names) {
                auto fit = L.Fields.find(toLower(nm));
                if (fit == L.Fields.end()) continue;
                const auto& P = fit->second;
                if (P.Index >= st->getNumElements()) continue;
                llvm::Value* gep = builder.CreateGEP(st, ptr,
                    {llvm::ConstantInt::get(i32Ty, 0),
                     llvm::ConstantInt::get(i32Ty, P.Index)}, "init.f");
                if (P.InVariant && P.Offset != 0)
                    gep = builder.CreateConstGEP1_64(i8Ty, gep, P.Offset, "init.f");
                emitInitialState(gep, P.Ty, fd.Type.get(), depth + 1);
            }
        }
        return;
    }

    if (auto* atn = llvm::dyn_cast_or_null<ArrayTypeNode>(shape)) {
        if (!hasInitialState(atn->Element.get(), depth + 1)) return;
        auto    range = arrayIndexRange(*atn);
        int64_t lo    = range ? range->first  : 0;
        int64_t hi    = range ? range->second : -1;
        if (hi < lo) return;
        auto* elemTy = llvmTypeOfNode(*atn->Element);
        auto* arrTy  = llvm::ArrayType::get(
            elemTy, static_cast<uint64_t>(hi - lo + 1));
        const int64_t count = hi - lo + 1;
        // Every element begins the same way, so one body serves them all; it
        // is written out per element only while that stays the smaller of the
        // two, and run as a loop once the array is long enough to matter.
        if (count <= 8) {
            for (int64_t i = 0; i < count; ++i) {
                auto* gep = builder.CreateGEP(arrTy, ptr,
                    {llvm::ConstantInt::get(i64Ty, 0),
                     llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(i))},
                    "init.e");
                emitInitialState(gep, elemTy, atn->Element.get(), depth + 1);
            }
            return;
        }
        auto* fn   = builder.GetInsertBlock()->getParent();
        auto* head = llvm::BasicBlock::Create(ctx, "init.head", fn);
        auto* body = llvm::BasicBlock::Create(ctx, "init.body", fn);
        auto* done = llvm::BasicBlock::Create(ctx, "init.done", fn);
        auto* iv   = createEntryAlloca(i64Ty, "init.i");
        builder.CreateStore(llvm::ConstantInt::get(i64Ty, 0), iv);
        builder.CreateBr(head);
        builder.SetInsertPoint(head);
        auto* i = builder.CreateLoad(i64Ty, iv, "init.i.cur");
        builder.CreateCondBr(
            builder.CreateICmpSLT(i, llvm::ConstantInt::get(i64Ty, count)),
            body, done);
        builder.SetInsertPoint(body);
        auto* gep = builder.CreateGEP(arrTy, ptr,
            {llvm::ConstantInt::get(i64Ty, 0), i}, "init.e");
        emitInitialState(gep, elemTy, atn->Element.get(), depth + 1);
        builder.CreateStore(
            builder.CreateAdd(i, llvm::ConstantInt::get(i64Ty, 1)), iv);
        builder.CreateBr(head);
        builder.SetInsertPoint(done);
    }
}

// EP §6.6 with §6.4.7: emitInitialState's counterpart for a schema instance
// new() just allocated.  See this function's declaration (CodeGenImpl.h) and
// emitSchemaInitialStateAt below for why it cannot simply call emitInitialState.
void Codegen::Impl::emitSchemaInitialState(llvm::Value* bodyAddr,
                                           const plang::Type& schema,
                                           const std::vector<llvm::Value*>& discs) {
    const TypeNode* body = schemaBodyNodeOf(schema);
    if (!body || !hasInitialState(body)) return;
    emitSchemaInitialStateAt(bodyAddr, discs, body, 0);
}

// The run-time-layout sibling of emitInitialState, above: that walk answers
// for a schema bound to compile-time-constant discriminants (a declared
// `var v: t(20)`, by way of a SchemaBindingScope over the static llvm::Type*
// / GEP-by-index machinery in this file) but new()'s discriminants need not
// be constants (EP §6.7.5.3), so this one asks SchemaLayoutEngine's run-time
// offsets instead -- the same ones a read of the object (schemaPathOf) asks
// for -- and stores through a byte address rather than indexing a static
// struct.  hasInitialState/writtenInitialState/storeInitialValue are shared
// with the static walk unchanged: none of the three looks at layout, only at
// a TypeNode's shape and the value to put there.
void Codegen::Impl::emitSchemaInitialStateAt(llvm::Value* addr,
                                             const std::vector<llvm::Value*>& discs,
                                             const TypeNode* tn, int depth) {
    if (!addr || !tn || depth > 16) return;

    const TypeNode* carrier = nullptr;
    if (const ExprNode* init = writtenInitialState(tn, &carrier)) {
        // A schema-body string's capacity is a run-time value even where
        // nothing else about this particular field varies -- R3 (SemaType.cpp)
        // records every extent written inside a schema body as a closed form,
        // whether or not it happens to read a discriminant -- so the static
        // declaredStrCapacity() storeInitialValue relies on (whichever
        // instantiation Sema resolved last) is not safe to reuse here; see
        // SchemaLayoutEngine::rtSizeOfTypeNode's own comment on that exact
        // class of bug.
        if (auto* st = llvm::dyn_cast<StringTypeNode>(carrier);
                st && st->ExtentLow) {
            if (auto* cap = emitExtentForm(*st->ExtentLow, discs)) {
                emitStrStore(addr, cap, *init);
                return;
            }
        }
        storeInitialValue(addr, llvmTypeOfNode(*tn), carrier, *init);
        return;
    }

    // A nested schema instantiation -- `outer(n) = record x: inner(n) end`
    // -- is descended exactly as a read of it is (SchemaAccess::schemaPathOf):
    // its own extents are forms over ITS discriminants, arithmetic over the
    // enclosing ones, and have to be evaluated against those and not against
    // the ones this call was entered with.
    const TypeNode* shape = initialStateShapeOf(tn);
    auto [ref, decl] = descendIntoInstantiation(
        SchemaRef{nullptr, addr, discs}, addr, shape);
    if (decl != shape) {
        // writtenInitialState follows a NAMED alias (NamedTypeNode::Denotes)
        // but not a schema-instantiation reference, so a `value` clause on
        // the far side of one -- `inner(n) = string(n) value 'hi'` used as
        // `x: inner(n)` -- is not yet seen; re-enter now that decl is past
        // the hop, this time as an ordinary leaf/shape under its own discs.
        emitSchemaInitialStateAt(ref.data, ref.discs, decl, depth + 1);
        return;
    }

    if (auto* rtn = llvm::dyn_cast_or_null<RecordTypeNode>(decl)) {
        // Not walkVariant's territory: a variant field is exactly what the
        // static emitInitialState above does not reach either (it iterates
        // rtn->Fields alone), so hasInitialState never asks for one and this
        // never needs to compute one's offset.
        for (const auto& fd : rtn->Fields) {
            if (!hasInitialState(fd.Type.get(), depth + 1)) continue;
            for (const auto& nm : fd.Names) {
                llvm::Value* off;
                {
                    RtDiscScope disc(*this, ref.discs);
                    off = rtFieldOffset(*rtn, nm);
                }
                auto* fldAddr = builder.CreateGEP(i8Ty, ref.data, {off}, "sch.init.f");
                emitSchemaInitialStateAt(fldAddr, ref.discs, fd.Type.get(), depth + 1);
            }
        }
        return;
    }

    if (auto* atn = llvm::dyn_cast_or_null<ArrayTypeNode>(decl)) {
        if (!hasInitialState(atn->Element.get(), depth + 1)) return;
        llvm::Value* lo;
        llvm::Value* hi;
        llvm::Value* stride;
        {
            RtDiscScope disc(*this, ref.discs);
            auto bounds = rtIndexBounds(*atn);
            if (!bounds) return;
            lo     = bounds->first;
            hi     = bounds->second;
            stride = alignUpV(rtSizeOfTypeNode(atn->Element.get()),
                              rtAlignOfTypeNode(atn->Element.get()));
        }
        // count may come out <= 0 for an empty range -- unlike the static
        // walk's compile-time `if (hi < lo) return;`, lo/hi are run-time
        // values here, so the loop below is entered and simply runs zero
        // times, which is the answer an empty range needs.
        auto* count = builder.CreateAdd(builder.CreateSub(hi, lo),
                                        i64c(1), "sch.init.count");
        auto* fn   = builder.GetInsertBlock()->getParent();
        auto* head = llvm::BasicBlock::Create(ctx, "sch.init.head", fn);
        auto* loop = llvm::BasicBlock::Create(ctx, "sch.init.body", fn);
        auto* done = llvm::BasicBlock::Create(ctx, "sch.init.done", fn);
        auto* iv   = createEntryAlloca(i64Ty, "sch.init.i");
        builder.CreateStore(i64c(0), iv);
        builder.CreateBr(head);
        builder.SetInsertPoint(head);
        auto* i = builder.CreateLoad(i64Ty, iv, "sch.init.i.cur");
        builder.CreateCondBr(builder.CreateICmpSLT(i, count), loop, done);
        builder.SetInsertPoint(loop);
        auto* off      = builder.CreateMul(i, stride, "sch.init.off");
        auto* elemAddr = builder.CreateGEP(i8Ty, ref.data, {off}, "sch.init.e");
        emitSchemaInitialStateAt(elemAddr, ref.discs, atn->Element.get(), depth + 1);
        builder.CreateStore(builder.CreateAdd(i, i64c(1)), iv);
        builder.CreateBr(head);
        builder.SetInsertPoint(done);
        return;
    }
}

// EP §6.8.2: a constant may be written as an expression that only a running
// program can work out — `const a = ord('x')`.  In a program that is done in
// main, where the value is wanted; a module has no main, and the value has to
// outlive the initialiser and be reachable from another object file besides,
// so it is given storage of its own and filled in by the module's initialiser.
void Codegen::Impl::emitRuntimeConst(const ConstDef& cd) {
    llvm::Type* ty = (cd.Value->ResolvedType && !cd.Value->ResolvedType->isError())
                   ? llvmTypeOfSemaType(*cd.Value->ResolvedType) : i64Ty;
    const std::string gname = globalPrefix + cd.Name;
    auto* gv = mod->getGlobalVariable(gname);
    if (!gv)
        gv = new llvm::GlobalVariable(*mod, ty, /*isConst=*/false,
                                      llvm::GlobalValue::ExternalLinkage,
                                      llvm::Constant::getNullValue(ty), gname);
    defVar(cd.Name, gv, ty, nullptr);
    if (!currentUnit_.empty())
        moduleGlobals_[currentUnit_ + "." + toLower(cd.Name)] =
            VarEntry{ gv, ty, nullptr };
    runtimeConsts_.push_back(&cd);
}

// The value of each such constant, worked out in declaration order so that one
// written in terms of another finds it already standing.
void Codegen::Impl::emitRuntimeConstInits() {
    for (const ConstDef* cd : runtimeConsts_) {
        const auto* ve = findVar(cd->Name);
        if (!ve) continue;
        const auto& T = cd->Value->ResolvedType;
        if (T && T->Kind == TypeKind::VarString) {
            builder.CreateCall(
                getStrFn("plang_str_init", llvm::Type::getVoidTy(ctx),
                         {ptrTy, i64Ty}),
                {ve->ptr, llvm::ConstantInt::get(i64Ty, T->StrCapacity, true)});
            emitStrStore(ve->ptr, T->StrCapacity, *cd->Value);
            continue;
        }
        if (auto* v = emitExpr(*cd->Value))
            builder.CreateStore(coerceToType(v, ve->type), ve->ptr);
    }
    runtimeConsts_.clear();
}

llvm::Type* Codegen::Impl::structuredConstType(const ConstDef& cd,
                                               const TypeNode*& tn) {
    // The named type carries the bounds, so the constant is given the very
    // declaration a variable of that type would have and reads the same way.
    const auto* sv = llvm::cast<StructuredValueExpr>(cd.Value.get());
    tn = nullptr;
    if (auto it = typeAliases.find(toLower(sv->TypeName)); it != typeAliases.end())
        tn = it->second;
    if (tn) return llvmTypeOfNode(*tn);
    return sv->ResolvedType ? llvmTypeOfSemaType(*sv->ResolvedType) : i64Ty;
}

void Codegen::Impl::emitStructuredConst(const ConstDef& cd) {
    const TypeNode* tn = nullptr;
    llvm::Type*     ty = structuredConstType(cd, tn);
    auto* gv = new llvm::GlobalVariable(*mod, ty, /*isConst=*/false,
                                        llvm::GlobalValue::ExternalLinkage,
                                        llvm::Constant::getNullValue(ty),
                                        globalPrefix + cd.Name);
    defVar(cd.Name, gv, ty, tn);
    structuredConsts_.push_back(&cd);
}

void Codegen::Impl::emitGlobalVarInits(const BlockNode& block) {
    // EP §6.8.7: fill in the structured constants before anything reads them.
    for (const ConstDef* cd : structuredConsts_) {
        const auto* ve = findVar(cd->Name);
        auto* val = emitExpr(*cd->Value);
        if (!ve || !val) continue;
        builder.CreateMemCpy(ve->ptr, llvm::MaybeAlign(), val, llvm::MaybeAlign(),
                             mod->getDataLayout().getTypeAllocSize(ve->type));
    }

    // Storage for program-level variables already exists as module globals, so
    // only the runtime initialization still has to be emitted into main.
    for (const auto& vg : block.Vars) emitGlobalVarInit(vg);
}

void Codegen::Impl::emitGlobalVarInit(const VarGroup& vg) {
    // String(N) global init.
    if (int64_t cap = declaredStrCapacity(vg.Type.get()); cap > 0) {
        auto* fn = getStrFn("plang_str_init",
            llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty});
        for (const auto& nm : vg.Names) {
            const auto* ve = findVar(nm);
            if (ve) builder.CreateCall(fn, {ve->ptr,
                llvm::ConstantInt::get(i64Ty, cap, true)});
        }
    }
    emitVarValueInit(vg);
}

// EP §6.11.2: a module's own variables and structured constants are brought
// up by its initialiser, which runs before anything that imports it and
// before its own `to begin do`.  Only a program's globals are brought up in
// main, and a module's were being brought up nowhere at all: a `value` clause
// on a module variable, and a structured constant a module declared, were
// read, checked, given storage, and then left as zeroes.
void Codegen::Impl::emitModuleGlobalInits(const BlockNode& own,
                                          const BlockNode* iface) {
    // The constants first: a variable's initial value may be written in terms
    // of one of them.
    emitRuntimeConstInits();
    emitGlobalVarInits(own);
    if (!iface) return;
    auto declaredInOwn = [&](const std::string& nm) {
        for (const auto& vg : own.Vars)
            for (const auto& n : vg.Names)
                if (eqCI(n, nm)) return true;
        return false;
    };
    // What the heading declares is the block's too, and this module is where
    // it lives, so this is where it is brought up.
    for (const auto& vg : iface->Vars) {
        bool anyOwn = false;
        for (const auto& nm : vg.Names) anyOwn = anyOwn || declaredInOwn(nm);
        if (!anyOwn) emitGlobalVarInit(vg);
    }
}

void Codegen::Impl::emitMain(const BlockNode& block,
                             const std::vector<std::string>& fileParams,
                             const std::vector<std::string>& initModules) {
    CGFunction curFn(*this);
    auto  savedIP        = builder.saveIP();

    curFuncName  = "main";
    namePrefix   = PlangProcPrefix;
    curRetType   = nullptr;
    curRetAlloca = nullptr;
    curRetSemaType = nullptr;

    // The standard C `int main(int argc, char** argv)` shape, UNCONDITIONALLY
    // for every dialect -- not just Turbo's own ParamCount/ParamStr, which
    // are the only PASCAL-level feature that reads argc/argv back (through
    // plang_set_args just below and runtime/plang_sys.cpp's own g_argc/
    // g_argv).  ISO 7185 and Extended Pascal programs compile through this
    // exact same emitMain and simply never read what plang_set_args stored;
    // a single, dialect-independent entry-point ABI here is what lets one
    // `int main(int, char**)` -- the shape the C runtime environment (glibc's
    // own _start/__libc_start_main) actually calls -- serve all three,
    // rather than needing two different generated `main`s.
    auto* funcTy = llvm::FunctionType::get(i32Ty, {i32Ty, ptrTy}, false);
    auto* func   = llvm::Function::Create(funcTy, llvm::Function::ExternalLinkage,
                                           "main", mod.get());
    func->getArg(0)->setName("argc");
    func->getArg(1)->setName("argv");
    curFunc = func;

    // -g: the program's own top-level scope, so a breakpoint on the first
    // executable statement and a backtrace's outermost frame both land on
    // something named "main", matching what a debugger already expects
    // there regardless of source language.  The block's own Loc points at
    // its first declaration, not its 'begin' -- Body->Loc is where a reader
    // (and a debugger opening on this frame) would actually expect main to
    // start.  dbgScope's destructor restores the enclosing (null) scope at
    // the end of this function.
    const SourceLocation startLoc = block.Body ? block.Body->Loc : block.Loc;
    CGDebugInfo::ScopeGuard dbgScope(
        *dbgInfo_, dbgInfo_->emitFunctionStart(func, dbgInfo_->getFile(), "main", startLoc));

    auto* entry = llvm::BasicBlock::Create(ctx, "entry", func);
    builder.SetInsertPoint(entry);

    // Literally the first instruction of every compiled program's own
    // `main`, every dialect alike (see this function's own funcTy comment
    // above): stores argc/argv where Turbo's ParamCount/ParamStr can read
    // them back later, however deep into the program either is asked for.
    // A plain runtime global rather than threading argc/argv through every
    // function that might want them -- runtime/plang_sys.cpp's own
    // g_argc/g_argv, exactly the same pattern plang_tp_exitcode's own
    // comment already establishes for a Turbo predefined identifier's
    // storage.
    builder.CreateCall(
        getExternFnN("plang_set_args", llvm::Type::getVoidTy(ctx), {i32Ty, ptrTy}),
        {func->getArg(0), func->getArg(1)});

    // TP-only: ExitProc, hooked into the ALREADY-WORKING
    // plang_module_finals_run/plang_halt chain (issue #242) exactly once,
    // here, near the very start of the program -- before anything the
    // program itself does could possibly reach a Halt/RunError/normal-exit
    // path.  After this one registration, every existing call to
    // plang_module_finals_run (plang_halt, plang_tp_runerror, and this same
    // function's own end-of-program call just below) already runs whatever
    // routine the program has assigned to ExitProc by the time each of those
    // is reached, with no separate "and also run ExitProc" step needed at
    // any of the three -- see plang_tp_run_exitproc's own comment
    // (runtime/plang_sys.cpp) for the rest of this design.
    if (langOpts.turbo()) {
        auto* runExitProc =
            getExternFnN("plang_tp_run_exitproc", llvm::Type::getVoidTy(ctx), {});
        auto* pushFinal =
            getExternFnN("plang_module_final_push", llvm::Type::getVoidTy(ctx), {ptrTy});
        builder.CreateCall(pushFinal, {runExitProc});
    }

    // TP-only: where a bare `Exit` at the program's own top level branches
    // to -- confirmed against `fpc -Mtp` that Exit at program scope ends the
    // program (same as falling off the end of the body) rather than being
    // refused; see CGFunction::ExitBB's own comment for why this is created
    // unconditionally.
    curFn.ExitBB = llvm::BasicBlock::Create(ctx, "exit", func);

    pushScope();
    // Program-level variables were already given storage as module globals by
    // emitGlobals(), and procedures resolve them to those globals.  Allocating
    // them again here would shadow the globals inside main only, so writes made
    // by a procedure would not be visible to the program body and vice versa.
    emitBlockDecls(block);
    emitFileParamBinds(fileParams);
    // -std=turbo only: Input/Output are bound unconditionally, whether or
    // not this program's own heading lists them -- see this function's own
    // comment (CodeGenProcs.cpp) for why it is separate from
    // emitFileParamBinds just above, which is a no-op under Turbo.
    emitTurboStdIOBinds();
    // The constants first, as emitModuleGlobalInits already does for a
    // module: a variable's initial value may be written in terms of one, and
    // any constant emitGlobals could not fold at compile time (registered by
    // emitRuntimeConst) still needs its value computed somewhere before
    // anything reads it.
    emitRuntimeConstInits();
    emitGlobalVarInits(block);

    // EP §6.11: bring the modules up before the program body runs.  Each
    // initialiser starts the ones it imports and guards against running twice,
    // so calling them in any order gives the same result.
    for (const auto& ModName : initModules)
        builder.CreateCall(moduleInitFn(ModName), {});

    // The scope was opened before the procedures were emitted, so that one of
    // them could take the address of the buffer; here is where the program is
    // far enough along for a goto to land in it.
    emitLabelLanding();

    if (block.Body) emitCompound(*block.Body);
    closeLabelScope();

    // TP-only: Exit (CGProcCall) branches to ExitBB from anywhere in the
    // program body; the ordinary "fall off the end" path (this brIfNeeded)
    // is routed through the same block, so the epilogue below runs in
    // exactly one place regardless of which of the two reached it.
    brIfNeeded(exitBlock());
    builder.SetInsertPoint(exitBlock());

    // EP §6.11.2: the finalisers run in the reverse of the order the
    // initialisers did.  A module is initialized after the ones it imports, so
    // running the finalisers forward would tear down a module while one that
    // depends on it is still to be finalized.  Each initialiser registered its
    // own finaliser as it completed, which is the order the runtime unwinds.
    builder.CreateCall(getExternFnN("plang_module_finals_run",
                                     llvm::Type::getVoidTy(ctx), {}), {});

    if (!isTerminated()) {
        // -std=turbo only: the process's exit status is whatever the
        // program last assigned to ExitCode (0 if it never did -- see
        // plang_tp_exitcode's own zero-initializer, runtime/plang_sys.cpp).
        // ISO 7185 and Extended Pascal have no such variable and keep
        // returning the fixed 0 they always have; emitPredefinedGlobals
        // only ever registers ExitCode under Turbo, so this findVar is
        // guaranteed to succeed exactly when this branch is taken.
        if (langOpts.turbo()) {
            auto* ev = findVar("ExitCode");
            if (!ev) codegenICE("main: ExitCode has no storage under -std=turbo");
            auto* val = builder.CreateLoad(ev->type, ev->ptr, "exitcode");
            builder.CreateRet(builder.CreateSExtOrTrunc(val, i32Ty, "exitcode.i32"));
        } else {
            builder.CreateRet(llvm::ConstantInt::get(i32Ty, 0));
        }
    }

    popScope();

    // curFunc/curRetAlloca/curRetType/curFuncName/namePrefix are restored
    // unconditionally by curFn's destructor, at the end of this scope.
    builder.restoreIP(savedIP);
}

// ---------------------------------------------------------------------------
// EP §6.11: Module lifecycle functions (to begin do / to end do)
// ---------------------------------------------------------------------------

llvm::Function* Codegen::Impl::moduleInitFn(const std::string& moduleName) {
    const std::string fnName = "__plang_init_" + toLower(moduleName);
    if (auto* f = mod->getFunction(fnName)) return f;
    auto* funcTy = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {}, false);
    return llvm::Function::Create(funcTy, llvm::Function::ExternalLinkage,
                                   fnName, mod.get());
}

// A module is initialized after every module it imports, and at most once
// however many paths reach it (EP §6.11.2).  The order cannot be settled by
// the program, which sees only its own import clauses and cannot tell what a
// separately compiled module imports in turn.  So each initialiser guards
// itself and calls the initialisers of its own imports first: the order falls
// out of the recursion, and works the same whether the modules were compiled
// together or apart.
std::string Codegen::Impl::emitModuleInitFn(const ModuleNode& modNode) {
    const std::string unit   = toLower(modNode.Name);
    const std::string fnName = "__plang_init_" + unit;

    // namePrefix is left as the caller set it, same as
    // emitModuleLifecycleFn below -- CGFunction still snapshots/restores
    // it unconditionally, which is a correct no-op here since nothing in
    // this function ever assigns to it.
    CGFunction curFn(*this);
    auto  savedIP        = builder.saveIP();

    curFuncName  = fnName;
    curRetType   = nullptr;
    curRetAlloca = nullptr;

    // The declaration may already stand here, made by another module's
    // initialiser that imports this one.
    auto* func = moduleInitFn(modNode.Name);
    curFunc = func;

    // -g: this is EP §6.11.2's 'to begin do'/'to end do', a synthesized
    // function with no Pascal-level name of its own -- described rather
    // than named, so a backtrace through it says what it is.  InitStmt's
    // own Loc is the 'to begin do' block itself, a better start line than
    // modNode.Loc's 'module ... implementation', when there is one.
    // dbgScope's destructor restores the enclosing scope at the end of
    // this function.
    const SourceLocation startLoc =
        modNode.InitStmt ? modNode.InitStmt->Loc : modNode.Loc;
    CGDebugInfo::ScopeGuard dbgScope(
        *dbgInfo_, dbgInfo_->emitFunctionStart(
            func, dbgInfo_->getFile(), "initialization of " + modNode.Name, startLoc));

    // The guard lives with the initialiser, in whichever object defines it.
    auto* done = new llvm::GlobalVariable(
        *mod, i8Ty, /*isConst=*/false, llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantInt::get(i8Ty, 0), "__plang_initdone_" + unit);

    auto* entry = llvm::BasicBlock::Create(ctx, "entry", func);
    auto* run   = llvm::BasicBlock::Create(ctx, "run", func);
    auto* ret   = llvm::BasicBlock::Create(ctx, "done", func);

    builder.SetInsertPoint(entry);
    auto* seen = builder.CreateLoad(i8Ty, done, "initdone");
    builder.CreateCondBr(
        builder.CreateICmpNE(seen, llvm::ConstantInt::get(i8Ty, 0)), ret, run);

    builder.SetInsertPoint(run);
    // Set before recursing, so a cycle of imports terminates rather than
    // overflowing the stack.  Sema rejects those, but this function is also
    // reachable from objects it never saw.
    builder.CreateStore(llvm::ConstantInt::get(i8Ty, 1), done);
    // A module written with a separate interface/implementation is two AST
    // nodes for one Pascal-level declaration, and 'import' may be written on
    // either half (most naturally the interface's, since that is where a
    // name a signature mentions has to already be visible).  modNode is the
    // implementation half here, so moduleIfaceImports_ -- the matching
    // interface's own import clauses, set by the caller while this module's
    // scope is current -- has to be walked too, or an import written only on
    // the interface would silently never be brought up when this module is
    // its own separate translation unit (issue #126).  Calling
    // moduleInitFn() twice for a name listed on both halves is harmless: the
    // guard above makes every init function idempotent.
    std::vector<const std::vector<ImportClause>*> importLists{&modNode.Imports};
    if (moduleIfaceImports_) importLists.push_back(moduleIfaceImports_);
    for (const auto* clauses : importLists)
        for (const auto& clause : *clauses)
            if (!isBuiltinModule(clause.ModuleName))
                builder.CreateCall(moduleInitFn(clause.ModuleName), {});

    // The module's own globals come up here, after the modules it imports and
    // before its `to begin do`, which may well read them.
    if (modNode.Body) emitModuleGlobalInits(*modNode.Body, moduleIfaceBlock_);

    pushScope();
    if (modNode.InitStmt) emitStmt(modNode.InitStmt.get());
    popScope();

    // Registered after the body, so finalization unwinds in the reverse of the
    // order the modules actually came up in.
    if (modNode.FinalStmt) {
        auto* push = getExternFnN("plang_module_final_push",
                                   llvm::Type::getVoidTy(ctx), {ptrTy});
        builder.CreateCall(push, {mod->getFunction("__plang_fini_" + unit)});
    }
    if (!isTerminated()) builder.CreateBr(ret);

    builder.SetInsertPoint(ret);
    builder.CreateRetVoid();

    // curFunc/curRetAlloca/curRetType/curFuncName are restored
    // unconditionally by curFn's destructor, at the end of this scope.
    builder.restoreIP(savedIP);
    return fnName;
}

// Turbo Tier 4, Cluster A item 2: see this method's own declaration
// (CodeGenImpl.h) for the whole design.  Deliberately does NOT call any
// OTHER unit's own __plang_init_<name> the way emitModuleInitFn calls
// moduleInitFn(clause.ModuleName) for each of an EP module's imports: a
// Turbo unit 'uses'd only through Sema::loadUnitInterfaceExports' .pas
// fallback (no .tui was published, so Sema read its INTERFACE straight from
// source for type-checking only -- see that function's own comment on why
// the fallback exists) has no real object file anywhere in this compile,
// and calling its __plang_init_<name> here would leave an undefined
// reference for the linker to fail on even though the compile itself looked
// clean.  So this only ever brings up the unit's OWN runtime-computed
// constants/variables and its own InitBody -- automatically running a
// USED unit's own initializer is left for a later item once every 'uses' in
// a build is known to have a real published .tui/.o rather than possibly
// being the fallback path (see this item's own report for exactly what this
// means in practice: a used unit's foldable interface constants, extern
// variable storage, and extern procedure/function calls all work today; a
// used unit's runtime-computed constant/variable value and its
// `begin...end` initialization section do not yet run automatically).
std::string Codegen::Impl::emitUnitInitFn(const UnitNode& unit) {
    const std::string unitLower = toLower(unit.Name);
    const std::string fnName    = "__plang_init_" + unitLower;

    CGFunction curFn(*this);
    auto  savedIP = builder.saveIP();

    curFuncName  = fnName;
    curRetType   = nullptr;
    curRetAlloca = nullptr;

    auto* func = moduleInitFn(unit.Name);
    curFunc = func;

    const SourceLocation startLoc = unit.InitBody ? unit.InitBody->Loc : unit.Loc;
    CGDebugInfo::ScopeGuard dbgScope(
        *dbgInfo_, dbgInfo_->emitFunctionStart(
            func, dbgInfo_->getFile(), "initialization of " + unit.Name, startLoc));

    auto* done = new llvm::GlobalVariable(
        *mod, i8Ty, /*isConst=*/false, llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantInt::get(i8Ty, 0), "__plang_initdone_" + unitLower);

    auto* entry = llvm::BasicBlock::Create(ctx, "entry", func);
    auto* run   = llvm::BasicBlock::Create(ctx, "run", func);
    auto* ret   = llvm::BasicBlock::Create(ctx, "done", func);

    builder.SetInsertPoint(entry);
    auto* seen = builder.CreateLoad(i8Ty, done, "initdone");
    builder.CreateCondBr(
        builder.CreateICmpNE(seen, llvm::ConstantInt::get(i8Ty, 0)), ret, run);

    builder.SetInsertPoint(run);
    builder.CreateStore(llvm::ConstantInt::get(i8Ty, 1), done);

    if (unit.ImplementationBlock)
        emitModuleGlobalInits(*unit.ImplementationBlock, unit.InterfaceBlock.get());

    pushScope();
    if (unit.InitBody) emitCompound(*unit.InitBody);
    popScope();

    if (!isTerminated()) builder.CreateBr(ret);

    builder.SetInsertPoint(ret);
    builder.CreateRetVoid();

    builder.restoreIP(savedIP);
    return fnName;
}

void Codegen::Impl::emitModuleLifecycleFn(const std::string& fnName,
                                           const StmtNode& stmt) {
    // Save outer context. namePrefix is left as the caller set it: the
    // block belongs to the module, and the procedures it calls are
    // mangled with that module's name -- CGFunction still snapshots/
    // restores it unconditionally, a correct no-op since nothing here
    // ever assigns to it.
    CGFunction curFn(*this);
    auto  savedIP        = builder.saveIP();

    curFuncName  = fnName;
    curRetType   = nullptr;
    curRetAlloca = nullptr;

    auto* voidTy = llvm::Type::getVoidTy(ctx);
    auto* funcTy = llvm::FunctionType::get(voidTy, {}, false);
    auto* func   = llvm::Function::Create(funcTy, llvm::Function::InternalLinkage,
                                           fnName, mod.get());
    curFunc = func;

    // -g: EP §6.11.2's 'to begin do'/'to end do', the same as
    // emitModuleInitFn's own initializer -- this is its finalizer,
    // reached the same way, from a synthesized name with no Pascal-level
    // declaration of its own.  dbgScope's destructor restores the
    // enclosing scope at the end of this function.
    CGDebugInfo::ScopeGuard dbgScope(
        *dbgInfo_, dbgInfo_->emitFunctionStart(func, dbgInfo_->getFile(), fnName, stmt.Loc));

    auto* entry = llvm::BasicBlock::Create(ctx, "entry", func);
    builder.SetInsertPoint(entry);

    pushScope();
    emitStmt(&stmt);
    popScope();

    if (!isTerminated())
        builder.CreateRetVoid();

    // curFunc/curRetAlloca/curRetType/curFuncName are restored
    // unconditionally by curFn's destructor, at the end of this scope.
    builder.restoreIP(savedIP);
}
