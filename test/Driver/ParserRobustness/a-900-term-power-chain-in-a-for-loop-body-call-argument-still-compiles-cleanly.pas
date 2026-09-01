(*
Issue #556's own reopening: a for-loop body's call-statement arguments are
scanned by checkForBody (SemaStmt.cpp, issue #265/#291's own var-parameter
threat detection) BEFORE checkStmt/checkExpr ever run on the same body --
see checkForBody's own call site (SemaStmt.cpp's ForStmt handling):
`checkForBody(S.Body.get(), S.Var, S.Loc); ...; checkStmt(S.Body.get());`.
That scan walks every CallExpr hanging off the body's statements through
walkExprs (Basic/SemaUtil.h), which is the SAME shared-utility walk
Sema::checkBlock's refsPendingEnum (a plain `const x = ...;` declaration)
also drives, and is the second, independent call site an adversarial
review of PR #558 (issue #556's own fix) found still segfaulting under a
constrained stack (`ulimit -s 256`, Debug, 700 terms) purely because
walkExprs/walkStmts had no live-stack-headroom check of their own yet --
only the pre-existing MaxWalkDepth=1000 term-count ceiling, the same gap
checkExpr/constBound/buildExtentForm already had before issue #556's own
fix. Fixed by threading Sema::StackBaseline through walkExprs/walkStmts
the same way checkExpr already does (see SemaUtil.h's own comment).

This file is the false-positive half of that fix's regression coverage --
its sibling a-700-term-and-a-900-term-addition-chain-both-still-compile-
cleanly.pas (this directory) is the identical check for refsPendingEnum's
own call site. 900 terms sits safely under Sema's own MaxExprDepth/
MaxWalkDepth=1000 ceiling, so this must compile and run cleanly at the
default (unconstrained, ~8MiB) stack exactly as it did before this fix --
proving the new stackNearlyExhausted check added alongside the existing
term count does not reject anything the term count alone would have
accepted. (The stack-exhaustion crash itself needs a constrained ulimit to
reproduce, which lit's own internal RUN-line shell cannot script portably
-- see test/Turbo/ioresult-matrix-real-filesystem-driven-error-codes.pas's
own comment on why a ulimit-bounded RUN line has no precedent in this
suite; verified manually instead, both before and after this fix, and
recorded in this fix's own PR.) 2**2**...**2 (900 terms) overflows Real,
so the loop prints "inf" -- an ordinary, deterministic Real result, not a
sign anything went wrong.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:inf
CHECK-NEXT:inf
CHECK-NEXT:inf
*)

program p;
var i: integer;
begin
  for i := 1 to 3 do
    writeln(2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2);
end.
