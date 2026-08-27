(*
Issue #203: EP §6.8.7's structured-value-constructor grammar bypassed all
four existing parser depth guards (Parser.h:50-128) -- an arm's value
recurses straight back into parseComponentValue ('value [1:[1: ... :0]]'),
so before this test existed, a deeply nested structured-value arm
exhausted the real C++ stack instead of failing cleanly.  600 levels is
comfortably past the 500-deep ceiling (MaxValueDepth, ParseInit.cpp)
while staying fast to parse here; the actual crash threshold (tens of
thousands) is exercised end-to-end, under the real compiler's real
stack, by
test/Driver/ParserRobustness/deeply-nested-structured-value-does-not-crash-the-compiler.pas.

Checked in verbatim rather than generated at build time, matching the
parenthesized-expression precedent this mirrors -- lit has no
loop-emitting primitive, and the source is deterministic; nobody should
have to look at 600 levels of '[1:' to read this file, so the lit
directives come first.

The depth-limit diagnostic fires first, and the diagnostic count stays
small and independent of nesting depth: ValueDepthLimitHit suppresses
the "expected ']'" cascade that unwinding 600 stacked '[1:' frames would
otherwise produce on the way out, in the same way ExprDepthLimitHit
does for stacked '(' in the sibling test this one mirrors.

RUN: not %plang_ir -std=iso10206 -dump-parse-tree %s 2> %t.err
RUN: FileCheck %s < %t.err
RUN: grep -c "error:" %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
CHECK: error: structured value is nested too deeply
COUNT:6
*)

program p; var v: integer value [1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:[1:0]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]; begin end.
