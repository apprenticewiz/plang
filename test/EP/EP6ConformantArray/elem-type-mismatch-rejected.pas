(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: conformant
*)

program p;
procedure proc(A: array [lo..hi : integer] of integer);
begin end;
var arr: array [1..3] of real;
begin proc(arr) end.
