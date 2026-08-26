(*
Issue #13: parseFactor's LeftParen case recurses through parseExpression ->
parseSimpleExpr -> parseTerm -> parsePower -> parseFactor with no depth
limit, so before this test existed, deeply nested parens exhausted the real
C++ stack instead of failing cleanly.  1000 levels is well past the
500-deep ceiling (MaxExprDepth, ParseExpr.cpp) while staying fast to parse
here; the actual crash threshold (~20,000) is exercised end-to-end, under
the real compiler's real stack, by
test-lit/Driver/ParserRobustness/deeply-nested-parentheses-do-not-crash-the-compiler.pas.

Checked in verbatim rather than generated at build time, matching that
file's own precedent -- lit has no loop-emitting primitive, and the source
is deterministic; nobody should have to look at 1000 parens to read this
file, so the lit directives come first.

The depth-limit diagnostic fires first, and the diagnostic count stays
small and independent of nesting depth: ExprDepthLimitHit suppresses the
"expected )" cascade that unwinding 500 stacked '(' frames would
otherwise produce on the way out -- one per frame, i.e. ~500 of them
without the suppression, not the handful seen here.  What is left is the
ordinary, bounded fallout of one expression failing to consume its input:
the enclosing compound-statement, program, and end-of-file check each
report once that they did not see what they expected either.

RUN: not %plang_ir -dump-parse-tree %s 2> %t.err
RUN: FileCheck %s < %t.err
RUN: grep -c "error:" %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

program p; var x : integer; begin x := ((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((1)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))) end.

(*
CHECK: error: expression is nested too deeply
COUNT:4
*)
