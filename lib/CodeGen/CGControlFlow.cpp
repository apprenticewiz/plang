#include "CGControlFlow.h"

#include "llvm/IR/Constants.h"

#include "plang/AST/Ast.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"

using namespace plang;

void CGControlFlow::emitIf(const IfStmt& s) {
    // A condition is an arbitrary expression and is not a statement, so a
    // run-time-sized string temporary in one had no scope to be given back at.
    // `while ... do if trim(q^.s) = 'a' then ...` took a fresh piece of stack
    // every pass and died of exhaustion after some tens of thousands.  emitIf
    // and emitCase were the two the first version of StackScope missed:
    // covering the two LOOP conditions and not these mistook "evaluated once"
    // for "evaluated once per program".
    llvm::Value* cond = WithStackScope([&]{ return EnsureI1(EmitExpr(*s.Cond)); });

    auto* thenBB = llvm::BasicBlock::Create(Ctx, "if.then", CurFn);
    auto* endBB  = llvm::BasicBlock::Create(Ctx, "if.end",  CurFn);
    auto* elseBB = s.Else ? llvm::BasicBlock::Create(Ctx, "if.else", CurFn) : endBB;

    B.CreateCondBr(cond, thenBB, elseBB);

    B.SetInsertPoint(thenBB);
    EmitStmt(s.Then.get());
    BrIfNeeded(endBB);

    if (s.Else) {
        B.SetInsertPoint(elseBB);
        EmitStmt(s.Else.get());
        BrIfNeeded(endBB);
    }

    B.SetInsertPoint(endBB);
}

void CGControlFlow::emitWhile(const WhileStmt& s) {
    auto* condBB = llvm::BasicBlock::Create(Ctx, "while.cond", CurFn);
    auto* bodyBB = llvm::BasicBlock::Create(Ctx, "while.body", CurFn);
    auto* endBB  = llvm::BasicBlock::Create(Ctx, "while.end",  CurFn);

    B.CreateBr(condBB);
    B.SetInsertPoint(condBB);
    // A condition is re-evaluated on every pass and is not a statement, so a
    // run-time-sized string temporary in one -- `while trim(q^) <> '' do` --
    // would otherwise take a fresh piece of stack per iteration and never give
    // any of it back.  Scoped here for the same reason a simple statement is.
    llvm::Value* cond = WithStackScope([&]{ return EnsureI1(EmitExpr(*s.Cond)); });
    if (!cond) { B.SetInsertPoint(endBB); return; }
    B.CreateCondBr(cond, bodyBB, endBB);

    B.SetInsertPoint(bodyBB);
    // TP-only: `while`'s continue-target is its own condition-test block --
    // a Continue here re-evaluates cond exactly the way falling off the end
    // of the body already does, which is also why `break`'s target is
    // simply endBB, the same block the condition already branches to on
    // false.
    PushLoopTargets(condBB, endBB);
    EmitStmt(s.Body.get());
    PopLoopTargets();
    BrIfNeeded(condBB);

    B.SetInsertPoint(endBB);
}

