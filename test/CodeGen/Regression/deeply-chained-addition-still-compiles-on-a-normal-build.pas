(*
Issue #146's companion check: the exact same 300-term flat `1+1+...+1` chain
as deeply-chained-addition-does-not-crash-codegen-under-asan.pas next to
this file, but run on a normal (non-sanitizer) build, where CodeGen's own
new defense-in-depth MaxExprDepth (CGExprCore.h) is set far above 1000 --
comfortably above anything Sema::checkExpr's own 1000-term cap could ever
hand it -- specifically so a perfectly ordinary, Sema-accepted expression
like this one still just compiles and runs, rather than picking up a new,
artificial rejection from a guard that exists only to catch a sanitizer
build's much lower real stack-overflow threshold.

UNSUPPORTED here (rather than "always run, expect success"): on an
asan-build, CodeGen's ceiling for this exact input is deliberately set
BELOW 300 (see the sibling test), so this file's own RUN line would fail
there for the same reason the sibling one exists -- the two files are two
halves of one property (build-appropriate ceiling), not overlapping checks
of the same build.
*)

(*
UNSUPPORTED: asan-build
*)

(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK: 300
*)

program p;
var x: integer;
begin
  x := 1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1;
  writeln(x)
end.
