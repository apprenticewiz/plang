(*
new() puts the discriminants in a header and the body behind it.  The
header's size was spelled out as `discs * 8` in two places -- the code
that WRITES it and the code that SKIPS it -- and eight is the alignment
of the header, not of the body.  A body wanting sixteen sat misaligned,
and the aligned vector stores llvm emits from -O1 upward faulted on it:
correct at -O0 and a segmentation fault at every level above.

Both places ask one function now.  The optimisation levels are the test:
this is invisible at -O0, which is where a suite that compiles at one
level would have looked.
*)

(*
RUN: %plang_ep -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang_ep -O1 %s -o %t.O1
RUN: %run %t.O1 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang_ep -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang_ep -O3 %s -o %t.O3
RUN: %run %t.O3 | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: true
*)

program p(output);
type t(n: integer) = array[1..n] of set of char;
var q: ^t; i: integer;
begin new(q, 4);
  for i := 1 to 4 do q^[i] := ['a'..'c'];
  writeln(('b' in q^[4]):5) end.
