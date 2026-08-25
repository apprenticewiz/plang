(*
EP section 6.4.8.  A schema instantiated inside another schema's BODY has
discriminants that are arithmetic over the enclosing ones.  Sema folds
the body against a probe binding of 1, so `vector(n)` inside
`matrix(m,n)` came out `vector(1)` -- and that probe answer was taken for
the layout, so the allocation was one element wide in every instance and
the writes ran off the end of it.

This is the canonical example from the standard itself, which is the
strongest argument for it not being an edge case.
*)

(*
RUN: %plang_ep -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 11.0 12.0 13.0 14.0
CHECK-NEXT: 21.0 22.0 23.0 24.0
CHECK-NEXT: 31.0 32.0 33.0 34.0
*)

program p(output);
type vector(n: integer) = array[1..n] of real;
     matrix(m, n: integer) = array[1..m] of vector(n);
var q: ^matrix; i, j: integer;
begin new(q, 3, 4);
  for i := 1 to 3 do
    for j := 1 to 4 do q^[i][j] := i * 10 + j;
  for i := 1 to 3 do begin
    for j := 1 to 4 do write(q^[i][j]:5:1);
    writeln end end.