void CGControlFlow::emitFor(const ForStmt& s) {
    auto* ve = SymTab.findVar(s.Var);
    if (!ve) codegenICE("for-loop control variable '" + s.Var + "' has no storage");

    // ISO §6.8.3.9 gives the for-statement as an equivalent that evaluates the
    // initial and final values, and only then assigns the control variable.
    // Both are therefore evaluated once, and both before the store: in
    // `i := 10; for i := 1 to i`, the limit is the 10 that was there when the
    // statement began, and storing 1 first made the loop run a single time.
    auto* fromVal  = CoerceToType(EmitExpr(*s.From),  ve->type);
    auto* limitVal = CoerceToType(EmitExpr(*s.Limit), ve->type);
    B.CreateStore(fromVal, ve->ptr);

    auto* condBB = llvm::BasicBlock::Create(Ctx, "for.cond", CurFn);
    auto* bodyBB = llvm::BasicBlock::Create(Ctx, "for.body", CurFn);
    auto* incBB  = llvm::BasicBlock::Create(Ctx, "for.inc",  CurFn);
    auto* endBB  = llvm::BasicBlock::Create(Ctx, "for.end",  CurFn);

    B.CreateBr(condBB);

    // s.VarType (the control variable's own declared type; see its comment,
    // AstStmt.h) has to be consulted too, not just the bounds: both fromVal
    // and limitVal above are already coerced into the CONTROL VARIABLE's
    // storage width before the compare below ever runs, so what the compare
    // must respect is that storage's signedness.  From/Limit's own types
    // are ordinarily the same signedness (a bound that is itself a variable
    // or typed constant matches the loop variable it is assignment-
    // compatible with), but a bare integer literal bound is always the
    // dialect's plain signed Integer regardless of value -- `for w := 0 to
    // 65535 do` with `w: Word` used to read 65535 (truncated into Word's
    // 16 bits, the all-ones bit pattern) as -1 and run zero iterations.
    const bool uns = OrdinalIsUnsigned(s.VarType.get())
                     || OrdinalIsUnsigned(s.From->ResolvedType.get())
                     || OrdinalIsUnsigned(s.Limit->ResolvedType.get());

    B.SetInsertPoint(condBB);
    auto* cur  = B.CreateLoad(ve->type, ve->ptr, "for.cur");
    llvm::Value* cmp = s.Downto
        ? (uns ? B.CreateICmpUGE(cur, limitVal, "for.cmp")
               : B.CreateICmpSGE(cur, limitVal, "for.cmp"))
        : (uns ? B.CreateICmpULE(cur, limitVal, "for.cmp")
               : B.CreateICmpSLE(cur, limitVal, "for.cmp"));
    B.CreateCondBr(cmp, bodyBB, endBB);

    B.SetInsertPoint(bodyBB);
    // TP-only: `for`'s continue-target is its INCREMENT block (incBB), not
    // condBB -- incBB tests whether the control variable is already AT the
    // limit before advancing it (see below), which is what makes `to
    // maxint` terminate correctly instead of wrapping.  Landing a Continue
    // on condBB instead would re-test the CURRENT (unincremented) value
    // against the limit, find it still in range, and re-enter the body
    // without ever advancing -- an infinite loop on the same iteration.
    PushLoopTargets(incBB, endBB);
    EmitStmt(s.Body.get());
    PopLoopTargets();
    BrIfNeeded(incBB);

    // ISO §6.8.3.9: the control variable is never advanced past the final
    // value.  Testing for it here rather than incrementing and re-comparing is
    // what makes `to maxint` and `to true` terminate instead of wrapping.
    B.SetInsertPoint(incBB);
    auto* stepBB = llvm::BasicBlock::Create(Ctx, "for.step", CurFn);
    auto* cur2 = B.CreateLoad(ve->type, ve->ptr, "for.cur2");
    B.CreateCondBr(
        B.CreateICmpEQ(cur2, limitVal, "for.atlimit"), endBB, stepBB);

    B.SetInsertPoint(stepBB);
    auto* one  = llvm::ConstantInt::get(ve->type, 1);
    auto* next = s.Downto
        ? B.CreateSub(cur2, one, "for.dec")
        : B.CreateAdd(cur2, one, "for.inc");
    B.CreateStore(next, ve->ptr);
    B.CreateBr(condBB);

    B.SetInsertPoint(endBB);
}

