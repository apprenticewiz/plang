#include "CodeGenImpl.h"
#include "plang/Basic/SemaUtil.h"
using namespace plang;

// See NumStmtKinds in AstBase.h.
static_assert(NumStmtKinds == 12, "a new statement needs a case in emitStmt");

// ====================================================================
// Statement emission
// ====================================================================

void Codegen::Impl::emitStmt(const StmtNode* stmt) {
    if (!stmt || isTerminated()) return;

    // -g: one hook for every statement kind, not one per kind -- IRBuilder's
    // own Insert() attaches whatever SetCurrentDebugLocation last set to
    // every instruction it creates from here on, automatically, so this is
    // the only place a location needs to be set at all.
    dbgInfo_->setLocation(stmt->Loc);

    // The two statement kinds that evaluate an arbitrary expression and then
    // finish are the two that can leave a run-time-sized string temporary
    // behind, and giving the stack back here is what keeps one inside a loop
    // costing a fixed amount rather than one allocation per iteration.
    //
    // Deliberately not around every statement: a labeled statement moves the
    // insertion point to a block that any goto may enter, so the save would not
    // dominate the restore and the IR would not verify.  A structured statement
    // is covered by the scopes of the simple statements inside it.
    if (auto* s = llvm::dyn_cast<AssignStmt>(stmt)) {
        StackScope frame(*this); emitAssign(*s);   return; }
    if (auto* s = llvm::dyn_cast<CompoundStmt>(stmt))  { emitCompound(*s); return; }
    if (auto* s = llvm::dyn_cast<IfStmt>(stmt))        { emitIf(*s);       return; }
    if (auto* s = llvm::dyn_cast<WhileStmt>(stmt))     { emitWhile(*s);    return; }
    if (auto* s = llvm::dyn_cast<ForStmt>(stmt))       { emitFor(*s);      return; }
    if (auto* s = llvm::dyn_cast<ForInStmt>(stmt))    { emitForIn(*s);    return; }
    if (auto* s = llvm::dyn_cast<RepeatStmt>(stmt))    { emitRepeat(*s);   return; }
    if (auto* s = llvm::dyn_cast<CallStmt>(stmt)) {
        StackScope frame(*this); emitCallStmt(*s); return; }
    if (auto* s = llvm::dyn_cast<GotoStmt>(stmt)) { emitGoto(*s); return; }
    if (auto* s = llvm::dyn_cast<LabeledStmt>(stmt)) {
        auto* lblBB = getOrCreateLabel("lbl_" + s->Label);
        brIfNeeded(lblBB);
        builder.SetInsertPoint(lblBB);
        emitStmt(s->Stmt.get());
        return;
    }
    if (auto* s = llvm::dyn_cast<CaseStmt>(stmt)) { emitCase(*s); return; }
    if (auto* s = llvm::dyn_cast<WithStmt>(stmt)) { emitWith(*s); return; }
    // Falling off the end would drop the statement from the program silently.
    codegenICE("unhandled statement kind in codegen");
}

bool Codegen::Impl::declaresLabel(const BlockNode& block, const std::string& label) {
    return LabelGotoEngine::declaresLabel(block, label);
}

std::set<std::string> Codegen::Impl::nonLocalTargets(const BlockNode& block) {
    return LabelGotoEngine::nonLocalTargets(block);
}

int64_t Codegen::Impl::gotoDispatchValue(const std::string& label) {
    return LabelGotoEngine::gotoDispatchValue(label);
}

llvm::BasicBlock* Codegen::Impl::getOrCreateLabel(const std::string& name) {
    return gotoEngine_->getOrCreateLabel(name);
}

void Codegen::Impl::openLabelScope(const BlockNode& block, bool programBlock) {
    gotoEngine_->openLabelScope(block, programBlock);
}

void Codegen::Impl::emitLabelLanding() {
    gotoEngine_->emitLabelLanding();
}

void Codegen::Impl::closeLabelScope() {
    gotoEngine_->closeLabelScope();
}

void Codegen::Impl::pinLocalsToMemory(llvm::Function* f) {
    LabelGotoEngine::pinLocalsToMemory(f);
}

void Codegen::Impl::emitGoto(const GotoStmt& s) {
    gotoEngine_->emitGoto(s);
}

void Codegen::Impl::emitCompound(const CompoundStmt& s) {
    for (const auto& st : s.Stmts) {
        if (isTerminated()) resumeAfterTerminator();
        emitStmt(st.get());
    }
}

