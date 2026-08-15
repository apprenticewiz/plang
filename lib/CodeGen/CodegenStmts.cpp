#include "CodegenImpl.h"
#include "plang/Basic/SemaUtil.h"
using namespace plang;

// See NumStmtKinds in AstBase.h.
static_assert(NumStmtKinds == 12, "a new statement needs a case in emitStmt");

// ====================================================================
// Statement emission
// ====================================================================

void Codegen::Impl::emitStmt(const StmtNode* stmt) {
    if (!stmt || isTerminated()) return;

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

bool Codegen::Impl::declaresLabel(const BlockNode& block,
                                  const std::string& label) {
    // The parser reduced every label to its apparent integral value, so two
    // spellings of one label are one string here.
    return std::ranges::find(block.Labels, label) != block.Labels.end();
}

void Codegen::Impl::scanNonLocalTargets(const BlockNode& inner,
                                        const BlockNode& block,
                                        std::set<std::string>& found) {
    for (const auto& proc : inner.Procs) {
        if (!proc->Body) continue;
        // A procedure declaring the label itself means its own, and every goto
        // below it names that one rather than the outer block's.
        walkStmts(proc->Body->Body.get(), [&](const StmtNode* s) {
            if (auto* g = llvm::dyn_cast<GotoStmt>(s))
                if (declaresLabel(block, g->Label)
                        && !declaresLabel(*proc->Body, g->Label))
                    found.insert(g->Label);
        });
        scanNonLocalTargets(*proc->Body, block, found);
    }
}

std::set<std::string> Codegen::Impl::nonLocalTargets(const BlockNode& block) {
    std::set<std::string> found;
    scanNonLocalTargets(block, block, found);
    return found;
}

int64_t Codegen::Impl::gotoDispatchValue(const std::string& label) {
    return std::strtoll(label.c_str(), nullptr, 10) + 1;
}

void Codegen::Impl::openLabelScope(const BlockNode& block, bool programBlock) {
    const std::string bufName = "goto.buf$" + std::to_string(labelOwners.size());
    labelOwners.push_back({&block, {}, nullptr});
    if (nonLocalTargets(block).empty()) return;

    auto* bufTy = llvm::ArrayType::get(i64Ty, gotoBufWords);
    llvm::Value* buf = nullptr;
    if (programBlock) {
        // The program block is entered once and stays entered for the run, so
        // one buffer for the whole program is one per activation.  It has to
        // exist before the procedures that jump to it are emitted, and main is
        // emitted after them, so it cannot live in main's frame.
        buf = new llvm::GlobalVariable(*mod, bufTy, /*isConstant=*/false,
                                       llvm::GlobalValue::PrivateLinkage,
                                       llvm::Constant::getNullValue(bufTy),
                                       "plang.goto.buf");
    } else {
        // A procedure may have several activations of itself outstanding, each
        // with its own block to return to, so its buffer belongs to the frame.
        buf = builder.CreateAlloca(bufTy, nullptr, "goto.buf");
    }
    defVar(bufName, buf, bufTy);
    labelOwners.back().bufName = bufName;
    if (!programBlock) emitLabelLanding();
}

void Codegen::Impl::emitLabelLanding() {
    if (labelOwners.empty() || labelOwners.back().bufName.empty()) return;
    const VarEntry* ve = findVar(labelOwners.back().bufName);
    if (!ve) return;

    // ISO §6.8.1 permits the jump only to a label at the outermost level of
    // the statement part, so the landing pad may sit here, ahead of the body,
    // and reach every target with a branch.  It sits after the block's
    // initialization because a goto landing here resumes the block rather than
    // starting it again.
    auto* setjmpFn = getExternFnN("_setjmp", i32Ty, {ptrTy});
    if (auto* f = llvm::dyn_cast<llvm::Function>(setjmpFn))
        f->addFnAttr(llvm::Attribute::ReturnsTwice);
    auto* where = builder.CreateCall(setjmpFn, {ve->ptr}, "goto.where");
    where->addFnAttr(llvm::Attribute::ReturnsTwice);

    auto* body = llvm::BasicBlock::Create(ctx, "goto.body", curFunc);
    labelOwners.back().dispatch = builder.CreateSwitch(where, body);
    builder.SetInsertPoint(body);
}

void Codegen::Impl::closeLabelScope() {
    if (auto* dispatch = labelOwners.back().dispatch) {
        for (const auto& label : nonLocalTargets(*labelOwners.back().block))
            dispatch->addCase(
                llvm::ConstantInt::get(i32Ty, gotoDispatchValue(label)),
                getOrCreateLabel("lbl_" + label));
        pinLocalsToMemory(curFunc);
    }
    labelOwners.pop_back();
}

void Codegen::Impl::pinLocalsToMemory(llvm::Function* f) {
    if (!f) return;
    // The edge from the setjmp to the landing pad is drawn as an ordinary
    // branch out of the entry block, because that is the only way to draw it.
    // Read as one, it says the block is entered with nothing assigned yet, and
    // a variable the optimiser has moved into a register is then given its
    // entry value on that edge: nlg2's `k` came back as 0 rather than 30.
    //
    // The jump really arrives long after entry, and what a variable holds then
    // is what the abandoned frames left in memory.  So the variables of a
    // block that a goto can land in stay in memory, marked here rather than at
    // each access because there is one place to do it and hundreds of those.
    // Only the owning block pays; every other function optimises as before.
    for (auto& inst : f->getEntryBlock()) {
        auto* slot = llvm::dyn_cast<llvm::AllocaInst>(&inst);
        if (!slot) continue;
        for (auto* use : slot->users()) {
            if (auto* load = llvm::dyn_cast<llvm::LoadInst>(use))
                load->setVolatile(true);
            else if (auto* store = llvm::dyn_cast<llvm::StoreInst>(use))
                store->setVolatile(true);
        }
    }
}

void Codegen::Impl::emitGoto(const GotoStmt& s) {
    // A label of the block being emitted is in this function, so the jump is a
    // branch.  Anything else belongs to a block further out.
    if (labelOwners.empty() || declaresLabel(*labelOwners.back().block, s.Label)) {
        builder.CreateBr(getOrCreateLabel("lbl_" + s.Label));
        return;
    }
    const LabelOwner* owner = nullptr;
    for (auto it = labelOwners.rbegin(); it != labelOwners.rend(); ++it)
        if (declaresLabel(*it->block, s.Label)) { owner = &*it; break; }
    if (!owner || owner->bufName.empty()) {
        // Sema accepted the goto, so the label was found and is placed where a
        // jump may land; openLabelScope saw the same program and should have
        // planted the pad.
        codegenICE("no landing pad for non-local goto to label " + s.Label);
        return;
    }
    const VarEntry* ve = findVar(owner->bufName);
    if (!ve) { codegenICE("jump buffer for label " + s.Label + " is out of reach"); return; }

    // _longjmp, not longjmp, to match the _setjmp the landing pad was entered
    // with.  The two forms differ in whether they carry the signal mask, and
    // they have to agree: on macOS longjmp restores a mask from the buffer
    // whatever put it there, so paired with _setjmp, which does not save one,
    // it sets the mask from whatever the buffer happened to hold.  The
    // program-level buffer is zeroed, which unblocks every signal the program
    // had blocked; a procedure's buffer is a stack slot, so it is worse.
    auto* jump = getExternFnN("_longjmp", llvm::Type::getVoidTy(ctx),
                              {ptrTy, i32Ty});
    if (auto* f = llvm::dyn_cast<llvm::Function>(jump))
        f->addFnAttr(llvm::Attribute::NoReturn);
    builder.CreateCall(jump, {ve->ptr,
                              llvm::ConstantInt::get(i32Ty,
                                                     gotoDispatchValue(s.Label))});
    builder.CreateUnreachable();
}

llvm::BasicBlock* Codegen::Impl::getOrCreateLabel(const std::string& name) {
    auto it = labelBlocks.find(name);
    if (it != labelBlocks.end()) return it->second;
    auto* bb = llvm::BasicBlock::Create(ctx, name, curFunc);
    labelBlocks[name] = bb;
    return bb;
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

void Codegen::Impl::emitAssign(const AssignStmt& s) {
    // EP §6.4.7: a whole schematic variable copies its body, whose length only
    // the discriminants know.  It has to run before emitLValue because for p^
    // the body starts past the discriminant header.
    //
    // EITHER side may be the undiscriminated one.  Only the target was handled
    // here, so both halves of the pair took the compiler down: `v := q^` fell
    // through to the ordinary path and asked for the LLVM type of a schema,
    // which by construction has none, and `q^ := v` reached for run-time
    // discriminants that a discriminated instance does not carry.  Both are
    // legal EP.
    {
        const plang::Type* tt = s.Target->ResolvedType.get();
        const plang::Type* vt = s.Value->ResolvedType.get();
        auto isSchema   = [](const plang::Type* T) {
            return T && T->Kind == TypeKind::Schema; };
        auto isInstance = [](const plang::Type* T) {
            return T && T->Kind == TypeKind::SchemaInstance; };

        // Two discriminated instances are ordinary values with a static layout
        // and keep the ordinary path; this is only for a pair where at least
        // one side knows its discriminants no earlier than run time.
        if (isSchema(tt) || (isInstance(tt) && isSchema(vt))) {
            if (!isSchema(vt) && !isInstance(vt))
                codegenICE("assignment between schematic variables that codegen "
                           "cannot locate");
            // The undiscriminated side names the schema and carries the
            // discriminant NAMES; a discriminated instance knows the VALUES at
            // compile time.  schemaActual answers for both shapes, so neither
            // side has to know which the other is -- and once the two agree,
            // either one sizes the copy.
            const plang::Type& schemaTy = isSchema(tt) ? *tt : *vt;
            const auto n = static_cast<unsigned>(schemaTy.SchemaDiscs.size());
            auto [dstData, dstDiscs] = schemaActual(*s.Target, n);
            auto [srcData, srcDiscs] = schemaActual(*s.Value,  n);
            SchemaRef dst{&schemaTy, dstData, dstDiscs};
            SchemaRef src{&schemaTy, srcData, srcDiscs};
            emitSchemaDiscMatch(dst, src);
            builder.CreateMemCpy(dstData, llvm::MaybeAlign(),
                                 srcData, llvm::MaybeAlign(),
                                 schemaBodySize(schemaTy, dstDiscs));
            return;
        }
    }

    // EP §6.5.6: assigning to a substring replaces those characters and leaves
    // the rest of the string as it was, so it cannot go through the ordinary
    // string store, which would replace the whole value.
    if (auto* sub = llvm::dyn_cast<SubstringExpr>(s.Target.get())) {
        auto* dst = emitLValue(*sub->Str);
        if (!dst) codegenICE("assignment to a substring of a non-addressable string");
        // Sizing a temporary needs a constant; what the runtime is told about
        // the destination is the capacity it really has.
        const int64_t cap = exprStrCapStatic(*sub->Str);
        auto* dstCap      = exprStrCapV(*sub->Str);
        auto* low  = toI64(emitExpr(*sub->Low));
        auto* high = toI64(emitExpr(*sub->High));
        auto* n    = builder.CreateAdd(
            builder.CreateSub(high, low, "substr.span"),
            llvm::ConstantInt::get(i64Ty, 1), "substr.len");
        // The value can be written any way a string value can, so it is built
        // where a string belongs and then copied in.
        auto* src = createEntryAlloca(strStructType(cap), "substr.src");
        emitStrStore(src, cap, *s.Value);
        auto* capV = dstCap;
        builder.CreateCall(
            getStrFn("plang_str_substr_assign", llvm::Type::getVoidTy(ctx),
                     {ptrTy, i64Ty, i64Ty, i64Ty, ptrTy, i64Ty}),
            {dst, capV, low, n, src, capV});
        return;
    }

    // EP §6.4.7: a string whose capacity a discriminant fixes needs both an
    // address and a capacity, and each of emitLValue and exprStrCapV resolves
    // the access path from scratch -- so every subscript along the way was
    // emitted twice, and a side-effecting one in `q^.a[next].s := v` ran twice.
    // One walk, both answers.
    if (exprIsVarStr(*s.Target) && s.Target->ResolvedType->ExtentVaries)
        if (auto path = schemaPathOf(*s.Target))
            if (auto* cap = strCapFromPath(*path)) {
                emitStrStore(path->addr, cap, *s.Value);
                return;
            }

    auto* addr = emitLValue(*s.Target);
    if (!addr) codegenICE("assignment to a non-addressable target");

    // EP VarString assignment — dispatch on the Sema-annotated types.
    if (exprIsVarStr(*s.Target)) {
        emitStrStore(addr, exprStrCapV(*s.Target), *s.Value);
        return;
    }

    // ISO §6.4.3.2: a packed array[1..n] of char takes a string value, which
    // may be a literal or a string(n) and so is not an array to load and store.
    if (exprIsCharStr(*s.Target)) {
        emitCharStrStore(addr, exprCharStrLen(*s.Target), *s.Value);
        return;
    }

    auto* rhs = emitExpr(*s.Value);
    if (!rhs) codegenICE("assignment from an unlowerable expression");

    // EP §6.8.7: structured value constructor for arrays/records returns a ptr
    // to a temporary alloca — use memcpy to copy it into the destination.
    if (auto* sve = llvm::dyn_cast<StructuredValueExpr>(s.Value.get())) {
        if (sve->ResolvedType &&
            (sve->ResolvedType->Kind == TypeKind::Array ||
             sve->ResolvedType->Kind == TypeKind::Record)) {
            llvm::Type* ty = llvmTypeOfSemaType(*sve->ResolvedType);
            auto& dl = mod->getDataLayout();
            builder.CreateMemCpy(addr, llvm::MaybeAlign(),
                                 rhs,  llvm::MaybeAlign(),
                                 dl.getTypeAllocSize(ty));
            return;
        }
    }

    // ISO §6.4.2.4: the value assigned to a subrange must lie within it.
    if (const auto& tt = s.Target->ResolvedType;
        tt && tt->Kind == TypeKind::Subrange && rhs->getType()->isIntegerTy()) {
        // EP §6.4.7: `record k: 1..n end` -- the discriminant fixes the range k
        // is checked against, not any storage.  The recorded bounds are the
        // probe's, so the check is re-emitted from the declaration against the
        // discriminants the object carries.
        bool checked = false;
        if (tt->ExtentVaries)
            if (auto path = schemaPathOf(*s.Target)) {
                const TypeNode* d = path->decl;
                while (auto* pk = llvm::dyn_cast_or_null<PackedTypeNode>(d))
                    d = pk->Inner.get();
                if (auto* sr = llvm::dyn_cast_or_null<SubrangeTypeNode>(d)) {
                    // R3: the form, against THIS object's discriminants.  The
                    // bounds used to be re-emitted here as expressions, which
                    // resolved the declaration's names in the assigning
                    // procedure -- so a `const n` in the bound was answered by
                    // any unrelated `var n` in scope at the assignment.
                    auto b = boundsOfDenoter(*sr, path->root);
                    auto* lo = b ? b->first  : nullptr;
                    auto* hi = b ? b->second : nullptr;
                    if (lo && hi) {
                        emitRangeCheckDyn(rhs, lo, hi, /*isIndex=*/false, s.Loc);
                        checked = true;
                    }
                }
            }
        if (!checked && tt->SubLo != tt->SubHi)
            emitRangeCheck(rhs, tt->SubLo, tt->SubHi, /*isIndex=*/false, s.Loc);
    }

    // What the destination holds, not what the source produced: an array
    // element and a record field are just as much a real as a bare variable
    // is, and taking the source's type here stores the integer bit pattern.
    llvm::Type* dstTy = nullptr;
    if (auto* id = llvm::dyn_cast<IdentExpr>(s.Target.get()))
        if (auto* ve = findVar(id->Name)) dstTy = ve->type;
    if (!dstTy && s.Target->ResolvedType)
        dstTy = llvmTypeOfSemaType(*s.Target->ResolvedType);
    if (!dstTy) dstTy = rhs->getType();

    // A set crossing into a type whose base begins elsewhere moves with it.
    if (const auto& tt = s.Target->ResolvedType;
        tt && tt->Kind == TypeKind::Set)
        rhs = alignSet(rhs, setBaseOf(*s.Value), setBaseOf(*s.Target));

    // EP §6.4.2.2: integer/real → complex widening.
    if (dstTy == complexTy() && rhs->getType() != complexTy())
        rhs = coerceToComplex(rhs);
    // Implicit integer-to-real widening.
    if (dstTy->isDoubleTy() && rhs->getType()->isIntegerTy())
        rhs = builder.CreateSIToFP(rhs, dblTy, "widen");
    // Integer narrowing (e.g. i64 → i8 for char assignment).
    if (dstTy->isIntegerTy() && rhs->getType()->isIntegerTy()
            && rhs->getType()->getIntegerBitWidth() > dstTy->getIntegerBitWidth())
        rhs = builder.CreateTrunc(rhs, dstTy, "narrow");

    builder.CreateStore(rhs, addr);
}

void Codegen::Impl::emitIf(const IfStmt& s) {
    // A condition is an arbitrary expression and is not a statement, so a
    // run-time-sized string temporary in one had no scope to be given back at.
    // `while ... do if trim(q^.s) = 'a' then ...` took a fresh piece of stack
    // every pass and died of exhaustion after some tens of thousands.  emitIf
    // and emitCase were the two the first version of StackScope missed:
    // covering the two LOOP conditions and not these mistook "evaluated once"
    // for "evaluated once per program".
    llvm::Value* cond = nullptr;
    { StackScope frame(*this); cond = ensureI1(emitExpr(*s.Cond)); }

    auto* thenBB = llvm::BasicBlock::Create(ctx, "if.then", curFunc);
    auto* endBB  = llvm::BasicBlock::Create(ctx, "if.end",  curFunc);
    auto* elseBB = s.Else ? llvm::BasicBlock::Create(ctx, "if.else", curFunc) : endBB;

    builder.CreateCondBr(cond, thenBB, elseBB);

    builder.SetInsertPoint(thenBB);
    emitStmt(s.Then.get());
    brIfNeeded(endBB);

    if (s.Else) {
        builder.SetInsertPoint(elseBB);
        emitStmt(s.Else.get());
        brIfNeeded(endBB);
    }

    builder.SetInsertPoint(endBB);
}

void Codegen::Impl::emitWhile(const WhileStmt& s) {
    auto* condBB = llvm::BasicBlock::Create(ctx, "while.cond", curFunc);
    auto* bodyBB = llvm::BasicBlock::Create(ctx, "while.body", curFunc);
    auto* endBB  = llvm::BasicBlock::Create(ctx, "while.end",  curFunc);

    builder.CreateBr(condBB);
    builder.SetInsertPoint(condBB);
    // A condition is re-evaluated on every pass and is not a statement, so a
    // run-time-sized string temporary in one -- `while trim(q^) <> '' do` --
    // would otherwise take a fresh piece of stack per iteration and never give
    // any of it back.  Scoped here for the same reason a simple statement is.
    llvm::Value* cond = nullptr;
    { StackScope frame(*this); cond = ensureI1(emitExpr(*s.Cond)); }
    if (!cond) { builder.SetInsertPoint(endBB); return; }
    builder.CreateCondBr(cond, bodyBB, endBB);

    builder.SetInsertPoint(bodyBB);
    emitStmt(s.Body.get());
    brIfNeeded(condBB);

    builder.SetInsertPoint(endBB);
}

void Codegen::Impl::emitFor(const ForStmt& s) {
    auto* ve = findVar(s.Var);
    if (!ve) codegenICE("for-loop control variable '" + s.Var + "' has no storage");

    // ISO §6.8.3.9 gives the for-statement as an equivalent that evaluates the
    // initial and final values, and only then assigns the control variable.
    // Both are therefore evaluated once, and both before the store: in
    // `i := 10; for i := 1 to i`, the limit is the 10 that was there when the
    // statement began, and storing 1 first made the loop run a single time.
    auto* fromVal  = coerceToType(emitExpr(*s.From),  ve->type);
    auto* limitVal = coerceToType(emitExpr(*s.Limit), ve->type);
    builder.CreateStore(fromVal, ve->ptr);

    auto* condBB = llvm::BasicBlock::Create(ctx, "for.cond", curFunc);
    auto* bodyBB = llvm::BasicBlock::Create(ctx, "for.body", curFunc);
    auto* incBB  = llvm::BasicBlock::Create(ctx, "for.inc",  curFunc);
    auto* endBB  = llvm::BasicBlock::Create(ctx, "for.end",  curFunc);

    builder.CreateBr(condBB);

    const bool uns = ordinalIsUnsigned(s.From->ResolvedType.get())
                     || ordinalIsUnsigned(s.Limit->ResolvedType.get());

    builder.SetInsertPoint(condBB);
    auto* cur  = builder.CreateLoad(ve->type, ve->ptr, "for.cur");
    llvm::Value* cmp = s.Downto
        ? (uns ? builder.CreateICmpUGE(cur, limitVal, "for.cmp")
               : builder.CreateICmpSGE(cur, limitVal, "for.cmp"))
        : (uns ? builder.CreateICmpULE(cur, limitVal, "for.cmp")
               : builder.CreateICmpSLE(cur, limitVal, "for.cmp"));
    builder.CreateCondBr(cmp, bodyBB, endBB);

    builder.SetInsertPoint(bodyBB);
    emitStmt(s.Body.get());
    brIfNeeded(incBB);

    // ISO §6.8.3.9: the control variable is never advanced past the final
    // value.  Testing for it here rather than incrementing and re-comparing is
    // what makes `to maxint` and `to true` terminate instead of wrapping.
    builder.SetInsertPoint(incBB);
    auto* stepBB = llvm::BasicBlock::Create(ctx, "for.step", curFunc);
    auto* cur2 = builder.CreateLoad(ve->type, ve->ptr, "for.cur2");
    builder.CreateCondBr(
        builder.CreateICmpEQ(cur2, limitVal, "for.atlimit"), endBB, stepBB);

    builder.SetInsertPoint(stepBB);
    auto* one  = llvm::ConstantInt::get(ve->type, 1);
    auto* next = s.Downto
        ? builder.CreateSub(cur2, one, "for.dec")
        : builder.CreateAdd(cur2, one, "for.inc");
    builder.CreateStore(next, ve->ptr);
    builder.CreateBr(condBB);

    builder.SetInsertPoint(endBB);
}

// EP §6.9.3.9.3: for v in set-expr do stmt
// Iterate every bit position the set representation can hold, executing the
// body with v bound to each ordinal whose bit is set.  Ordinals ascend, which
// is the order the standard requires.
void Codegen::Impl::emitForIn(const ForInStmt& s) {
    // Evaluate the set expression once.
    auto* setVal = emitExpr(*s.SetExpr);
    if (!setVal) codegenICE("'for ... in' over an unlowerable set expression");
    setVal = toSetWidth(setVal);

    // The loop variable takes the set's element type, so that a `set of char`
    // yields characters rather than their ordinals.
    llvm::Type* elemTy = i64Ty;
    if (const auto& st = s.SetExpr->ResolvedType; st && st->ElemType)
        elemTy = llvmTypeOfSemaType(*st->ElemType);
    if (!elemTy->isIntegerTy())
        codegenICE("'for ... in' over a set with a non-ordinal element type");

    // EP §6.9.3.9.3: the control variable is a variable-access, so the
    // DECLARED variable takes each value.  A fresh alloca bound under the same
    // name gave the loop body one variable and everything else another -- a
    // procedure called from the body read the declared variable, which the
    // loop never wrote, and saw it unset both times round.
    llvm::Value* loopVar = nullptr;
    if (const auto* ve = findVar(s.Var); ve && ve->ptr && ve->type == elemTy)
        loopVar = ve->ptr;
    auto* bitAlloca = createEntryAlloca(i64Ty, "forin.bit");
    builder.CreateStore(llvm::ConstantInt::get(i64Ty, 0), bitAlloca);

    if (!loopVar) {
        // No declaration to write into, or one of a shape this loop cannot
        // store through: keep the old behaviour rather than store somewhere
        // the program did not ask for.
        loopVar = createEntryAlloca(elemTy, s.Var + ".addr");
        defVar(s.Var, loopVar, elemTy);
    }

    auto* condBB = llvm::BasicBlock::Create(ctx, "forin.cond", curFunc);
    auto* testBB = llvm::BasicBlock::Create(ctx, "forin.test", curFunc);
    auto* bodyBB = llvm::BasicBlock::Create(ctx, "forin.body", curFunc);
    auto* incBB  = llvm::BasicBlock::Create(ctx, "forin.inc",  curFunc);
    auto* endBB  = llvm::BasicBlock::Create(ctx, "forin.end",  curFunc);

    builder.CreateBr(condBB);

    // Condition: bit < PlangMaxSetElements
    builder.SetInsertPoint(condBB);
    auto* bit = builder.CreateLoad(i64Ty, bitAlloca, "forin.bit");
    auto* cmp = builder.CreateICmpULT(
        bit, llvm::ConstantInt::get(i64Ty, PlangMaxSetElements), "forin.cmp");
    builder.CreateCondBr(cmp, testBB, endBB);

    // Test: is this bit set in the mask?
    builder.SetInsertPoint(testBB);
    auto* bit2  = builder.CreateLoad(i64Ty, bitAlloca, "forin.bit2");
    auto* one   = llvm::ConstantInt::get(i64Ty, 1);
    auto* shifted = builder.CreateLShr(setVal,
        builder.CreateZExt(bit2, setTy()), "forin.shr");
    auto* isMem = builder.CreateTrunc(shifted, i1Ty, "forin.ismem");
    builder.CreateCondBr(isMem, bodyBB, incBB);

    // Body: store ordinal into loop variable and run the body.  The counter
    // walks bit positions, so a set based below zero needs its base added back
    // to turn the position into the ordinal the program declared.
    builder.SetInsertPoint(bodyBB);
    llvm::Value* bit3 = builder.CreateLoad(i64Ty, bitAlloca, "forin.bit3");
    if (const int64_t base = setBaseOf(*s.SetExpr); base != 0)
        bit3 = builder.CreateAdd(bit3, llvm::ConstantInt::get(i64Ty, base, true),
                                 "forin.rebase");
    builder.CreateStore(builder.CreateZExtOrTrunc(bit3, elemTy, "forin.ord"), loopVar);
    emitStmt(s.Body.get());
    brIfNeeded(incBB);

    // Increment bit counter.
    builder.SetInsertPoint(incBB);
    auto* bit4 = builder.CreateLoad(i64Ty, bitAlloca, "forin.bit4");
    auto* next = builder.CreateAdd(bit4, one, "forin.next");
    builder.CreateStore(next, bitAlloca);
    builder.CreateBr(condBB);

    builder.SetInsertPoint(endBB);
}

void Codegen::Impl::emitRepeat(const RepeatStmt& s) {
    auto* bodyBB = llvm::BasicBlock::Create(ctx, "repeat.body", curFunc);
    auto* condBB = llvm::BasicBlock::Create(ctx, "repeat.cond", curFunc);
    auto* endBB  = llvm::BasicBlock::Create(ctx, "repeat.end",  curFunc);

    builder.CreateBr(bodyBB);

    builder.SetInsertPoint(bodyBB);
    for (const auto& st : s.Stmts) {
        if (isTerminated()) resumeAfterTerminator();
        emitStmt(st.get());
    }
    brIfNeeded(condBB);

    builder.SetInsertPoint(condBB);
    // Re-evaluated per pass; see emitWhile.
    llvm::Value* cond = nullptr;
    { StackScope frame(*this); cond = ensureI1(emitExpr(*s.Cond)); }
    // Branching to endBB here instead would turn the loop into a single
    // unconditional pass through the body.
    if (!cond) codegenICE("'repeat' with an unlowerable termination condition");
    builder.CreateCondBr(cond, endBB, bodyBB);

    builder.SetInsertPoint(endBB);
}

// case selector of const-list: stmt ; ... end
// Uses an if-else chain to support both point and lo..hi range labels (EP).
void Codegen::Impl::emitCase(const CaseStmt& s) {
    // Re-evaluated on every pass through an enclosing loop; see emitIf.
    llvm::Value* sel = nullptr;
    { StackScope frame(*this); sel = toI64(emitExpr(*s.Selector)); }
    auto* endBB = llvm::BasicBlock::Create(ctx, "case.end", curFunc);

    // Chain: for each arm build a test block and a body block.
    // nextBB starts as a newly-created block so each arm can branch to the next test.
    auto* nextBB = llvm::BasicBlock::Create(ctx, "case.test", curFunc);
    builder.CreateBr(nextBB);

    for (const auto& arm : s.Arms) {
        builder.SetInsertPoint(nextBB);
        nextBB = llvm::BasicBlock::Create(ctx, "case.test", curFunc);
        auto* armBB = llvm::BasicBlock::Create(ctx, "case.arm", curFunc);

        // Build condition: OR of all labels in this arm.
        llvm::Value* cond = llvm::ConstantInt::getFalse(ctx);
        for (const auto& lbl : arm.Labels) {
            llvm::Value* match;
            if (lbl.High) {
                // Range lo..hi: sel >= lo && sel <= hi
                auto* lo  = toI64(emitExpr(*lbl.Low));
                auto* hi  = toI64(emitExpr(*lbl.High));
                auto* geq = builder.CreateICmpSGE(sel, lo, "rlo");
                auto* leq = builder.CreateICmpSLE(sel, hi, "rhi");
                match     = builder.CreateAnd(geq, leq, "range");
            } else {
                auto* val = toI64(emitExpr(*lbl.Low));
                match     = builder.CreateICmpEQ(sel, val, "eq");
            }
            cond = builder.CreateOr(cond, match, "lbl");
        }
        builder.CreateCondBr(cond, armBB, nextBB);

        builder.SetInsertPoint(armBB);
        emitStmt(arm.Body.get());
        brIfNeeded(endBB);
    }

    // else / otherwise / default branch
    builder.SetInsertPoint(nextBB);
    if (s.HasElse) {
        if (s.Else) emitStmt(s.Else.get());
    } else {
        // ISO §6.8.3.5: reaching here means no label matched, which is an
        // error rather than a silent no-op.
        builder.CreateCall(
            getExternFnN("plang_err_no_case", llvm::Type::getVoidTy(ctx), {i64Ty}),
            {sel});
        builder.CreateUnreachable();
    }
    brIfNeeded(endBB);

    builder.SetInsertPoint(endBB);
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
    // EP §6.7.5.2: SeekRead / SeekWrite / SeekUpdate
    if ((lo == "seekread" || lo == "seekwrite" || lo == "seekupdate")
        && s.Args.size() >= 2) {
        auto* fp      = fileVarPtr(*s.Args[0]);
        auto* idx     = toI64(emitExpr(*s.Args[1]));
        int64_t esz   = getFileElemSize(*s.Args[0]);
        auto* fn = getExternFnN("plang_" + lo,
            llvm::Type::getVoidTy(ctx), {ptrTy, i64Ty, i64Ty});
        builder.CreateCall(fn, {fp, idx, llvm::ConstantInt::get(i64Ty, esz)});
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
        // EP §6.9.7 halt takes no argument; halt(n) is the widespread extension
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
        int64_t         Bytes  = 0;
        const TypeNode* domain = nullptr;
        if (auto* id = llvm::dyn_cast<IdentExpr>(s.Args[0].get()))
            if (auto* ve = findVar(id->Name))
                if (auto* ptn = llvm::dyn_cast_or_null<PointerTypeNode>(
                        denoterOf(ve->typeNode))) {
                    domain = ptn->Base.get();
                    auto* pointeeTy = llvmTypeOfNode(*domain);
                    Bytes = (int64_t)mod->getDataLayout().getTypeAllocSize(pointeeTy);
                }
        if (Bytes == 0)
            if (const auto& pt = s.Args[0]->ResolvedType;
                    pt && pt->Kind == TypeKind::Pointer && pt->PointeeType)
                Bytes = (int64_t)mod->getDataLayout().getTypeAllocSize(
                    llvmTypeOfSemaType(*pt->PointeeType));
        // The domain type, for the initial state below.  Only the identifier
        // route set it, so `new(h.p)` and `new(a[1])` applied no initial state
        // at all: the size already fell back to Sema's type and this did not.
        // A record is what carries field `value` clauses, and Sema's type
        // knows the declaration it came from.
        if (!domain)
            if (const auto& pt = s.Args[0]->ResolvedType;
                    pt && pt->Kind == TypeKind::Pointer && pt->PointeeType)
                domain = pt->PointeeType->RecordDecl;
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
    // conformantDims[i] is the dimension list for the i-th AST argument position.
    // An empty list means the param is not conformant (emit normally).
    const std::vector<std::vector<std::pair<std::string,std::string>>>* cDims = nullptr;
    {
        auto cit = conformantParamDims_.find(mangledName);
        if (cit != conformantParamDims_.end())
            cDims = &cit->second;
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

        bool isConformant = cDims && astArgIdx < cDims->size()
                            && !(*cDims)[astArgIdx].empty();

        if (isConformant) {
            const size_t dims = (*cDims)[astArgIdx].size();
            pushConformantArgs(args, *arg, dims);
            pi += 1 + 2 * dims;
        } else {
            // Regular param (var or value).
            args.push_back(alignSetArg(
                emitCallArg(*arg,
                    pi < callee->arg_size()
                        ? callee->getFunctionType()->getParamType(pi) : nullptr,
                    paramIsByRef(mangledName, astArgIdx)),
                *arg, mangledName, astArgIdx));
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

void Codegen::Impl::emitWith(const WithStmt& s) {
    // with r1, r2 do stmt — open each record's fields as local variables,
    // innermost record taking priority (last opened = first consulted).
    pushScope();
    for (const auto& rec : s.Records) {
        if (!rec->ResolvedType) continue;

        // EP §6.4.7: an undiscriminated schema has no struct to GEP into, so
        // each field is bound to the address the run-time layout gives it, and
        // each discriminant to the value the object carries.
        //
        // Asked of the ACCESS PATH and not of the type's kind.  `with q^ do` is
        // a Schema; `with q^.inner do` is an ordinary Record that merely lives
        // inside one, and keying on the kind sent it to the static branch
        // below -- where the nested `string(n)` was bound at the probe's
        // capacity, so reading a field worked and assigning to one raised
        // "string of length 7 assigned to a string(1)" on legal code.
        // Whether there is a struct to GEP into is a question about the
        // storage, which is what the path knows and the kind does not.
        const bool isInstance =
            rec->ResolvedType->Kind == TypeKind::SchemaInstance;
        // schemaPathOf EMITS the access path -- every subscript in it -- so its
        // result is kept whatever happens next.  Discarding it and letting the
        // static branch call emitLValue below emitted the path a SECOND time:
        // `with q^.a[idx] do` called idx twice, bound the element the second
        // call chose, and left a live range check on the first.  ISO §6.8.3.10
        // says the record-variable is evaluated once.
        std::optional<SchemaPath> path;
        if (!isInstance) path = schemaPathOf(*rec);
        if (!isInstance) {
            const RecordTypeNode* rt =
                path ? llvm::dyn_cast_or_null<RecordTypeNode>(
                           peelPackedNode(path->decl))
                     : nullptr;
            if (path && rt) {
                // The discriminants belong to the schematic variable, not to a
                // record nested inside it, so they are exposed only where the
                // body IS the schema's.
                const bool isBody = rec->ResolvedType->Kind == TypeKind::Schema;
                if (isBody) {
                    const auto& discs = rec->ResolvedType->SchemaDiscs;
                    for (size_t i = 0;
                         i < discs.size() && i < path->root.discs.size(); ++i) {
                        auto* slot = createEntryAlloca(i64Ty,
                                                       "disc." + discs[i].Name);
                        builder.CreateStore(path->root.discs[i], slot);
                        defVar(discs[i].Name, slot, i64Ty);
                    }
                }
                {
                    const auto& fields =
                        isBody ? rec->ResolvedType->SchemaBody->RecordFields
                               : rec->ResolvedType->RecordFields;
                    for (const auto& F : fields) {
                        auto* off = [&] {
                            RtDiscScope disc(*this, path->root.discs);
                            return rtFieldOffset(*rt, F.Name);
                        }();
                        auto* fp = builder.CreateGEP(i8Ty, path->addr, {off},
                                                     "with.fld");
                        defVar(F.Name, fp,
                               F.Ty ? llvmTypeOfSemaType(*F.Ty) : i64Ty);
                        // Keep the path, not just the address: an array field
                        // bound here is still indexed, and a nested record is
                        // still selected from.
                        setVarSchemaPath(F.Name, path->root,
                                         fieldDenoterOf(*rt, F.Name));
                        // A varying string field: record what its capacity
                        // really is, since the bound name loses the path.
                        if (F.Ty && F.Ty->Kind == TypeKind::VarString
                                && F.Ty->ExtentVaries)
                            if (auto* st = llvm::dyn_cast_or_null<StringTypeNode>(
                                    fieldDenoterOf(*rt, F.Name))) {
                                // R3: the form, not the declaration's
                                // expression re-emitted in the with-statement's
                                // scope.
                                if (!st->ExtentLow)
                                    codegenICE("a with over a schema string "
                                               "field with no capacity form");
                                auto* cap = emitExtentForm(*st->ExtentLow,
                                                           path->root.discs);
                                setVarStrCap(F.Name, cap);
                            }
                    }
                }
                continue;
            }
        }

        // EP §6.4.7: schema instance — expose discriminants as constant vars
        // and body record fields as normal GEP-derived vars.
        if (rec->ResolvedType->Kind == TypeKind::SchemaInstance) {
            // Expose discriminants as alloca'd integer constants.
            for (const auto& D : rec->ResolvedType->SchemaDiscs) {
                auto* alloca = createEntryAlloca(i64Ty, "disc." + D.Name);
                builder.CreateStore(
                    llvm::ConstantInt::get(i64Ty,
                        static_cast<uint64_t>(D.Value), /*isSigned=*/true),
                    alloca);
                defVar(D.Name, alloca, i64Ty);
            }
            // If body is a record, expose its fields via GEP.
            auto* body = rec->ResolvedType->SchemaBody.get();
            if (body && body->Kind == TypeKind::Record) {
                auto* recPtr = emitLValue(*rec);
                if (!recPtr) codegenICE("'with' on a schema instance without an address");
                llvm::StructType* st = nullptr;
                if (auto* id = llvm::dyn_cast<IdentExpr>(rec.get()))
                    if (auto* ve = findVar(id->Name))
                        st = llvm::dyn_cast<llvm::StructType>(ve->type);
                if (!st)
                    st = llvm::dyn_cast<llvm::StructType>(llvmTypeOfSemaType(*body));
                if (st) {
                    // Through the layout, exactly as the record case below
                    // does: Sema's field list is flattened and the struct has
                    // one blob for all the variants, so pairing them by
                    // position binds the first variant field to the blob and
                    // never binds the rest.  A schema body may have a variant
                    // part like any other record, and this is that same walk.
                    auto* zero = llvm::ConstantInt::get(i32Ty, 0);
                    const RecordLayout* L = layoutOfRecord(*body);
                    unsigned ElemIdx = 0;
                    for (const auto& F : body->RecordFields) {
                        llvm::Value* fldPtr = nullptr;
                        llvm::Type*  fldTy  = nullptr;
                        if (L) {
                            const auto It = L->Fields.find(toLower(F.Name));
                            if (It == L->Fields.end()) continue;
                            const auto& P = It->second;
                            fldPtr = builder.CreateGEP(
                                st, recPtr,
                                {zero, llvm::ConstantInt::get(i32Ty, P.Index)},
                                "with." + F.Name);
                            if (P.InVariant && P.Offset != 0)
                                fldPtr = builder.CreateConstGEP1_64(
                                    i8Ty, fldPtr, P.Offset, "with." + F.Name);
                            fldTy = P.Ty;
                        } else {
                            if (ElemIdx >= st->getNumElements()) break;
                            fldPtr = builder.CreateGEP(
                                st, recPtr,
                                {zero, llvm::ConstantInt::get(i32Ty, ElemIdx)},
                                "with." + F.Name);
                            fldTy = st->getElementType(ElemIdx);
                        }
                        defVar(F.Name, fldPtr, fldTy);
                        ++ElemIdx;
                    }
                }
            }
            continue;
        }

        // Reuse the address schemaPathOf already emitted, when it produced one.
        // Calling emitLValue here regardless is what evaluated the record
        // variable a second time.
        auto* recPtr = path ? path->addr : emitLValue(*rec);
        if (!recPtr) codegenICE("'with' on a record that has no address");
        if (rec->ResolvedType->Kind != TypeKind::Record)
            codegenICE("'with' on a non-record operand");

        // Get the LLVM struct type from the variable entry (needed for GEP).
        llvm::StructType* st = nullptr;
        if (auto* id = llvm::dyn_cast<IdentExpr>(rec.get()))
            if (auto* ve = findVar(id->Name))
                st = llvm::dyn_cast<llvm::StructType>(ve->type);
        // Records reached through an index or a dereference have no variable
        // entry of their own, so fall back to the type Sema resolved.
        if (!st)
            st = llvm::dyn_cast<llvm::StructType>(
                     llvmTypeOfSemaType(*rec->ResolvedType));
        if (!st) codegenICE("cannot resolve the struct type for a 'with' record");

        // Expose each field as a named variable pointing into the record
        // struct, through the SAME layout that r.f goes through.
        //
        // This used to pair Sema's field list positionally with the struct's
        // elements, and the two are not the same list.  §6.4.3.3 lets a variant
        // field be selected by name like any other, so Sema's list is flattened
        // -- fixed fields, the tag, then every alternative's fields -- while the
        // struct holds the fixed fields, the tag, and ONE blob shared by all the
        // alternatives.  So the first variant field was bound to the blob and
        // every later one ran off the end of the struct and was not bound at
        // all: `with r do c := 4` stored an integer bit pattern into a real, and
        // `with r do b := 22` referred to a `pasg_b` that no one defined and
        // failed at link time.
        auto* zero = llvm::ConstantInt::get(i32Ty, 0);
        const RecordLayout* L = layoutOfRecord(*rec->ResolvedType);
        for (const auto& F : rec->ResolvedType->RecordFields) {
            llvm::Value* fldPtr = nullptr;
            llvm::Type*  fldTy  = nullptr;
            if (L) {
                const auto It = L->Fields.find(toLower(F.Name));
                if (It == L->Fields.end()) continue;
                const auto& P = It->second;
                fldPtr = builder.CreateGEP(
                    st, recPtr, {zero, llvm::ConstantInt::get(i32Ty, P.Index)},
                    "with." + F.Name);
                if (P.InVariant && P.Offset != 0)
                    fldPtr = builder.CreateConstGEP1_64(i8Ty, fldPtr, P.Offset,
                                                        "with." + F.Name);
                fldTy = P.Ty;
            } else {
                // A record with no declaration to lay out -- one that came in
                // through an interface file.  Positional is all there is, and
                // it is right for a record with no variant part.
                const unsigned Idx =
                    static_cast<unsigned>(&F - rec->ResolvedType->RecordFields.data());
                if (Idx >= st->getNumElements()) break;
                fldPtr = builder.CreateGEP(st, recPtr,
                             {zero, llvm::ConstantInt::get(i32Ty, Idx)},
                             "with." + F.Name);
                fldTy = st->getElementType(Idx);
            }
            defVar(F.Name, fldPtr, fldTy);
        }
    }
    emitStmt(s.Body.get());
    popScope();
}
