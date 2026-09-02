(*
Issue #572: Parser::parseFactor's own recursion ceiling (ExprDepth/
MaxExprDepth = 500, ParseExpr.cpp) was a pure term-count ceiling with no
plang::stackNearlyExhausted check at all -- unlike Parser::parsePower
(issues #550/#551, PR #555), Parser::parseStatement (issue #562), and
Sema's checkExpr/constBound/buildExtentForm/walkExprs/walkStmts and
CodeGen's emitBinary/emitExpr (issue #556 and its own follow-up, PR #558).
499 levels of nested '(' (safely under MaxExprDepth=500) reliably
segfaulted the frontend with no diagnostic at all under a constrained
stack (`ulimit -s 352`, Release; larger under Debug) -- the same
architectural gap those issues closed for every other recursive
parser/Sema/CodeGen entry point, in this one further sibling: parseFactor
is the single choke point every recursive re-entry into expression
parsing (a parenthesized sub-expression, 'not'/'@' unary recursion) funnels
through, so this ceiling bounds the whole parseExpression/parseSimpleExpr/
parseTerm/parsePower/parseFactor cycle exactly the way parsePower's own
'**'-chain guard bounds the OTHER recursive edge of that same cycle. Fixed
by adding plang::stackNearlyExhausted(StackBaseline) alongside the
existing ExprDepth check, checked before ExprDepthScope's construction the
same way the pure term-count check already was -- ExprDepthScope is
already constructed unconditionally on every non-limit-hit activation, so
by the time enough nesting has happened for the real stack to run low,
guards from every shallower level are already alive to reset
ExprDepthLimitHit as they unwind, the same guarantee the pre-existing
`ExprDepth >= MaxExprDepth` early-return already relied on.

This file is the false-positive half of that fix's regression coverage:
499 levels of nested '(' around a literal (the issue's own repro depth)
plus the literal itself is exactly 500 live parseFactor activations --
ExprDepth's own ceiling, and the same boundary the OLD term-count-only
check already enforced -- so the new stackNearlyExhausted check added
alongside it must not narrow what already-legitimate, boundary-exact
input this parser accepts on a normal (default, ~8MiB) stack. Deeper
nesting past this boundary is already covered by the existing
deeply-nested-parentheses-do-not-crash-the-compiler.pas sibling test
(~20,000 levels, far past this boundary, on a normal stack -- exercising
the term-count ceiling alone, since a normal stack never gets close to
exhausted at this depth); this file's own job is narrower: prove the
boundary itself did not move.

(The stack-exhaustion crash itself needs a constrained ulimit to
reproduce, which lit's own internal RUN-line shell cannot script
portably -- see test/Turbo/ioresult-matrix-real-filesystem-driven-error-
codes.pas's own comment on why a ulimit-bounded RUN line has no precedent
in this suite, and the a-499-level-nested-if-chain-at-the-stmtdepth-
boundary-still-compiles-cleanly.pas sibling test's own identical
disclaimer for issue #562's analogous guard. Verified manually instead,
both before and after this fix, at `ulimit -s 352`, Release (matching the
issue's own bisected threshold for this exact 499-level input): before,
499 nested '(' segfaults (SIGSEGV, exit 139, no diagnostic); after, it
either compiles cleanly or is rejected with the same "expression is
nested too deeply" diagnostic the term-count ceiling already uses --
never a raw crash -- and recorded in this fix's own PR.)
*)

(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:ok
*)

program p;
var x: integer;
begin
  x := (((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((1)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
  writeln('ok');
end.
