// SemaFlow.cpp — definite-assignment analysis.
//
// ISO 7185 §6.5.1 says a variable has no value until one is assigned to it,
// and §5.1 f) 1) lets a processor leave the reading of such a variable
// unreported provided it says so.  plang says so, in docs/conformance.md: the
// value is whatever the storage held.  What follows narrows that admission by
// reporting the cases a walk over the block can be sure of.
//
// Two other rules fall out of the same walk, because both are about a variable
// having no value at a particular point rather than at any point:
//
//   §6.8.3.9  after a for-statement its control variable is undefined again.
//   §6.6.2    the value of a function is the last assigned to its result, so a
//             path assigning nothing leaves the result whatever storage held.
//
// WHAT THE WALK WILL NOT DO
// -------------------------
// It reports a read only where no path reaching it assigns the variable, so
// branches merge by intersection and a loop body is entered knowing only what
// held before the loop.  Where the flow stops being followable the whole block
// is abandoned: a label can be reached by any goto that names it, and after
// that the walk would be guessing.  Likewise a variable a nested procedure can
// reach is dropped, since the assignment may be in there.
//
// The bias is deliberate and one-way.  Missing a warning costs a warning; a
// warning about correct code costs the reader's trust in all the others.

#include "plang/Sema/Sema.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/SemaUtil.h"
#include "plang/Basic/StringUtil.h"

#include "llvm/Support/Casting.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <string>

using namespace plang;

