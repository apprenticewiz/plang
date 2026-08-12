#include "CodegenImpl.h"
using namespace plang;

// ====================================================================
// Globals
// ====================================================================

// Program file-parameters (ISO §6.10) are registered by Sema as text file
// variables, so they need matching module-level storage or any use of them
// fails to resolve during lowering.
void Codegen::Impl::emitFileParams(const std::vector<std::string>& names) {
    for (const auto& nm : names) {
        auto* ty   = fileStructType();
        auto* zero = llvm::Constant::getNullValue(ty);
        auto* gv   = new llvm::GlobalVariable(*mod, ty, /*isConst=*/false,
                                               llvm::GlobalValue::ExternalLinkage,
                                               zero, PlangGlobalPrefix + nm);
        defVar(nm, gv, ty, /*typeNode=*/nullptr);
    }
}

// Attaches 'input' and 'output' to the standard streams.  Any other
// file-parameter is left closed, so rewrite/reset give it temporary storage
// like an internal file.
void Codegen::Impl::emitFileParamBinds(const std::vector<std::string>& names) {
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
        // initialiser, which needs storage to leave the answer in.
        if (!cv && !currentUnit_.empty()) { emitRuntimeConst(cd); continue; }
        if (!cv) cv = llvm::ConstantInt::get(i64Ty, 0);
        defineConst(cd.Name, cv);
    }
    for (const auto& vg : block.Vars) registerEnumValues(vg.Type.get());
    registerSchemaDefs(block);

    // Global variable declarations.
    for (const auto& vg : block.Vars) {
        llvm::Type* ty = llvmTypeOf(vg.Type.get(), nullptr);
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

    for (const auto& proc : block.Procs) {
        if (proc->IsForward) continue;
        emitFunctionDef(*proc);
    }
}

