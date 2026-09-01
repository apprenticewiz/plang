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
own call site, at 700/900 terms. THIS file uses only 100 terms, not
700/900, for a reason specific to this shape: refsPendingEnum's `const`
declaration is constant-folded by Sema itself, so it never reaches
CodeGen at all, whereas THIS shape -- a call-statement argument -- is not
constant-folded and reaches CodeGen as a full N-deep nested BinaryExpr
tree that CodeGen's own emitExpr/emitBinary mutual recursion (CGExprCore.h/
.cpp) has to walk. CodeGen has its own, separately-calibrated,
already-independently-reviewed recursion ceiling for exactly that walk
(CGExprCore::MaxExprDepth) which -- under this project's own ASan+UBSan CI
build specifically -- is deliberately set to 200, not 1000-ish, because
ASan's own inflated per-frame stack cost drops CodeGen's *real* crash
floor to as low as ~380 terms for a flat chain (see CGExprCore.h's own
comment, issue #146). A first attempt at this file used 900 terms,
copying the sibling test's own number without noticing that number is
only safe for a shape Sema folds away before CodeGen ever sees it -- 900
exceeds CodeGen's ASan-specific 200-term ceiling by a wide margin, so it
failed this project's own live "asan + ubsan" CI job with a clean
codegenICE abort (not a crash: `LLVM ERROR: plang codegen: expression
nesting exceeds CodeGen's depth ceiling...`), even though it compiled
cleanly everywhere else in the CI matrix (non-ASan Debug/Release on every
platform tested handle 900 terms in this same shape fine -- confirmed
directly against this exact file).

100 terms was chosen empirically, not guessed: measured directly against
an ASan+UBSan build (`-DPLANG_SANITIZE=address,undefined`, Debug, matching
this project's own CI job exactly) using this same for-loop-body
call-argument shape, the boundary is an exact, deterministic 200/201
terms (CGExprCore::MaxExprDepth's own value -- a pure activation-count
ceiling, not a live-stack-headroom trip, so this boundary does not vary
by machine or environment the way a stack-headroom-based crash floor
would). 100 terms leaves a full 2x margin below that ceiling -- comfortably
clear of it under any build configuration -- while still being large
enough to meaningfully exercise checkForBody's own walkExprs scan (and
nowhere near a term count a legitimate, non-adversarial Pascal program
would ever contain in a single expression, so this margin is not a
meaningful loss of real coverage). 2**2**...**2 (100 terms, right-
associative power tower) still overflows Real by a wide margin, so the
loop still prints "inf" -- an ordinary, deterministic Real result, not a
sign anything went wrong.

(The stack-exhaustion crash itself needs a constrained ulimit to
reproduce, which lit's own internal RUN-line shell cannot script
portably -- see test/Turbo/ioresult-matrix-real-filesystem-driven-error-
codes.pas's own comment on why a ulimit-bounded RUN line has no precedent
in this suite; verified manually instead, both before and after the
walkExprs/walkStmts fix, and recorded in that fix's own PR. This file's
own job is narrower and does not need a ulimit: it only needs to show the
new stackNearlyExhausted check added to walkExprs/walkStmts does not
reject anything the old term count alone would have accepted, at a term
count actually reachable in this shape on every build configuration this
project tests, including ASan.)
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
    writeln(2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2**2);
end.