/// Open a fresh block to continue emitting into after a goto or a return has
/// closed the current one.  Stopping instead would be wrong: a later statement
/// can carry a label, and skipping it leaves the block that a forward goto
/// already branched to empty and unterminated.  A block nothing reaches costs
/// nothing — LLVM discards it.
void Codegen::Impl::resumeAfterTerminator() {
    builder.SetInsertPoint(
        llvm::BasicBlock::Create(ctx, "unreachable", curFunc));
}

void Codegen::Impl::emitCallStmt(const CallStmt& s) {
    std::string lo = toLower(s.Name);

    // ISO §6.6.3.1: a procedural parameter is called through the pair it
    // arrived as.  Checked before the required procedures, so a parameter
    // named `page` or `get` is still the parameter.
    if (auto* pve = findVar(s.Name); pve && pve->isProcParam) {
        (void)emitProcParamCall(*pve, s.Args);
        return;
    }

    // ISO §6.2.2.10: a required procedure identifier may be redeclared, and
    // then it denotes what the program declared and not the required one.  The
    // chain below dispatches on spelling alone, so without this a program that
    // declares its own `close` reaches a required procedure that takes
    // different arguments — which it then emitted a call to with none of them.
    // Sema resolved the name in the scope it was written in and is the only
    // thing that knows which won.
    if (s.ResolvedBuiltin == BuiltinID::None) {
        emitUserProcCall(s);
        return;
    }

    if (lo == "write" || lo == "writeln") {
        emitBuiltinWrite(s.Args, lo == "writeln");
        return;
    }
    if (lo == "read") {
        emitBuiltinRead(s.Args);
        return;
    }
    if (lo == "readln") {
        emitBuiltinReadln(s.Args);
        return;
    }
    // EP §6.7.5.5: both require a destination/source plus at least one value.
    if (lo == "writestr" && s.Args.size() >= 2) {
        emitBuiltinWriteStr(s.Args);
        return;
    }
    if (lo == "readstr" && s.Args.size() >= 2) {
        emitBuiltinReadStr(s.Args);
        return;
    }
    if (lo == "page") {
        if (!s.Args.empty() && isFileVar(*s.Args[0])) {
            auto* fp = fileVarPtr(*s.Args[0]);
            builder.CreateCall(getExternFnN("plang_page_file",
                llvm::Type::getVoidTy(ctx), {ptrTy}), {fp});
        } else {
            builder.CreateCall(getRuntimeFn("plang_page", nullptr), {});
        }
        return;
    }
    if ((lo == "reset" || lo == "rewrite") && !s.Args.empty()) {
        auto* fp = fileVarPtr(*s.Args[0]);
        auto* nm = s.Args.size() > 1
            ? emitExpr(*s.Args[1])
            : llvm::ConstantPointerNull::get(ptrTy);
        // §6.4.3.5 makes a text file a sequence of lines, each ended by a line
        // marker.  Turning one round to read it has to finish the line the
        // writing left open, and whether there is a line to finish is a
        // question only about a text file.
        auto* fn = getExternFnN("plang_" + lo,
            llvm::Type::getVoidTy(ctx), {ptrTy, ptrTy, i8Ty});
        builder.CreateCall(fn, {fp, nm,
            llvm::ConstantInt::get(i8Ty,
                isTypedBinaryFileVar(*s.Args[0]) ? 0 : 1)});
        return;
    }
    if ((lo == "get" || lo == "put") && !s.Args.empty()) {
        // ISO §6.5.5: both move one component, so both need its width.
        auto* fp  = fileVarPtr(*s.Args[0]);
        auto* esz = llvm::ConstantInt::get(i64Ty, getFileElemSize(*s.Args[0]));
        auto* fn  = getExternFnN("plang_" + lo + "_file",
            llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty});
        builder.CreateCall(fn, {fp, esz});
        return;
    }
    if (lo == "close" && !s.Args.empty()) {
        auto* fp = fileVarPtr(*s.Args[0]);
        auto* fn = getExternFnN("plang_close",
            llvm::Type::getVoidTy(ctx), {ptrTy});
        builder.CreateCall(fn, {fp});
        return;
    }
    // EP §6.7.5.2: extend / update
    if ((lo == "extend" || lo == "update") && !s.Args.empty()) {
        auto* fp = fileVarPtr(*s.Args[0]);
        auto* nm = s.Args.size() > 1
            ? emitExpr(*s.Args[1])
            : llvm::ConstantPointerNull::get(ptrTy);
        auto* fn = getExternFnN("plang_" + lo,
            llvm::Type::getVoidTy(ctx), {ptrTy, ptrTy});
        builder.CreateCall(fn, {fp, nm});
        return;
    }
    // EP §6.7.5.2: SeekRead / SeekWrite / SeekUpdate.  n is a value of the
    // file's declared INDEX TYPE (ISO §6.7.5.2's own pre-assertion measures
    // "ord(n)-ord(a)"), not a byte offset and not a 0-based component count
    // -- so `file[5..10] of integer; SeekWrite(f, 5)` must land on the FIRST
    // component, not five components in.
    if ((lo == "seekread" || lo == "seekwrite" || lo == "seekupdate")
        && s.Args.size() >= 2) {
        auto* fp      = fileVarPtr(*s.Args[0]);
        auto* idx     = toI64(emitExpr(*s.Args[1]));
        int64_t esz   = getFileElemSize(*s.Args[0]);
        int64_t ilo   = getFileIndexLow(*s.Args[0]);
        auto* fn = getExternFnN("plang_" + lo,
            llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, i64Ty, i64Ty});
        builder.CreateCall(fn, {fp, idx, llvm::ConstantInt::get(i64Ty, esz),
                                llvm::ConstantInt::get(i64Ty, ilo)});
        return;
    }
    // EP §6.7.5.6: bind(f, b) / unbind(f) — associate/dissociate file binding
    if (lo == "bind" && s.Args.size() >= 2) {
        auto* fp = fileVarPtr(*s.Args[0]);
        auto* bp = emitLValue(*s.Args[1]);
        auto* fn = getExternFnN("plang_bind",
                                llvm::Type::getVoidTy(ctx), {ptrTy, ptrTy});
        builder.CreateCall(fn, {fp, bp});
        return;
    }
    if (lo == "unbind" && !s.Args.empty()) {
        auto* fp = fileVarPtr(*s.Args[0]);
        auto* fn = getExternFnN("plang_unbind",
                                llvm::Type::getVoidTy(ctx), {ptrTy});
        builder.CreateCall(fn, {fp});
        return;
    }

    // EP §6.7.5.8: GetTimeStamp(t) — fill t with current date/time
    if (lo == "gettimestamp" && !s.Args.empty()) {
        auto* tPtr = emitLValue(*s.Args[0]);
        auto* fn = getExternFnN("plang_gettimestamp",
                                llvm::Type::getVoidTy(ctx), {ptrTy});
        builder.CreateCall(fn, {tPtr});
        return;
    }

    // ISO §6.7.5.4 transfer procedures.
    if ((lo == "pack" || lo == "unpack") && s.Args.size() == 3) {
        emitPackUnpack(s, /*isPack=*/lo == "pack");
        return;
    }

    if (lo == "halt" || lo == "exit") {
        // EP §6.7.5.7 halt takes no argument; halt(n) is the widespread extension
        // that sets the exit status.
        auto* status = s.Args.empty() ? llvm::ConstantInt::get(i64Ty, 0)
                                      : toI64(emitExpr(*s.Args[0]));
        builder.CreateCall(getRuntimeHaltFn(), {status});
        builder.CreateUnreachable();
        return;
    }
    if (lo == "new" && !s.Args.empty()) {
        // EP §6.7.5.3: new(p, d1..ds) when p's domain-type is a schema-name.
        if (const auto& pt = s.Args[0]->ResolvedType;
                pt && pt->Kind == TypeKind::Pointer && pt->PointeeType
                && pt->PointeeType->Kind == TypeKind::Schema) {
            emitNewSchema(*s.Args[0], *pt->PointeeType,
                          std::span(s.Args).subspan(1));
            return;
        }
        auto* addr = emitLValue(*s.Args[0]);
        // How much to allocate is a question about the pointer's type, not
        // about how the declaration was written.  A pointer reached through a
        // type name has no PointerTypeNode to read, and the old fallback of
        // one pointer's worth silently under-allocated for anything larger.
        // R1, reopened by review 5.  Sema's answer was already here -- as the
        // FALLBACK, reached only when the denoter route returned 0.  The
        // denoter route walks `typeAliases` by SPELLING at the use site, so a
        // pointer declared `var g: pt` in a program where a procedure declares
        // its own `pt` allocated the INNER pt's domain: 16 bytes for a
        // ten-element array, and glibc aborted on the corrupted heap.  Plain
        // ISO 7185, and the shape the 0.1.5/0.1.6 corruptions had.
        //
        // The R1 rule went into llvmTypeOfNode's NamedTypeNode branch, which is
        // why this survived it: the name is re-bound HERE, before any TypeNode
        // is lowered, so llvmTypeOfNode is handed the inner declaration's base
        // and answers correctly for the wrong type.  A site that resolves a
        // name before reaching the rule is not covered by the rule.
        int64_t            Bytes   = 0;
        const TypeNode*    domain  = nullptr;
        const plang::Type* pointee = nullptr;
        if (const auto& pt = s.Args[0]->ResolvedType;
                pt && pt->Kind == TypeKind::Pointer && pt->PointeeType)
            pointee = pt->PointeeType.get();
        if (pointee)
            Bytes = (int64_t)mod->getDataLayout().getTypeAllocSize(
                llvmTypeOfSemaType(*pointee));

        // The domain DENOTER is still wanted, for the initial state below --
        // Sema's Type records a RecordDecl and nothing more general, so a
        // `value` clause on a non-record domain is only reachable through the
        // node.  It is accepted only when it agrees with the size Sema gave:
        // the same spelling walk that mis-sized the allocation also picked the
        // wrong type's `value` clause, memcpying 400 bytes of one type's
        // initial value into another's 4-byte allocation.  A disagreement means
        // the denoter was re-resolved somewhere else, so it is not this
        // variable's domain and its initial state is not this variable's.
        //
        // The size-agreement check is not enough on its own: `ve->typeNode` is
        // `g`'s OWN declaration, written wherever `g` was declared -- module
        // scope, say -- and not in the procedure calling `new(g)`.  denoterOf
        // walked `typeAliases` for that FOREIGN node's name, so a procedure
        // that merely shadows the pointer's own type name with an unrelated,
        // SAME-SIZE one slipped straight through the check: `new(g)` inside a
        // procedure with its own local `type pt = ^inner_dom` (one field,
        // like the real domain) applied inner_dom's `value` clause to g's
        // real, unrelated allocation.  initialStateShapeOf is the fix already
        // used for exactly this pattern elsewhere: it follows
        // NamedTypeNode::Denotes, which Sema recorded in the scope `pt` was
        // actually written in.
        if (auto* id = llvm::dyn_cast<IdentExpr>(s.Args[0].get()))
            if (auto* ve = findVar(id->Name))
                if (auto* ptn = llvm::dyn_cast_or_null<PointerTypeNode>(
                        initialStateShapeOf(ve->typeNode))) {
                    const TypeNode* d = ptn->Base.get();
                    const auto dsz = (int64_t)mod->getDataLayout()
                        .getTypeAllocSize(llvmTypeOfNode(*d));
                    if (Bytes == 0 || dsz == Bytes) {
                        domain = d;
                        if (Bytes == 0) Bytes = dsz;
                    }
                }
        // The domain type, for the initial state below.  Only the identifier
        // route set it, so `new(h.p)` and `new(a[1])` applied no initial state
        // at all: the size already fell back to Sema's type and this did not.
        // A record is what carries field `value` clauses, and Sema's type
        // knows the declaration it came from.
        if (!domain && pointee) domain = pointee->RecordDecl;
        if (Bytes == 0)
            codegenICE("new() cannot determine the size of what '"
                       + std::string(s.Args[0]->ResolvedType
                                     ? s.Args[0]->ResolvedType->Name : "?")
                       + "' points to");
        auto* ptr = builder.CreateCall(getRuntimeNewFn(),
                                       {llvm::ConstantInt::get(i64Ty, Bytes)});
        builder.CreateStore(ptr, addr);
        // EP §6.6: the variable new creates begins in whatever state its type
        // says a variable of it begins in, as a declared one would.
        if (domain && hasInitialState(domain))
            emitInitialState(ptr, llvmTypeOfNode(*domain), domain);
        return;
    }
    if (lo == "dispose" && !s.Args.empty()) {
        auto* val = emitExpr(*s.Args[0]);
        builder.CreateCall(getRuntimeDisposeFn(), {val});
        return;
    }

    emitUserProcCall(s);
}