void Codegen::Impl::emitFunctionDef(const ProcDecl& proc, bool declareOnly) {
    // Save outer context.
    auto* savedFunc       = curFunc;
    auto* savedRetAlloca  = curRetAlloca;
    auto* savedRetType    = curRetType;
    auto  savedFuncName   = curFuncName;
    auto  savedPrefix     = namePrefix;
    auto  savedIP         = builder.saveIP();
    auto* savedStaticLink = curStaticLink;
    auto  savedFuncDepth  = curFuncScopeDepth;
    auto  savedOuterVars  = outerVarNames;
    auto  savedOuterBinds = outerVarBindings;
    // ISO §6.2.2.3: a type or constant declared in this block is invisible
    // outside it.  Both maps are flat, so an inner declaration would otherwise
    // outlive its block and be picked up by whatever the enclosing block
    // declared next.  Copying in means the body still sees enclosing names.
    auto  savedTypeAliases = typeAliases;
    auto  savedConsts      = consts;
    auto  savedRequired    = requiredConsts;
    auto  savedLabels      = std::move(labelBlocks);
    labelBlocks.clear();

    std::string mangledName = namePrefix + proc.Name;
    namePrefix = mangledName + PlangScopeSep;
    curFuncName = proc.Name;

    // ISO §6.6.1: where the parameters and result type were written.  For the
    // defining occurrence of a forward-declared procedure that is the
    // declaration, whose heading this body does not repeat.
    const ProcDecl& hd = proc.heading();

    // Determine whether this is a nested procedure (has an outer function).
    bool isNested = (savedFunc != nullptr);

    // If nested, collect all outer-scope variables that this procedure may access.
    // We collect ALL visible outer variables (conservative; avoids escape analysis).
    std::vector<std::pair<std::string, VarEntry>> outerVars;
    if (isNested) {
        // Walk the scope stack (all scopes except the innermost, which belongs to
        // nested procs not yet emitted) to gather outer variables.
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            for (const auto& [nm, ve] : *it)
                outerVars.push_back({nm, ve});
        }
    }

    // Build LLVM parameter types.
    // For nested procedures, prepend an implicit ptr %static_link first.
    std::vector<llvm::Type*>      paramTypes;
    std::vector<std::string>      paramNames;
    std::vector<bool>             paramIsVar;
    std::vector<llvm::Type*>      paramValTypes;
    std::vector<const TypeNode*>  paramTypeNodes; // AST TypeNode for each param slot

    if (isNested && !outerVars.empty())
        paramTypes.push_back(ptrTy); // static link (frame pointer from outer scope)

    // Conformant-array dimension metadata for this function:
    // one entry per AST param, inner vector is [(loName,hiName)...] per dim.
    std::vector<std::vector<std::pair<std::string,std::string>>> conformantDims;

    // EP §6.4.7: discriminant count per AST param position; 0 = not schematic.
    std::vector<unsigned> schemaDiscCounts;
    // Parallel to schemaDiscCounts: the schema type for each schematic param.
    std::vector<const plang::Type*> schemaTypes;

    // ISO §6.6.3.1: procedural parameter positions, so call sites know to pass
    // the pair rather than a single value.
    std::vector<const ProcedureTypeNode*> procParamFlags;

    // The window each value parameter of set type is based in; 0 elsewhere.
    std::vector<int64_t> setBases;
    std::vector<bool>    paramByRef;

    for (const auto& pg : hd.Params) {
        // ISO §6.6.3.1: a procedural parameter takes two pointers — the entry
        // point and the frame its body reads outer variables through.  Both
        // have to travel together: the frame is built from names visible where
        // the procedure is passed, which is not where it is finally called.
        if (auto* pt = llvm::dyn_cast<ProcedureTypeNode>(pg.Type.get())) {
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

                conformantDims.push_back({});
                schemaDiscCounts.push_back(0);
                schemaTypes.push_back(nullptr);
                procParamFlags.push_back(pt);
                setBases.push_back(0);
                paramByRef.push_back(pg.IsVar);
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

                conformantDims.push_back({});
                schemaDiscCounts.push_back(n);
                schemaTypes.push_back(schTy);
                procParamFlags.push_back(nullptr);
                setBases.push_back(0);
                paramByRef.push_back(pg.IsVar);
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
            {
                const TypeNode* cur = pg.Type.get();
                while (auto* cn = llvm::dyn_cast<ConformantArrayTypeNode>(cur)) {
                    for (const auto& spec : cn->Specs)
                        dims.push_back({spec.Lo, spec.Hi});
                    cur = cn->Element.get();
                }
                // cur now points to the concrete element TypeNode.
                if (cur) elemTy = llvmTypeOfNode(*cur);
            }

            for (const auto& nm : pg.Names) {
                // Array ptr
                paramTypes.push_back(ptrTy);
                paramNames.push_back(nm);
                paramIsVar.push_back(false); // handled specially
                paramValTypes.push_back(elemTy);
                paramTypeNodes.push_back(pg.Type.get());

                // lo and hi for each dimension
                for (const auto& [loNm, hiNm] : dims) {
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
                conformantDims.push_back(dims);
                schemaDiscCounts.push_back(0);
                schemaTypes.push_back(nullptr);
                procParamFlags.push_back(nullptr);
                setBases.push_back(0);
                paramByRef.push_back(pg.IsVar);
            }
        } else {
            llvm::Type* vt = llvmTypeOfNode(*pg.Type);
            const int64_t sb =
                (pg.Type->ResolvedType
                 && pg.Type->ResolvedType->Kind == TypeKind::Set)
                    ? setOffsetOf(*pg.Type->ResolvedType) : 0;
            for (const auto& nm : pg.Names) {
                paramTypes.push_back(pg.IsVar ? ptrTy : vt);
                paramNames.push_back(nm);
                paramIsVar.push_back(pg.IsVar);
                paramValTypes.push_back(vt);
                paramTypeNodes.push_back(pg.Type.get());
                conformantDims.push_back({}); // not conformant
                schemaDiscCounts.push_back(0);
                schemaTypes.push_back(nullptr);
                procParamFlags.push_back(nullptr);
                setBases.push_back(sb);
                paramByRef.push_back(pg.IsVar);
            }
        }
    }

    // Return type.
    llvm::Type* retTy = llvm::Type::getVoidTy(ctx);
    if (hd.IsFunction && hd.ReturnType) {
        retTy = llvmTypeOfNode(*hd.ReturnType);
    }
    curRetType = (retTy->isVoidTy()) ? nullptr : retTy;

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
        funcOuterVarNames_[mangledName] = std::move(names);
    }

    // Parameter metadata a call site needs in order to pass the hidden
    // arguments — conformant bounds, schema discriminants, and the closure
    // pair of a procedural parameter.  Recorded here rather than alongside the
    // body so that the forward pre-pass leaves it behind too: the call being
    // fixed up is one that runs before the body is emitted.
    conformantParamDims_[mangledName] = conformantDims;
    schemaParamDiscs_[mangledName]    = schemaDiscCounts;
    procParamPositions_[mangledName]  = procParamFlags;
    paramSetBases_[mangledName]       = setBases;
    paramByRef_[mangledName]          = paramByRef;

    // A forward declaration contributes its signature and stops; the body
    // arrives with the defining occurrence.
    if (declareOnly) {
        curFunc       = savedFunc;
        curRetAlloca  = savedRetAlloca;
        curRetType    = savedRetType;
        curFuncName   = savedFuncName;
        namePrefix    = savedPrefix;
        curStaticLink = savedStaticLink;
        outerVarNames = savedOuterVars;
        outerVarBindings = savedOuterBinds;
        labelBlocks   = std::move(savedLabels);
        builder.restoreIP(savedIP);
        return;
    }

    // Name the static-link parameter (first arg of nested procs).
    auto funcArgIt = func->arg_begin();
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

    // If nested, expose outer variables via the static link.
    // The frame struct is { ptr, ptr, ... } — one ptr per outer variable,
    // each pointing to the outer alloca so reads and writes go through.
    curStaticLink = staticLinkArg;
    outerVarNames.clear();
    outerVarBindings.clear();
    if (staticLinkArg && !outerVars.empty()) {
        // Frame is { ptr, ptr, ... } — one slot per outer var.
        std::vector<llvm::Type*> ptrFields(outerVars.size(), ptrTy);
        auto* frameTy = llvm::StructType::get(ctx, ptrFields);

        auto* zero = llvm::ConstantInt::get(i32Ty, 0);
        for (size_t fi = 0; fi < outerVars.size(); ++fi) {
            const auto& [nm, ve] = outerVars[fi];
            auto* fidx    = llvm::ConstantInt::get(i32Ty, (unsigned)fi);
            // Load the pointer to the outer alloca from the frame slot.
            auto* ptrSlot = builder.CreateGEP(frameTy, staticLinkArg,
                                              {zero, fidx}, "frame.slot." + nm);
            auto* outerPtr = builder.CreateLoad(ptrTy, ptrSlot, "outer." + nm);
            // Register it as the variable's alloca so reads/writes go through.
            defVar(nm, outerPtr, ve.type, ve.typeNode);
            // ISO §6.6.3.1: an outer procedural parameter is still one here.
            // Only the address travels, which is why the pair was spilled to a
            // cell; what it means has to be carried across separately.
            if (ve.isProcParam) {
                auto& nve       = scopes.back()[toLower(nm)];
                nve.isProcParam = true;
                nve.procType    = ve.procType;
            }
            outerVarNames.push_back(nm);
            // Kept apart from the scope, which a local of the same
            // name will overwrite.
            outerVarBindings[toLower(nm)] = scopes.back()[toLower(nm)];
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

    // Copy Pascal-declared parameters to allocas.
    // conformantDims has one entry per Pascal param name; each LLVM arg sequence
    // consumed depends on whether the param is conformant or not.
    // flatIdx: current offset into paramNames/paramValTypes/paramIsVar.
    {
        auto it = funcArgIt; // LLVM arg iterator (after static link)
        size_t flatIdx = 0;
        for (size_t ci = 0; ci < conformantDims.size() && it != func->arg_end(); ++ci) {
            const std::string& nm  = paramNames[flatIdx];
            const auto&        dims = conformantDims[ci];

            // ISO §6.6.3.1: procedural param — the entry point and its frame.
            if (ci < procParamFlags.size() && procParamFlags[ci]) {
                llvm::Value* fnPtr = &*it; ++it;
                llvm::Value* frame = &*it; ++it;
                // Spilled to a cell so the pair has an address: a nested
                // procedure reaches an outer variable through a static link,
                // which carries addresses and nothing else.
                auto* cell = createEntryAlloca(procPairTy(), nm + ".closure");
                storeProcPair(cell, fnPtr, frame);
                defVar(nm, cell, procPairTy());
                auto& ve       = scopes.back()[toLower(nm)];
                ve.isProcParam = true;
                ve.procType    = procParamFlags[ci];
                flatIdx += 2;
                continue;
            }

            // EP §6.4.7: schema param — consume the body pointer, then one i64
            // per discriminant.  Both value and var parameters take the address
            // because the body's size is not known to the caller's ABI.
            if (schemaDiscCounts[ci] > 0) {
                llvm::Value* bodyPtr = &*it; ++it;
                std::vector<llvm::Value*> discs;
                for (unsigned d = 0; d < schemaDiscCounts[ci]; ++d) {
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
                defVar(nm, bodyPtr, paramValTypes[flatIdx]);
                auto& ve       = scopes.back()[toLower(nm)];
                ve.schemaTy    = schemaTypes[ci];
                ve.schemaDiscs = std::move(discs);
                flatIdx += 1 + schemaDiscCounts[ci];
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

                    auto* loA = createEntryAlloca(i64Ty, loNm + ".addr");
                    builder.CreateStore(loArg, loA);
                    defVar(loNm, loA, i64Ty);

                    auto* hiA = createEntryAlloca(i64Ty, hiNm + ".addr");
                    builder.CreateStore(hiArg, hiA);
                    defVar(hiNm, hiA, i64Ty);

                    dimPtrs.emplace_back(loA, hiA);
                }

                // Register the array itself as a conformant VarEntry.
                // We use the raw incoming ptr (no extra alloca needed) because
                // the actual data already lives in the caller's alloca.
                defVar(nm, arrPtrArg, elemTy);
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
                    defVar(nm, &*it, vt, tn);
                } else {
                    auto* a = createEntryAlloca(vt, nm + ".addr");
                    builder.CreateStore(&*it, a);
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

    // Emit return if the last block is not yet terminated.
    if (!isTerminated()) {
        if (curRetType && curRetAlloca) {
            auto* rv = builder.CreateLoad(curRetType, curRetAlloca, "retval");
            builder.CreateRet(rv);
        } else {
            builder.CreateRetVoid();
        }
    }

    popScope();

    // Restore outer context.
    curFunc       = savedFunc;
    curRetAlloca  = savedRetAlloca;
    curRetType    = savedRetType;
    curFuncName   = savedFuncName;
    namePrefix    = savedPrefix;
    curStaticLink = savedStaticLink;
    curFuncScopeDepth = savedFuncDepth;
    outerVarNames = savedOuterVars;
    outerVarBindings = savedOuterBinds;
    typeAliases   = std::move(savedTypeAliases);
    consts        = std::move(savedConsts);
    requiredConsts = std::move(savedRequired);
    labelBlocks   = std::move(savedLabels);
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
        llvm::Value* cv = constantValueOf(cd);
        if (!cv) cv = emitExpr(*cd.Value); // runtime fallback (EP only)
        if (!cv) cv = llvm::ConstantInt::get(i64Ty, 0);
        defineConst(cd.Name, cv);
    }
}

void Codegen::Impl::emitBlockAllocas(const BlockNode& block) {
    // Before the variables, not after: `type t = integer; var v: t` has to see
    // this block's t rather than an enclosing block's.
    emitBlockDecls(block);
    for (const auto& vg : block.Vars) {
        llvm::Type* ty = llvmTypeOf(vg.Type.get(), nullptr);
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
    if (auto* sn = llvm::dyn_cast<StringTypeNode>(tn))
        return evalConstInt(*sn->Capacity, 255, &consts);
    // Written through a name, so the syntax says nothing; Sema resolved it.
    if (tn->ResolvedType && tn->ResolvedType->Kind == TypeKind::VarString)
        return tn->ResolvedType->StrCapacity;
    return 0;
}

// EP §6.6: the value clause of a variable declaration, or of the denoter the
// variable is declared with.
void Codegen::Impl::emitVarValueInit(const VarGroup& vg) {
    if (!vg.InitExpr) {
        // The declaration says nothing, but the type may: `var x: t` begins in
        // whatever state t was declared to begin in.
        if (!hasInitialState(vg.Type.get())) return;
        for (const auto& nm : vg.Names)
            if (const auto* ve = findVar(nm))
                emitInitialState(ve->ptr, ve->type, vg.Type.get());
        return;
    }

    for (const auto& nm : vg.Names)
        if (const auto* ve = findVar(nm))
            storeInitialValue(ve->ptr, ve->type, vg.Type.get(), *vg.InitExpr);
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
    if (auto* sv = llvm::dyn_cast<StructuredValueExpr>(&value);
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
const ExprNode* Codegen::Impl::writtenInitialState(const TypeNode* tn,
                                                   const TypeNode** carrier) const {
    for (int hops = 0; tn && hops < 32; ++hops) {
        if (tn->InitialState) {
            if (carrier) *carrier = tn;
            return tn->InitialState.get();
        }
        auto* named = llvm::dyn_cast<NamedTypeNode>(tn);
        if (!named) break;
        auto it = typeAliases.find(toLower(named->Name));
        if (it == typeAliases.end() || it->second == tn) break;
        tn = it->second;
    }
    return nullptr;
}

// EP §6.6: a structured type begins in the state its components begin in, so
// a record with one initialized field has an initial state of its own even
// though nothing was written beside the record.
bool Codegen::Impl::hasInitialState(const TypeNode* tn, int depth) const {
    if (!tn || depth > 16) return false;
    if (writtenInitialState(tn)) return true;
    const TypeNode* shape = denoterOf(tn);
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
                                     const TypeNode* tn, int depth) {
    if (!ptr || !tn || depth > 16) return;

    const TypeNode* carrier = nullptr;
    if (const ExprNode* init = writtenInitialState(tn, &carrier)) {
        storeInitialValue(ptr, ty, carrier, *init);
        return;
    }

    const TypeNode* shape = denoterOf(tn);
    if (auto* rtn = llvm::dyn_cast_or_null<RecordTypeNode>(shape)) {
        const auto& L  = layoutOf(*rtn);
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
    auto* savedFunc      = curFunc;
    auto* savedRetAlloca = curRetAlloca;
    auto* savedRetType   = curRetType;
    auto  savedFuncName  = curFuncName;
    auto  savedPrefix    = namePrefix;
    auto  savedIP        = builder.saveIP();

    curFuncName  = "main";
    namePrefix   = PlangProcPrefix;
    curRetType   = nullptr;
    curRetAlloca = nullptr;

    auto* funcTy = llvm::FunctionType::get(i32Ty, {}, false);
    auto* func   = llvm::Function::Create(funcTy, llvm::Function::ExternalLinkage,
                                           "main", mod.get());
    curFunc = func;

    auto* entry = llvm::BasicBlock::Create(ctx, "entry", func);
    builder.SetInsertPoint(entry);

    pushScope();
    // Program-level variables were already given storage as module globals by
    // emitGlobals(), and procedures resolve them to those globals.  Allocating
    // them again here would shadow the globals inside main only, so writes made
    // by a procedure would not be visible to the program body and vice versa.
    emitBlockDecls(block);
    emitFileParamBinds(fileParams);
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

    // EP §6.11.2: the finalisers run in the reverse of the order the
    // initialisers did.  A module is initialized after the ones it imports, so
    // running the finalisers forward would tear down a module while one that
    // depends on it is still to be finalized.  Each initialiser registered its
    // own finaliser as it completed, which is the order the runtime unwinds.
    if (!isTerminated())
        builder.CreateCall(getExternFnN("plang_module_finals_run",
                                         llvm::Type::getVoidTy(ctx), {}), {});

    if (!isTerminated())
        builder.CreateRet(llvm::ConstantInt::get(i32Ty, 0));

    popScope();

    curFunc      = savedFunc;
    curRetAlloca = savedRetAlloca;
    curRetType   = savedRetType;
    curFuncName  = savedFuncName;
    namePrefix   = savedPrefix;
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

    auto* savedFunc      = curFunc;
    auto* savedRetAlloca = curRetAlloca;
    auto* savedRetType   = curRetType;
    auto  savedFuncName  = curFuncName;
    auto  savedIP        = builder.saveIP();

    curFuncName  = fnName;
    curRetType   = nullptr;
    curRetAlloca = nullptr;

    // The declaration may already stand here, made by another module's
    // initialiser that imports this one.
    auto* func = moduleInitFn(modNode.Name);
    curFunc = func;

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
    for (const auto& clause : modNode.Imports)
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

    curFunc      = savedFunc;
    curRetAlloca = savedRetAlloca;
    curRetType   = savedRetType;
    curFuncName  = savedFuncName;
    builder.restoreIP(savedIP);
    return fnName;
}

void Codegen::Impl::emitModuleLifecycleFn(const std::string& fnName,
                                           const StmtNode& stmt) {
    // Save outer context.
    auto* savedFunc      = curFunc;
    auto* savedRetAlloca = curRetAlloca;
    auto* savedRetType   = curRetType;
    auto  savedFuncName  = curFuncName;
    auto  savedIP        = builder.saveIP();

    // namePrefix is left as the caller set it: the block belongs to the module,
    // and the procedures it calls are mangled with that module's name.
    curFuncName  = fnName;
    curRetType   = nullptr;
    curRetAlloca = nullptr;

    auto* voidTy = llvm::Type::getVoidTy(ctx);
    auto* funcTy = llvm::FunctionType::get(voidTy, {}, false);
    auto* func   = llvm::Function::Create(funcTy, llvm::Function::InternalLinkage,
                                           fnName, mod.get());
    curFunc = func;

    auto* entry = llvm::BasicBlock::Create(ctx, "entry", func);
    builder.SetInsertPoint(entry);

    pushScope();
    emitStmt(&stmt);
    popScope();

    if (!isTerminated())
        builder.CreateRetVoid();

    curFunc      = savedFunc;
    curRetAlloca = savedRetAlloca;
    curRetType   = savedRetType;
    curFuncName  = savedFuncName;
    builder.restoreIP(savedIP);
}
