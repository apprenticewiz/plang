(*
Issue #588: 'for x in setExpr do ... ord(x) ...' used to crash the
COMPILER (LLVM verifier failure: "zext too small", not a diagnostic) when
the loop's control-variable name already denoted, in an enclosing scope, a
variable of SET type (as opposed to the set's ELEMENT type) --
CGControlFlow::emitForIn's reuse guard for issue #217 (a control variable
declared at a different ordinal WIDTH than the set's element type) tested
only `ve->type->isIntegerTy()`, which is also true of a Set's own wide
bitmask storage, so it wrongly reused a Set variable's own far-wider
storage as the loop variable's.  checkForIn (SemaStmt.cpp, issue #689)
now separately refuses this earlier, as a clean diagnostic, matching this
project's own established contract that a loop control variable must be
assign-compatible with the set's ELEMENT type -- a Set is never
assign-compatible with its own element type -- so this never reaches
CodeGen's reuse guard at all now.  Kept as its own test (rather than
folding into the pre-existing, more general
the-control-variables-type-must-agree-with-the-set-element-type.pas) since
this exact shape -- the control variable's name colliding with an outer
SET-typed variable specifically -- is what actually crashed the compiler.

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: loop variable 'c' has type 'set of char', incompatible with set element type 'char'
*)

program p(output);
var c: set of char; other: set of char;
begin
  other := ['x', 'y'];
  for c in other do
    writeln(ord(c))
end.