namespace {

/// The variable an access is ultimately of: `a[i].f^` is an access of `a`.
/// Returns null when the access is not rooted in a plain identifier.
const IdentExpr* rootVariable(const ExprNode* E) {
    while (E) {
        if (auto* N = llvm::dyn_cast<IdentExpr>(E))     return N;
        if (auto* N = llvm::dyn_cast<IndexExpr>(E))     { E = N->Array.get();   continue; }
        if (auto* N = llvm::dyn_cast<FieldExpr>(E))     { E = N->Record.get();  continue; }
        if (auto* N = llvm::dyn_cast<DerefExpr>(E))     { E = N->Pointer.get(); continue; }
        if (auto* N = llvm::dyn_cast<SubstringExpr>(E)) { E = N->Str.get();     continue; }
        // Turbo VARIABLE typecast: TByteRec(SomeWord).Lo := 0 reinterprets
        // SomeWord's own storage, so SomeWord is the variable this access is
        // ultimately of -- same simplification as IndexExpr/FieldExpr just
        // above, one component (or the whole reinterpreted value) standing
        // for the whole variable.
        if (auto* N = llvm::dyn_cast<TypeCastExpr>(E))  { E = N->Operand.get(); continue; }
        return nullptr;
    }
    return nullptr;
}

/// Whether reaching this access's root passes through a pointer dereference.
/// `p^`, `p^.f`, and `p^[i]` all write to whatever `p` points at, which is not
/// storage this walk tracks -- unlike `a[i]` or `r.f`, which write to a
/// component of `a` or `r`'s own storage and so, by the same simplification
/// rootVariable makes, give the whole of `a` or `r` a value.  `p` itself is
/// only read here, to be followed; it is not given a value by this access.
bool writesThroughDeref(const ExprNode* E) {
    while (E) {
        if (llvm::isa<DerefExpr>(E))                    return true;
        if (auto* N = llvm::dyn_cast<IndexExpr>(E))     { E = N->Array.get();  continue; }
        if (auto* N = llvm::dyn_cast<FieldExpr>(E))     { E = N->Record.get(); continue; }
        if (auto* N = llvm::dyn_cast<SubstringExpr>(E)) { E = N->Str.get();    continue; }
        // See rootVariable's identical case just above: a variable typecast
        // is transparent to this walk the same way a field/index access is,
        // so a deref further down its operand (PByteRec(SomePointer)^.Lo :=
        // 0) is still found.
        if (auto* N = llvm::dyn_cast<TypeCastExpr>(E))  { E = N->Operand.get(); continue; }
        return false;
    }
    return false;
}

// See NumSemaTypeKinds in Sema/Type.h.  A new scalar kind defaults to "not a
// simple value", which does not report anything: it silently drops that type
// out of the definite-assignment walk, so a variable of it used before it is
// given a value is no longer diagnosed.
static_assert(NumSemaTypeKinds == 23,
              "a new scalar type kind needs a case in isSimpleValue");

/// Whether a value of this type is one the walk can follow.  A structured
/// variable is given its value a component at a time, so "has it been
/// assigned" has no single answer for one, and a file is opened rather than
/// assigned.  Restricting the walk to the types that hold one value keeps it
/// from reporting a record that was filled in field by field.
bool isSimpleValue(const Type* T) {
    if (!T) return false;
    switch (T->Kind) {
    case TypeKind::Integer: case TypeKind::Real:    case TypeKind::Boolean:
    case TypeKind::Char:    case TypeKind::Enum:    case TypeKind::Subrange:
    case TypeKind::Pointer: case TypeKind::Complex:
        return true;
    default:
        return false;
    }
}

/// Whether the flow through this block can be followed at all.  A goto may
/// land on any label in scope, so a block using them is one where "the paths
/// reaching this statement" is not a question the walk can answer.
bool flowIsFollowable(const BlockNode& Block) {
    if (!Block.Labels.empty()) return false;
    bool Ok = true;
    walkStmts(Block.Body.get(), [&](const StmtNode* S) {
        if (llvm::isa<GotoStmt>(S) || llvm::isa<LabeledStmt>(S)) Ok = false;
    });
    return Ok;
}

/// Every identifier named anywhere inside these procedures, including the ones
/// nested within them.  A variable of the enclosing block that appears in here
/// may be assigned out of the walk's sight, so it cannot be tracked.
void collectNamesUsedIn(const std::vector<std::unique_ptr<ProcDecl>>& Procs,
                        std::set<std::string>& Out) {
    for (const auto& P : Procs) {
        if (!P->Body) continue;
        walkStmts(P->Body->Body.get(), [&](const StmtNode* S) {
            if (auto* C = llvm::dyn_cast<CallStmt>(S)) Out.insert(toLower(C->Name));
            forEachStmtExpr(S, [&](const ExprNode* E) {
                walkExprs(E, [&](const ExprNode* X) {
                    if (auto* Id = llvm::dyn_cast<IdentExpr>(X))
                        Out.insert(toLower(Id->Name));
                });
            });
        });
        collectNamesUsedIn(P->Body->Procs, Out);
    }
}

/// Whether this type-denoter, or one it names, carries a 'value' clause
/// (EP §6.4.1) — i.e. whether a variable declared with it already has a
/// value before the walk sees a single statement.  `type t = integer value
/// 5; var x: t;` puts InitialState on the TypeNode for `t`'s own definition,
/// not on the NamedTypeNode `x`'s declaration resolves to, so the answer has
/// to follow Sema's own Denotes chain to find it — the same chain
/// CodeGen::Impl::writtenInitialState follows for the identical reason
/// (denoterOf's old flat, spelling-keyed alternative re-bound every hop in
/// whatever procedure was being lowered).
bool hasWrittenInitialState(const TypeNode* TN) {
    for (int Hops = 0; TN && Hops < 32; ++Hops) {
        if (TN->InitialState) return true;
        auto* Named = llvm::dyn_cast<NamedTypeNode>(TN);
        if (!Named || !Named->Denotes || Named->Denotes == TN) break;
        TN = Named->Denotes;
    }
    return false;
}

/// The variables assigned on both of two paths — what is still known once they
/// meet.  Everything else may or may not have a value, which is the same as
/// not knowing that it has one.
std::set<std::string> intersect(const std::set<std::string>& A,
                                const std::set<std::string>& B) {
    std::set<std::string> Out;
    std::ranges::set_intersection(A, B, std::inserter(Out, Out.end()));
    return Out;
}

/// The union, used for the two sets where not knowing is the safe answer:
/// a variable left undefined on either path is one the walk must warn about.
std::set<std::string> unite(const std::set<std::string>& A,
                            const std::set<std::string>& B) {
    std::set<std::string> Out = A;
    Out.insert(B.begin(), B.end());
    return Out;
}

} // namespace

