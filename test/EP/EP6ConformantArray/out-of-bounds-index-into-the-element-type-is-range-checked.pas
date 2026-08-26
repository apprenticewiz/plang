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

(* EP §6.7.3.7: a subscript past the conformant dimensions indexes the
   element type -- fixed, statically-known bounds -- and that access has to
   be range-checked too, the same as a subscript on the conformant
   dimensions themselves. *)

(*
ERR: array index 99 out of bounds 1..3
*)

program p;
type row = array [1..3] of integer;
procedure fill(var a: array [lo..hi : integer] of row; j: integer);
begin
  a[lo][j] := 99
end;
var m: array [1..2] of row;
begin
  fill(m, 99)
end.