// EP §6.9.3.9.3: for v in set-expr do stmt
// Iterate every bit position the set representation can hold, executing the
// body with v bound to each ordinal whose bit is set.  Ordinals ascend, which
// is the order the standard requires.
void CGControlFlow::emitForIn(const ForInStmt& s) {
    // Evaluate the set expression once.
    auto* setVal = EmitExpr(*s.SetExpr);
    if (!setVal) codegenICE("'for ... in' over an unlowerable set expression");
    setVal = Sets.toSetWidth(setVal);

    // The loop variable takes the set's element type, so that a `set of char`
    // yields characters rather than their ordinals.
    llvm::Type* elemTy = I64Ty;
    if (const auto& st = s.SetExpr->ResolvedType; st && st->ElemType)
        elemTy = Types.llvmTypeOfSemaType(*st->ElemType);
    if (!elemTy->isIntegerTy())
        codegenICE("'for ... in' over a set with a non-ordinal element type");

    // EP §6.9.3.9.3: the control variable is a variable-access, so the
    // DECLARED variable takes each value.  A fresh alloca bound under the same
    // name gave the loop body one variable and everything else another -- a
    // procedure called from the body read the declared variable, which the
    // loop never wrote, and saw it unset both times round.
    //
    // Issue #217: the declared variable's own LLVM storage need not be the
    // same WIDTH as the set's element type -- `c: 'a'..'z'` is i64 (every
    // ordinal but Char and Boolean is stored at full width; see
    // TypeContext::getSubrange) while `set of char`'s element type is i8 --
    // and writing through it just needs the same zext-or-trunc an ordinary
    // assignment already gets.  Requiring an exact type match sent that
    // (entirely legal) case down the "no declaration" branch below, which
    // used to mint an alloca under the SAME name and rebind it into the
    // CURRENT scope with no push/pop to ever undo it.  The rebinding then
    // outlived the loop: a nested procedure that had already captured the
    // real variable's address, at its declared width, kept resolving the
    // name to the new and narrower alloca for the rest of the block, and
    // read past its end -- confirmed with AddressSanitizer, a
    // stack-buffer-overflow read of 8 bytes out of a 1-byte object.  The same
    // rebind also meant any further write to a mismatched GLOBAL control
    // variable landed on the throwaway local alloca instead, so the global
    // itself silently stopped tracking the loop at all.
    llvm::Value* loopVar   = nullptr;
    llvm::Type*  loopVarTy = elemTy;
    if (const auto* ve = SymTab.findVar(s.Var); ve && ve->ptr && ve->type->isIntegerTy()) {
        loopVar   = ve->ptr;
        loopVarTy = ve->type;
    }
    auto* bitAlloca = CreateEntryAlloca(I64Ty, "forin.bit");
    B.CreateStore(llvm::ConstantInt::get(I64Ty, 0), bitAlloca);

    // No declaration to write into, or one of a shape (non-ordinal) this loop
    // cannot store through: bind a fresh alloca under the same name instead
    // of storage the program never gave it.  Scoped to the body alone --
    // pushed here, popped the moment it is emitted -- so this stand-in name
    // cannot shadow the real one for anything that follows the loop.
    bool pushedForInScope = false;
    if (!loopVar) {
        SymTab.pushScope();
        pushedForInScope = true;
        loopVar = CreateEntryAlloca(elemTy, s.Var + ".addr");
        SymTab.defVar(s.Var, loopVar, elemTy);
    }

    auto* condBB = llvm::BasicBlock::Create(Ctx, "forin.cond", CurFn);
    auto* testBB = llvm::BasicBlock::Create(Ctx, "forin.test", CurFn);
    auto* bodyBB = llvm::BasicBlock::Create(Ctx, "forin.body", CurFn);
    auto* incBB  = llvm::BasicBlock::Create(Ctx, "forin.inc",  CurFn);
    auto* endBB  = llvm::BasicBlock::Create(Ctx, "forin.end",  CurFn);

    B.CreateBr(condBB);

    // Condition: bit < PlangMaxSetElements
    B.SetInsertPoint(condBB);
    auto* bit = B.CreateLoad(I64Ty, bitAlloca, "forin.bit");
    auto* cmp = B.CreateICmpULT(
        bit, llvm::ConstantInt::get(I64Ty, PlangMaxSetElements), "forin.cmp");
    B.CreateCondBr(cmp, testBB, endBB);

    // Test: is this bit set in the mask?
    B.SetInsertPoint(testBB);
    auto* bit2  = B.CreateLoad(I64Ty, bitAlloca, "forin.bit2");
    auto* one   = llvm::ConstantInt::get(I64Ty, 1);
    auto* shifted = B.CreateLShr(setVal,
        B.CreateZExt(bit2, Sets.setTy()), "forin.shr");
    auto* isMem = B.CreateTrunc(shifted, I1Ty, "forin.ismem");
    B.CreateCondBr(isMem, bodyBB, incBB);

    // Body: store ordinal into loop variable and run the body.  The counter
    // walks bit positions, so a set based below zero needs its base added back
    // to turn the position into the ordinal the program declared.
    B.SetInsertPoint(bodyBB);
    llvm::Value* bit3 = B.CreateLoad(I64Ty, bitAlloca, "forin.bit3");
    if (const int64_t base = Sets.setBaseOf(*s.SetExpr); base != 0)
        bit3 = B.CreateAdd(bit3, llvm::ConstantInt::get(I64Ty, base, true),
                                 "forin.rebase");
    B.CreateStore(B.CreateZExtOrTrunc(bit3, loopVarTy, "forin.ord"), loopVar);
    // TP-only: same reasoning as emitFor's own -- the increment-equivalent
    // block (incBB) is the continue-target, not condBB, so a Continue
    // actually advances the bit counter instead of re-testing membership at
    // the same position forever.
    PushLoopTargets(incBB, endBB);
    EmitStmt(s.Body.get());
    PopLoopTargets();
    if (pushedForInScope) SymTab.popScope();
    BrIfNeeded(incBB);

    // Increment bit counter.
    B.SetInsertPoint(incBB);
    auto* bit4 = B.CreateLoad(I64Ty, bitAlloca, "forin.bit4");
    auto* next = B.CreateAdd(bit4, one, "forin.next");
    B.CreateStore(next, bitAlloca);
    B.CreateBr(condBB);

    B.SetInsertPoint(endBB);
}