void Sema::FlowState::mergeWith(const FlowState& Other) {
    // A dead side was never actually going to reach this join, so it must
    // not get a vote: adopting the live side whole, rather than folding the
    // dead side's Assigned/UndefAfterFor/ResultAssigned into the ordinary
    // merge below, is what keeps `if C then begin F := 1; Exit end; F := 2`
    // from being told F might not always be set -- the branch that does not
    // set it also never survives to the statement after the if.
    if (Dead && !Other.Dead) { *this = Other; return; }
    if (Other.Dead && !Dead) { return; }
    Assigned       = intersect(Assigned, Other.Assigned);
    UndefAfterFor  = unite(UndefAfterFor, Other.UndefAfterFor);
    ResultAssigned = ResultAssigned && Other.ResultAssigned;
    Dead           = Dead && Other.Dead;
}

// ---------------------------------------------------------------------------
// Reads and writes
// ---------------------------------------------------------------------------

void Sema::flowRead(const ExprNode* E, FlowState& St) {
    if (!E) return;
    walkExprs(E, [&](const ExprNode* X) {
        // An access is a read of the variable it is rooted in.  The subscript
        // of an index and the like are reached by the walk in their own right,
        // so only the root has to be named here.
        const IdentExpr* Id = nullptr;
        if (llvm::isa<IdentExpr>(X))       Id = llvm::cast<IdentExpr>(X);
        else if (llvm::isa<IndexExpr>(X) || llvm::isa<FieldExpr>(X)
              || llvm::isa<DerefExpr>(X)  || llvm::isa<SubstringExpr>(X))
            Id = rootVariable(X);
        if (!Id) return;

        const std::string Key = toLower(Id->Name);
        if (!FlowTracked_.contains(Key))  return;
        if (St.Assigned.contains(Key))    return;
        if (FlowReported_.contains(Key))  return;

        // §6.8.3.9 is a rule of its own and reads as one: the variable did
        // have a value, and the for-statement ending is what took it away.
        // Saying only "never given a value" would be wrong as well as unhelpful.
        if (St.UndefAfterFor.contains(Key)) {
            warning(Id->Loc, diag::warn_for_var_after_loop, {Id->Name});
        } else {
            warning(Id->Loc, diag::warn_var_uninitialized, {Id->Name});
        }
        FlowReported_.insert(Key);
    });
}

void Sema::flowWrite(const ExprNode* E, FlowState& St) {
    // A write reached through a dereference lands in whatever the pointer
    // points at, not in the pointer's own storage -- `p^ := 0` and a
    // var-parameter pass of `p^` both need p's own value to know where to
    // write, so p is read here (and, same as any other read, diagnosed if
    // nothing has given it a value yet), and neither marks p itself assigned.
    if (writesThroughDeref(E)) { flowRead(E, St); return; }

    const IdentExpr* Id = rootVariable(E);
    if (!Id) return;
    const std::string Key = toLower(Id->Name);

    // Assigning the function's result is what §6.6.2 asks about, and the name
    // is not a variable of the block, so it is answered separately.
    if (FlowResultNames_.contains(Key)) { St.ResultAssigned = true; return; }

    if (!FlowTracked_.contains(Key)) return;
    // A component assignment gives the whole variable a value as far as this
    // walk is concerned.  Only simple variables are tracked, so in practice
    // the access is the variable itself.
    St.Assigned.insert(Key);
    St.UndefAfterFor.erase(Key);
}

// ---------------------------------------------------------------------------
// The walk
// ---------------------------------------------------------------------------

void Sema::flowSeq(const std::vector<std::unique_ptr<StmtNode>>& Stmts,
                   FlowState& St) {
    for (const auto& S : Stmts) flowStmt(S.get(), St);
}

