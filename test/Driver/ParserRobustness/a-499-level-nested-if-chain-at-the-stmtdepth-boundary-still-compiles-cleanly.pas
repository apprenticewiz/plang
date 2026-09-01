(*
Issue #562: Parser::parseStatement's own recursion ceiling (StmtDepth/
MaxStmtDepth = 500, ParseStmt.cpp) was a pure term-count ceiling with no
plang::stackNearlyExhausted check at all -- unlike Parser::parsePower
(issues #550/#551, PR #555) and unlike Sema's checkExpr/constBound/
buildExtentForm/walkExprs/walkStmts and CodeGen's emitBinary/emitExpr
(issue #556 and its own follow-up, PR #558). 499 levels of nested 'if'
(safely under MaxStmtDepth=500) reliably segfaulted the frontend with no
diagnostic at all under a constrained stack (`ulimit -s 256`, Debug) --
the same architectural gap those two issues closed for every other
recursive parser/Sema/CodeGen entry point, in this fifth sibling.  Fixed
by adding plang::stackNearlyExhausted(StackBaseline) alongside the
existing StmtDepth check, constructing StmtDepthScope unconditionally
before either check runs -- the exact shape parsePower's own guard (and
Sema::checkExpr's, restructured for issue #556) already established, for
the exact same "a headroom check can fire on the very first activation,
before any Guard exists to reset the latch" reason (see
plang::RecursionGuard's own comment, Basic/StackHeadroom.h).

This file is the false-positive half of that fix's regression coverage:
499 levels of nested 'if' (the issue's own repro depth) plus the trailing
statement they guard is exactly 500 live parseStatement activations --
StmtDepth's own ceiling, and the same boundary the OLD term-count-only
check already enforced (`StmtDepth >= MaxStmtDepth` before the RAII bump,
restructured here to `StmtDepth > MaxStmtDepth` after it, per DepthGuard's
own comment in ParseStmt.cpp) -- so the new stackNearlyExhausted check
added alongside it must not narrow what already-legitimate, boundary-exact
input this parser accepts on a normal (default, ~8MiB) stack. One level
deeper (500 nested 'if', 501 total activations) is exactly what the
existing deeply-nested-compound-statement-does-not-crash-the-compiler.pas
sibling test (30,000 levels, far past this boundary) already confirms is
rejected cleanly, not crashed on -- this file's own job is narrower: prove
the boundary itself did not move.

(The stack-exhaustion crash itself needs a constrained ulimit to
reproduce, which lit's own internal RUN-line shell cannot script
portably -- see test/Turbo/ioresult-matrix-real-filesystem-driven-error-
codes.pas's own comment on why a ulimit-bounded RUN line has no precedent
in this suite, and the a-100-term-power-chain-in-a-for-loop-body-call-
argument-still-compiles-cleanly.pas sibling test's own identical
disclaimer for issue #556's analogous guards. Verified manually instead,
both before and after this fix, at `ulimit -s 256`, Debug: before, 499
nested 'if' segfaults (SIGSEGV, exit 139, no diagnostic); after, it either
compiles cleanly or is rejected with the same "statement is nested too
deeply" diagnostic this file's sibling test uses -- never a raw crash --
and recorded in this fix's own PR.)
*)

(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:ok
*)

program p;
var b: boolean;
begin
  b := true;
  if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then if b then writeln('ok');
end.