void Codegen::Impl::emitUserProcCall(const CallStmt& s) {
    // User-defined procedure — walk the nesting hierarchy to find the right
    // LLVM mangled name (plang_outer__inner, not just plang_inner).
    std::string mangledName = findMangledProc(s.Name);
    auto* callee = mod->getFunction(mangledName);
    if (!callee) {
        // The procedure is not defined in this compilation unit; it must come
        // from a separately compiled module.  Create an external declaration
        // using LLVM types derived from the Sema-resolved argument types.
        std::vector<llvm::Type*> paramTys;
        for (const auto& Arg : s.Args) {
            if (Arg && Arg->ResolvedType && !Arg->ResolvedType->isError())
                paramTys.push_back(llvmTypeOfSemaType(*Arg->ResolvedType));
            else
                paramTys.push_back(i64Ty); // safe fallback
        }
        auto* fnTy = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), paramTys, false);
        callee = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage,
                                        mangledName, mod.get());
    }

    std::vector<llvm::Value*> args;

    // If the callee is a nested procedure, build its static-link frame from
    // funcOuterVarNames_[callee] (recorded at definition) so the slot ordering
    // matches exactly, resolving each name through findVar() in the *current*
    // scope.  findVar() returns the local alloca for immediate outer vars and
    // the GEP-loaded pointer for deeper ones — composing correctly through any
    // number of nesting levels.
    if (auto* frame = buildStaticLinkFrame(mangledName)) args.push_back(frame);

    // EP §6.7.3.7: look up conformant param dimensions for this callee.
    // pMeta[i].conformantDims is the dimension list for the i-th AST
    // argument position.  An empty list means the param is not conformant
    // (emit normally).
    const std::vector<ParamMeta>* pMeta = nullptr;
    {
        auto cit = paramMeta_.find(mangledName);
        if (cit != paramMeta_.end())
            pMeta = &cit->second;
    }

    size_t pi = args.size(); // LLVM arg index (after static link)
    for (size_t astArgIdx = 0; astArgIdx < s.Args.size(); ++astArgIdx) {
        const auto& arg = s.Args[astArgIdx];

        // ISO §6.6.3.1: procedural param — entry point plus its frame.
        if (const auto* pt = procParamArg(mangledName, astArgIdx)) {
            pushProcParamArgs(args, *arg, *pt);
            pi = args.size();
            continue;
        }

        // Check if this AST arg position is conformant.
        // EP §6.4.7: schema param — body pointer plus its discriminants.
        if (unsigned nd = schemaArgDiscs(mangledName, astArgIdx); nd > 0) {
            pushSchemaArgs(args, *arg, nd);
            pi = args.size();
            continue;
        }

        bool isConformant = pMeta && astArgIdx < pMeta->size()
                            && !(*pMeta)[astArgIdx].conformantDims.empty();

        if (isConformant) {
            const size_t dims = (*pMeta)[astArgIdx].conformantDims.size();
            pushConformantArgs(args, *arg, dims);
            pi += 1 + 2 * dims;
        } else {
            // Regular param (var or value).
            std::optional<int64_t> destSetBase;
            if (pMeta && astArgIdx < pMeta->size())
                destSetBase = (*pMeta)[astArgIdx].setBase;
            args.push_back(setOps_->alignSetArg(
                emitCallArg(*arg,
                    pi < callee->arg_size()
                        ? callee->getFunctionType()->getParamType(pi) : nullptr,
                    paramIsByRef(mangledName, astArgIdx)),
                *arg, destSetBase));
            ++pi;
        }
    }
    builder.CreateCall(callee, args);
}

