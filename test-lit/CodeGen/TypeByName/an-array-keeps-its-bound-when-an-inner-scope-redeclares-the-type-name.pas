(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42 44 7
*)

program p(output);
type t = array[0..4] of integer;
var a: t; guard: integer; i: integer;
procedure q;
type t = array[10..14] of integer;
var l: t;
begin l[10] := 1; a[0] := 42; a[4] := 44 end;
begin guard := 7; for i := 0 to 4 do a[i] := 0; q;
  writeln(a[0], ' ', a[4], ' ', guard) end.