void Sema::flowStmt(const StmtNode* S, FlowState& St) {
    if (!S) return;

    if (auto* N = llvm::dyn_cast<CompoundStmt>(S)) {
        flowSeq(N->Stmts, St);
        return;
    }

    if (auto* N = llvm::dyn_cast<AssignStmt>(S)) {
        // The value is read first, and so is anything the target is subscripted
        // or dereferenced by — `a[i] := 0` reads i.  What the target does not
        // do is read the variable it names.
        flowRead(N->Value.get(), St);
        if (!llvm::isa<IdentExpr>(N->Target.get()))
            if (const IdentExpr* Root = rootVariable(N->Target.get())) {
                walkExprs(N->Target.get(), [&](const ExprNode* X) {
                    if (X != Root && !llvm::isa<IdentExpr>(X)) return;
                    if (X == Root) return;
                    flowRead(X, St);
                });
                // A dereference reads the variable it is of: p^ := 0 needs p
                // to point somewhere. An index, a substring, or a Turbo
                // variable typecast do not -- a[i] := 0 needs only i (already
                // read by the walkExprs loop above), not a's own value, and
                // TByteRec(SomeWord).Lo := 0 needs no value from SomeWord
                // either: both write into the root's storage directly rather
                // than following a value out of it to find where to write.
                // writesThroughDeref is what already tells these apart for
                // flowWrite just below; asking it here too, instead of
                // reading the root unconditionally, is what keeps
                // TByteRec(SomeWord).Lo := 0 from reading an uninitialized
                // SomeWord that this statement is precisely what gives a
                // value.
                if (writesThroughDeref(N->Target.get()))
                    flowRead(Root, St);
            }
        flowWrite(N->Target.get(), St);
        return;
    }

    if (auto* N = llvm::dyn_cast<CallStmt>(S)) {
        // A var parameter is where the callee puts its answer, so an argument
        // passed to one is written rather than read.
        const std::string Lo = toLower(N->Name);
        const Symbol* Callee = Symtab.lookup(N->Name);
        const bool IsBuiltin = Callee && Callee->Kind == SymbolKind::Builtin;

        // TP's exit(value) (Builtins.def) is FPC's shorthand for "assign
        // value to the function's result, then leave" -- checkCallStmt's own
        // arm (SemaStmt.cpp) already treats it that way for err_assign_mismatch
        // and for FuncStack's HasResult (the "assigned somewhere at all" check
        // behind err_function_no_result), but this flow-sensitive walk is a
        // separate mechanism (behind warn_result_not_always_set) that arm does
        // not feed.  Handled up front, ahead of the builtinAssigns loop below:
        // value is a VALUE to read, not a variable the callee writes into, so
        // it does not fit that loop's byref/read shape -- read as one left it
        // an ordinary expression with no effect on ResultAssigned, so
        // `if C then Exit(1); Result := 2` looked like only one path (the
        // explicit assignment) ever set the result.
        if (IsBuiltin && Callee->BuiltinKind == BuiltinID::Exit && !N->Args.empty()) {
            flowRead(N->Args[0].get(), St);
            St.ResultAssigned = true;
            St.Dead = true;
            return;
        }

        // The standard procedures are declared without parameter lists, since
        // most of them are variadic, so the ones that give a variable a value
        // have to be named.  read and readln do it to everything they are
        // given — the file they may start with is not a variable this walk
        // tracks — and new does it to the pointer.  The rest only look.
        auto builtinAssigns = [&](size_t I) {
            if (Lo == "read" || Lo == "readln") return true;
            if (Lo == "new")                    return I == 0;
            // EP §6.7.5.5: readstr(e, v1,...,vn) reads e and assigns every
            // v -- the mirror of read/readln, except its first argument is
            // the SOURCE rather than the first destination.  Missing here,
            // v1..vn fell to the "only looks" default and warned "read
            // before given a value" on names this statement itself assigns.
            if (Lo == "readstr")                return I >= 1;
            // TP-only: the System-unit string routines that write through a
            // var parameter -- missing here, each fell to the "only looks"
            // default and warned "read before given a value" on a variable
            // the call itself assigns (the same gap readstr's own comment,
            // just above, already describes for EP).  Delete/SetLength's var
            // string is argument 0; Insert's is argument 1 (source, its
            // OTHER string argument, is read, not written); Str's
            // destination is argument 1; Val's v and code are arguments 1
            // and 2 (its source string, argument 0, is read).
            if (Lo == "delete" || Lo == "setlength") return I == 0;
            if (Lo == "insert" || Lo == "str")       return I == 1;
            if (Lo == "val")                         return I == 1 || I == 2;
            return false;
        };

        for (size_t I = 0; I < N->Args.size(); ++I) {
            bool ByRef;
            if (IsBuiltin)                                ByRef = builtinAssigns(I);
            else if (Callee && I < Callee->Params.size()) ByRef = Callee->Params[I].IsVar;
            // A signature the walk cannot read is assumed to assign, which
            // costs a warning rather than inventing one.
            else                                          ByRef = true;
            if (ByRef) flowWrite(N->Args[I].get(), St);
            else       flowRead (N->Args[I].get(), St);
        }

        // Halt/RunError end the program, plain Exit ends the function or
        // procedure (a value-carrying Exit already returned above), and
        // Break/Continue end the loop iteration -- see BuiltinID's own
        // comment for why this one call answers for all five.
        if (IsBuiltin && builtinAlwaysTransfers(Callee->BuiltinKind)) {
            St.Dead = true;
            switch (Callee->BuiltinKind) {
            case BuiltinID::Break:
                // Marks the innermost loop currently being walked (see
                // FlowLoopBroke_'s own comment, Sema.h) so ForStmt can tell a
                // path that left this way from one that fell off the body's
                // natural end.
                FlowLoopBroke_ = true;
                break;
            case BuiltinID::Halt:
            case BuiltinID::RunError:
            case BuiltinID::Exit:
                // Unlike Break/Continue, these end the function/procedure (or
                // the program) right here -- see FlowResultMaybeUnset_'s own
                // comment, Sema.h, for why that is checked at this point
                // rather than only where mergeWith folds (or drops) this
                // path into a later one.
                if (!FlowResultNames_.empty() && !St.ResultAssigned)
                    FlowResultMaybeUnset_ = true;
                break;
            default:
                break;
            }
        }
        return;
    }

    if (auto* N = llvm::dyn_cast<IfStmt>(S)) {
        flowRead(N->Cond.get(), St);
        FlowState Then = St;
        flowStmt(N->Then.get(), Then);
        // Without an else the other path is the implicit empty statement --
        // a copy of St, untouched, which flowStmt(nullptr, ...) leaves that
        // way.  mergeWith already knows what to do with that: if Then turned
        // out dead (it left through Halt/Exit/Break/Continue/RunError before
        // reaching here), the merge adopts this copy of St whole, which is
        // the same "the other path did nothing, so nothing new is known"
        // reasoning this arm used to hand-rewrite as touching UndefAfterFor
        // alone -- that shortcut was only sound because nothing could
        // previously mark a branch dead, so Then's Assigned/ResultAssigned
        // were always safe to leave out of it.  Now that one can be dead,
        // folding through the general merge is what keeps `if C then begin
        // F := 1; Exit end; F := 2` from being told F might not be set: the
        // branch that never sets it also never survives to be asked.
        FlowState Else = St;
        flowStmt(N->Else.get(), Else);
        Then.mergeWith(Else);
        St = std::move(Then);
        return;
    }

    if (auto* N = llvm::dyn_cast<WhileStmt>(S)) {
        flowRead(N->Cond.get(), St);
        // The body is entered with what held before the loop, which is right
        // for the first time round and is the only iteration that can read a
        // variable nothing has assigned yet.  It may run no times, so what it
        // assigns is not known afterwards.
        FlowState Body = St;
        // FlowLoopBroke_ is scoped to the body of the innermost loop being
        // walked (see its own comment, Sema.h) so a Break in here is not
        // mistaken for one in a loop this while is nested inside.  Nothing
        // in this arm reads it back -- while has no control variable for it
        // to answer for -- but every loop arm resets and restores it so the
        // one that does (ForStmt) sees only Breaks that are really its own.
        const bool SavedBroke = FlowLoopBroke_;
        FlowLoopBroke_ = false;
        flowStmt(N->Body.get(), Body);
        FlowLoopBroke_ = SavedBroke;
        St.UndefAfterFor = unite(St.UndefAfterFor, Body.UndefAfterFor);
        return;
    }

    if (auto* N = llvm::dyn_cast<RepeatStmt>(S)) {
        // A repeat runs its body at least once, so what the body assigns is
        // assigned afterwards, and the condition is read after the body.
        // Dead is saved and restored around it regardless: this is the one
        // loop arm that mutates St directly rather than merging a separate
        // Body copy back in, and a Break/Continue in the body ends the
        // repeat-statement itself (the same as it would for any other loop),
        // not whatever encloses it -- without this, St.Dead would come out
        // of a repeat containing an unconditional Break still set, as though
        // the repeat-statement were what left for good.
        const bool SavedDead  = St.Dead;
        const bool SavedBroke = FlowLoopBroke_;
        FlowLoopBroke_ = false;
        flowSeq(N->Stmts, St);
        flowRead(N->Cond.get(), St);
        St.Dead        = SavedDead;
        FlowLoopBroke_ = SavedBroke;
        return;
    }

    if (auto* N = llvm::dyn_cast<ForStmt>(S)) {
        flowRead(N->From.get(), St);
        flowRead(N->Limit.get(), St);
        const std::string Key = toLower(N->Var);

        FlowState Body = St;
        Body.Assigned.insert(Key);          // the control variable has a value inside
        Body.UndefAfterFor.erase(Key);
        const bool SavedBroke = FlowLoopBroke_;
        FlowLoopBroke_ = false;
        flowStmt(N->Body.get(), Body);
        const bool Broke = FlowLoopBroke_;
        FlowLoopBroke_  = SavedBroke;

        // The loop may run no times, so nothing the body assigned is known.
        // §6.8.3.9: and the control variable is undefined once it finishes --
        // by exhausting its range, which is what falling off the body's
        // natural end means here.  TP's Break (FlowLoopBroke_, set by
        // flowStmt's CallStmt arm) is the one way to leave that is not that:
        // it does not exhaust the range, so the control variable keeps
        // whatever value it had, which is exactly the TP idiom `for i := 1
        // to n do if a[i] = x then break; if i <= n then {found at i}`
        // relies on.  A one-way carve-out in the same direction as the rest
        // of this file's bias -- a loop whose only Break is itself
        // unreachable still gets the benefit of the doubt, which costs a
        // missed warning rather than a wrong one.
        St.UndefAfterFor = unite(St.UndefAfterFor, Body.UndefAfterFor);
        if (Broke && FlowTracked_.contains(Key)) {
            // Treated as genuinely assigned, not merely "not known to be
            // undefined" -- flowRead reports EITHER an UndefAfterFor member
            // or (failing that) a plain FlowTracked_ one that Assigned does
            // not contain, so leaving Key out of both here would still get
            // `if i <= n` reported as reading i before it was ever given a
            // value, the same false alarm from the opposite diagnosis.
            St.Assigned.insert(Key);
            St.UndefAfterFor.erase(Key);
        } else {
            St.Assigned.erase(Key);
            if (FlowTracked_.contains(Key)) St.UndefAfterFor.insert(Key);
        }
        return;
    }

    if (auto* N = llvm::dyn_cast<ForInStmt>(S)) {
        flowRead(N->SetExpr.get(), St);
        // EP §6.9.3.9.3: for-in declares its control variable implicitly, for
        // the body only, and the loop gives it a value on every iteration.
        // This arm did not say so, and the flow state is keyed by NAME with no
        // scopes, so every read of the variable in the body was reported as a
        // read of something never given a value -- on the one program shape the
        // feature exists for.
        //
        // The variable is a FRESH one that shadows any outer variable of the
        // same spelling for the body's duration, so unlike an ordinary for-loop
        // its assignment must not survive the loop: whatever the outer one's
        // state was, it is still that afterwards.
        const std::string Key = toLower(N->Var);
        const bool OuterAssigned = St.Assigned.count(Key) != 0;
        const bool OuterUndef    = St.UndefAfterFor.count(Key) != 0;

        FlowState Body = St;
        Body.Assigned.insert(Key);
        Body.UndefAfterFor.erase(Key);
        // See WhileStmt above: for-in has no control-variable question a
        // Break here would answer (its own control variable never survives
        // the loop either way, break or no), but the scope is still reset so
        // one does not leak to an enclosing for-loop's.
        const bool SavedBroke = FlowLoopBroke_;
        FlowLoopBroke_ = false;
        flowStmt(N->Body.get(), Body);
        FlowLoopBroke_ = SavedBroke;

        St.UndefAfterFor = unite(St.UndefAfterFor, Body.UndefAfterFor);
        if (!OuterAssigned) St.Assigned.erase(Key);
        if (!OuterUndef)    St.UndefAfterFor.erase(Key);
        return;
    }

    if (auto* N = llvm::dyn_cast<CaseStmt>(S)) {
        flowRead(N->Selector.get(), St);
        // §6.8.3.5: a selector matching no arm is an error, and plang reports
        // it when the program runs.  So every path that carries on past the
        // case went through an arm, and what all the arms assign is assigned.
        //
        // Turbo is the one exception (checkCase's own Opts.turbo() gate,
        // SemaStmt.cpp, just above the exhaustiveness warning that reasoning
        // feeds; CGControlFlow::emitCase lowers it the same way): with no
        // else/otherwise part, an unmatched selector falls through instead of
        // trapping, so a path that matches nothing DOES survive to whatever
        // follows the case, having gone through none of the arms.  That is
        // exactly what an unmodified copy of the incoming state stands for,
        // so under Turbo it is folded into the merge as one more branch --
        // `case i of 1: x := 1 end; writeln(x)` must still warn that x might
        // not be assigned, since i might not be 1.
        std::optional<FlowState> Merged;
        auto takeBranch = [&](const StmtNode* Body) {
            FlowState Br = St;
            flowStmt(Body, Br);
            if (Merged) Merged->mergeWith(Br);
            else        Merged = std::move(Br);
        };
        for (const auto& Arm : N->Arms) takeBranch(Arm.Body.get());
        if (N->HasElse)         takeBranch(N->Else.get());
        else if (Opts.turbo())  takeBranch(nullptr);
        if (Merged) St = std::move(*Merged);
        return;
    }

    if (auto* N = llvm::dyn_cast<WithStmt>(S)) {
        for (const auto& R : N->Records) flowRead(R.get(), St);
        flowStmt(N->Body.get(), St);
        return;
    }

    // A labeled statement or a goto means flowIsFollowable said no and the
    // walk was never started.  Anything else has no expressions and no body.
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void Sema::checkDefiniteAssignment(const BlockNode& Block) {
    if (!Block.Body) return;
    // A program that does not typecheck is not one whose flow means anything.
    // The walk reads a tree where some names have no type and some statements
    // were kept only to carry on looking for errors, and what it would say
    // about that is guesswork stacked on a mistake the reader already has to
    // fix.
    if (std::ranges::any_of(Diags, [](const Diagnostic& D) {
            return D.Severity == DiagSeverity::Error; }))
        return;
    if (!flowIsFollowable(Block)) return;

    // Anything a nested procedure names might be assigned in there, out of
    // sight of this walk.
    std::set<std::string> Hidden;
    collectNamesUsedIn(Block.Procs, Hidden);

    auto SavedTracked  = std::move(FlowTracked_);
    auto SavedResults  = std::move(FlowResultNames_);
    auto SavedReported = std::move(FlowReported_);
    const bool SavedResultMaybeUnset = FlowResultMaybeUnset_;
    FlowTracked_.clear();
    FlowResultNames_.clear();
    FlowReported_.clear();
    FlowResultMaybeUnset_ = false;

    // EP §6.4.1: a 'value' clause -- on the declaration itself (`var x: T
    // value E;`) or on the type it declares x with (`type t = T value E;`)
    // -- gives x a value before the walk sees a single statement, the same
    // as a parameter or a for-loop's control variable does implicitly.
    // Neither was seeded into the initial FlowState below, so reading such a
    // variable before its first explicit assignment statement was reported
    // as though the declaration had done nothing at all.
    std::set<std::string> PreAssigned;
    for (const auto& Vg : Block.Vars) {
        const bool HasInit = Vg.InitExpr || hasWrittenInitialState(Vg.Type.get());
        for (const auto& Nm : Vg.Names) {
            const std::string Key = toLower(Nm);
            if (Hidden.contains(Key)) continue;
            const Symbol* Sym = Symtab.lookup(Nm);
            // A variable bound to something outside the program (EP §6.4.1)
            // has a value from there, and one the program never took the
            // address of is the only kind the walk can speak for.
            if (!Sym || Sym->Kind != SymbolKind::Var) continue;
            if (Sym->IsBindable) continue;
            // TP-only: 'absolute' (AstDecl.h's VarGroup::AbsoluteExpr) gives
            // this variable's storage to whatever the overlaid variable
            // already holds, the same as a bindable variable's storage comes
            // from outside the program -- this walk cannot see whether THAT
            // storage was assigned, so it does not try, exactly the way it
            // already declines to for a bindable variable just above.
            if (Vg.AbsoluteExpr) continue;
            if (!isSimpleValue(Sym->Ty.get())) continue;
            FlowTracked_.insert(Key);
            if (HasInit) PreAssigned.insert(Key);
        }
    }

    // §6.6.2 applies to the body of a function, which is the block owned by
    // one.  The result answers to the function's own identifier, and in
    // Extended Pascal to the name the heading may have given it as well.
    const ProcDecl* Owner = CurrentProc;
    bool IsFunc = Owner && Owner->heading().IsFunction
               && Owner->Body.get() == &Block;
    if (IsFunc) {
        FlowResultNames_.insert(toLower(Owner->Name));
        if (!Owner->heading().ResultName.empty())
            FlowResultNames_.insert(toLower(Owner->heading().ResultName));
        // §6.8.2.2 lets a function nested inside this one assign the result,
        // and the walk does not go in there.  Whether every path assigns it is
        // then not a question this walk can answer; that there is an
        // assignment at all is still checked, on the frame, where it works.
        for (const auto& Nm : FlowResultNames_)
            if (Hidden.contains(Nm)) { IsFunc = false; break; }
        if (!IsFunc) FlowResultNames_.clear();
    }

    if (!FlowTracked_.empty() || IsFunc) {
        FlowState St;
        St.Assigned = std::move(PreAssigned);
        flowStmt(Block.Body.get(), St);

        // A function that assigns its result nowhere at all is already an
        // error, reported against the declaration; saying it again here as a
        // warning would be two messages for one mistake.  FlowResultMaybeUnset_
        // covers a path this final St cannot speak for: one that left through
        // a bare Halt/Exit/RunError with the result still unassigned, which
        // mergeWith may since have dropped from every later join (see its own
        // comment) -- St.ResultAssigned answers for the paths that reached
        // the end of the block, not for one that never did.
        if (IsFunc && (!St.ResultAssigned || FlowResultMaybeUnset_)
            && !FuncStack.empty() && FuncStack.back().HasResult)
            warning(Owner->Loc, diag::warn_result_not_always_set, {Owner->Name});
    }

    FlowTracked_          = std::move(SavedTracked);
    FlowResultNames_      = std::move(SavedResults);
    FlowReported_         = std::move(SavedReported);
    FlowResultMaybeUnset_ = SavedResultMaybeUnset;
}
