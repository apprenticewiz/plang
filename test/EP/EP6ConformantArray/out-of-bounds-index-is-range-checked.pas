(*
RUN: %plang -std=iso10206 %s -o %t.O0
RUN: not %run %t.O0 > %t.O0.out 2> %t.O0.err
RUN: FileCheck --check-prefix=ERR %s < %t.O0.err
RUN: %plang -std=iso10206 -O1 %s -o %t.O1
RUN: not %run %t.O1 > %t.O1.out 2> %t.O1.err
RUN: FileCheck --check-prefix=ERR %s < %t.O1.err
RUN: %plang -std=iso10206 -O2 %s -o %t.O2
RUN: not %run %t.O2 > %t.O2.out 2> %t.O2.err
RUN: FileCheck --check-prefix=ERR %s < %t.O2.err
RUN: %plang -std=iso10206 -O3 %s -o %t.O3
RUN: not %run %t.O3 > %t.O3.out 2> %t.O3.err
RUN: FileCheck --check-prefix=ERR %s < %t.O3.err
*)

(* EP §6.7.3.7: a conformant array parameter's element access has to be
   range-checked the same as every other array-indexing path -- an
   out-of-bounds write here must trap, not silently land past the actual
   argument's allocation. *)

(*
ERR: array index 9 out of bounds 1..5
*)

program p;
procedure fill(var A: array [lo..hi : integer] of integer; i, v: integer);
begin
  A[i] := v
end;
var arr: array [1..5] of integer;
begin
  fill(arr, 9, 42)
end.