void CGControlFlow::emitRepeat(const RepeatStmt& s) {
    auto* bodyBB = llvm::BasicBlock::Create(Ctx, "repeat.body", CurFn);
    auto* condBB = llvm::BasicBlock::Create(Ctx, "repeat.cond", CurFn);
    auto* endBB  = llvm::BasicBlock::Create(Ctx, "repeat.end",  CurFn);

    B.CreateBr(bodyBB);

    B.SetInsertPoint(bodyBB);
    // TP-only: `repeat`'s continue-target is ALSO its condition-test block,
    // same as `while` -- the until-test is genuinely re-evaluated on a
    // Continue, unlike some other Pascals' `repeat`, because that test is
    // the loop's only exit and skipping it would make Continue loop forever
    // rather than ever re-checking whether to stop.
    PushLoopTargets(condBB, endBB);
    for (const auto& st : s.Stmts) {
        if (IsTerminated()) ResumeAfterTerminator();
        EmitStmt(st.get());
    }
    PopLoopTargets();
    BrIfNeeded(condBB);

    B.SetInsertPoint(condBB);
    // Re-evaluated per pass; see emitWhile.
    llvm::Value* cond = WithStackScope([&]{ return EnsureI1(EmitExpr(*s.Cond)); });
    // Branching to endBB here instead would turn the loop into a single
    // unconditional pass through the body.
    if (!cond) codegenICE("'repeat' with an unlowerable termination condition");
    B.CreateCondBr(cond, endBB, bodyBB);

    B.SetInsertPoint(endBB);
}

// case selector of const-list: stmt ; ... end
// Uses an if-else chain to support both point and lo..hi range labels (EP).
void CGControlFlow::emitCase(const CaseStmt& s) {
    // Re-evaluated on every pass through an enclosing loop; see emitIf.
    llvm::Value* sel = WithStackScope([&]{ return ToI64(EmitExpr(*s.Selector)); });
    auto* endBB = llvm::BasicBlock::Create(Ctx, "case.end", CurFn);

    // Chain: for each arm build a test block and a body block.
    // nextBB starts as a newly-created block so each arm can branch to the next test.
    auto* nextBB = llvm::BasicBlock::Create(Ctx, "case.test", CurFn);
    B.CreateBr(nextBB);

    for (const auto& arm : s.Arms) {
        B.SetInsertPoint(nextBB);
        nextBB = llvm::BasicBlock::Create(Ctx, "case.test", CurFn);
        auto* armBB = llvm::BasicBlock::Create(Ctx, "case.arm", CurFn);

        // Build condition: OR of all labels in this arm.
        llvm::Value* cond = llvm::ConstantInt::getFalse(Ctx);
        for (const auto& lbl : arm.Labels) {
            llvm::Value* match;
            if (lbl.High) {
                // Range lo..hi: sel >= lo && sel <= hi
                auto* lo  = ToI64(EmitExpr(*lbl.Low));
                auto* hi  = ToI64(EmitExpr(*lbl.High));
                auto* geq = B.CreateICmpSGE(sel, lo, "rlo");
                auto* leq = B.CreateICmpSLE(sel, hi, "rhi");
                match     = B.CreateAnd(geq, leq, "range");
            } else {
                auto* val = ToI64(EmitExpr(*lbl.Low));
                match     = B.CreateICmpEQ(sel, val, "eq");
            }
            cond = B.CreateOr(cond, match, "lbl");
        }
        B.CreateCondBr(cond, armBB, nextBB);

        B.SetInsertPoint(armBB);
        EmitStmt(arm.Body.get());
        BrIfNeeded(endBB);
    }

    // else / otherwise / default branch
    B.SetInsertPoint(nextBB);
    if (s.HasElse) {
        if (s.Else) EmitStmt(s.Else.get());
    } else if (Opts.turbo()) {
        // Real Turbo Pascal's case is not exhaustive-or-die the way ISO
        // 7185/Extended Pascal's is: an unmatched selector with no else/
        // otherwise part just does nothing here and carries on after the
        // case-statement.  Nothing to emit -- nextBB is left empty, and the
        // BrIfNeeded(endBB) below routes it to the same after-block every
        // arm's own body already reaches, exactly the way it would if this
        // were an ordinary matched arm with an empty body.
    } else {
        // ISO §6.8.3.5: reaching here means no label matched, which is an
        // error rather than a silent no-op.
        B.CreateCall(
            RtFns.getExternFnN("plang_err_no_case", llvm::Type::getVoidTy(Ctx), {I64Ty}),
            {sel});
        B.CreateUnreachable();
    }
    BrIfNeeded(endBB);

    B.SetInsertPoint(endBB);
}