// ISO §6.7.5.4: pack(a, i, z) copies a[i], a[i+1], ... into the whole of z;
// unpack(z, a, i) copies the whole of z back into a starting at index i.
// Packed arrays share their unpacked element layout here, so the transfer is a
// contiguous copy rather than a bit-level repack.
void Codegen::Impl::emitPackUnpack(const CallStmt& s, bool isPack) {
    const ExprNode& aExpr = isPack ? *s.Args[0] : *s.Args[1];
    const ExprNode& zExpr = isPack ? *s.Args[2] : *s.Args[0];
    const ExprNode& iExpr = isPack ? *s.Args[1] : *s.Args[2];

    const std::string what = isPack ? "pack" : "unpack";
    const auto& zTy = zExpr.ResolvedType;
    if (!zTy || zTy->Kind != TypeKind::Array || !zTy->IndexType)
        codegenICE(what + " has no packed array to transfer");

    const int64_t count = zTy->IndexType->SubHi - zTy->IndexType->SubLo + 1;
    if (count <= 0) return;

    // Where the unpacked array starts and ends.  A conformant array parameter
    // is an array like any other to ISO §6.6.5.4, but its bounds arrived with
    // it as hidden arguments and are values in this activation rather than
    // numbers in its type, so both ends are Values and the check on the
    // starting index is made at run time.
    llvm::Value* aPtr   = nullptr;
    llvm::Type*  elemTy = nullptr;
    llvm::Value* aLo    = nullptr;
    llvm::Value* aHi    = nullptr;

    const VarEntry* conf = nullptr;
    if (auto* id = llvm::dyn_cast<IdentExpr>(&aExpr))
        if (const auto* ve = findVar(id->Name); ve && ve->isConformantArray)
            conf = ve;

    if (conf) {
        auto boundOf = [&](const std::string& nm) -> llvm::Value* {
            const auto* bv = findVar(nm);
            return bv ? builder.CreateLoad(i64Ty, bv->ptr, "conf.bound") : nullptr;
        };
        aPtr   = conf->ptr;
        elemTy = conf->conformantElemTy;
        aLo    = boundOf(conf->conformantLoName);
        aHi    = boundOf(conf->conformantHiName);
        if (!aPtr || !elemTy || !aLo || !aHi)
            codegenICE(what + " has a conformant array whose bounds did not "
                              "arrive with it");
    } else if (auto path = schemaPathOf(aExpr);
               path && llvm::isa_and_nonnull<ArrayTypeNode>(
                           peelPackedNode(path->decl))) {
        // EP §6.4.7: the bounds of a schema array are not in its type.  Sema
        // holds the PROBE's, so reading them from the type checked
        // `pack(q^.a, 3, z)` against "1..-2" -- one minus the width of z, off a
        // probe upper bound of 1 -- and refused a legal program with a bound
        // that describes nothing.  Re-emitted here against the discriminants
        // the object carries, like every other extent in a schema body.
        auto* at = llvm::cast<ArrayTypeNode>(peelPackedNode(path->decl));
        RtDiscScope disc(*this, path->root.discs);
        auto  bounds = rtIndexBounds(*at);
        auto* elemSz = rtSizeOfTypeNode(at->Element.get());
        if (!bounds)
            codegenICE(what + " has a schema array whose bounds cannot be "
                              "evaluated at run time");
        // The transfer strides by a constant element size, so ask the layout
        // walk whether this element has one -- rather than asking the node's
        // annotation, which belongs to whichever instantiation was resolved
        // last and is not this walk's to trust.  Loud rather than wrong.
        if (!llvm::isa_and_nonnull<llvm::ConstantInt>(elemSz))
            codegenICE(what + " on an array whose element size a discriminant "
                              "fixes");
        aPtr   = path->addr;
        elemTy = llvmTypeOfNode(*at->Element);
        aLo    = bounds->first;
        aHi    = bounds->second;
    } else {
        const auto& aTy = aExpr.ResolvedType;
        if (!aTy || aTy->Kind != TypeKind::Array || !aTy->IndexType
                || !aTy->ElemType)
            codegenICE(what + " has no unpacked array to transfer");
        elemTy = llvmTypeOfSemaType(*aTy->ElemType);
        aPtr   = emitLValue(aExpr);
        aLo = llvm::ConstantInt::get(i64Ty,
                  static_cast<uint64_t>(aTy->IndexType->SubLo), /*isSigned=*/true);
        aHi = llvm::ConstantInt::get(i64Ty,
                  static_cast<uint64_t>(aTy->IndexType->SubHi), /*isSigned=*/true);
    }

    auto* zPtr = emitLValue(zExpr);
    if (!aPtr || !zPtr)
        codegenICE(what + " operand is not an addressable array");

    // The unpacked array must hold `count` components from index i onwards.
    auto* idx  = toI64(emitExpr(iExpr));
    auto* last = builder.CreateSub(
        aHi, llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(count - 1)),
        "pack.last");
    emitRangeCheckDyn(idx, aLo, last, /*isIndex=*/true, iExpr.Loc);

    auto* off = builder.CreateSub(idx, aLo, "pack.off");
    auto* aElem = builder.CreateGEP(elemTy, aPtr, {off}, "pack.a");

    const auto  elemSize = mod->getDataLayout().getTypeAllocSize(elemTy);
    auto* bytes = llvm::ConstantInt::get(i64Ty, elemSize * static_cast<uint64_t>(count));
    const llvm::Align align = mod->getDataLayout().getABITypeAlign(elemTy);
    if (isPack) builder.CreateMemCpy(zPtr, align, aElem, align, bytes);
    else        builder.CreateMemCpy(aElem, align, zPtr, align, bytes);
}
