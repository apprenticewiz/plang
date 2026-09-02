(*
Issue #689: `for c in set do` no longer implicitly declares a FRESH,
loop-scoped shadow of `c` -- it drives the program's own declared `c`, the
same way `for i := 1 to n do` drives its own declared control variable.
ISO §6.8.3.9 leaves a for-loop's control variable undefined once the loop
finishes by exhausting its range (rather than via Break), and
§6.9.3.9.1's `for ... in` is exhausted the identical way, so `c` is still
flagged as not-yet-given-a-value right after the loop -- the same
diagnostic the old (wrong) shadow-based behavior also happened to produce
here, but now for the CORRECT reason (SemaFlow.cpp's ForInStmt arm mirrors
ForStmt's own UndefAfterFor tracking) rather than by treating `c` as a
name that never really existed outside the loop body.

RUN: %plang_ep %s -o %t 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'c' is undefined here: a for-statement leaves its control variable undefined when it finishes
*)

program p(output);
var c: char;
begin for c in ['a'..'c'] do write(c); writeln(c) end.
