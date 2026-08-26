(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0 1
*)

program p(output);
type t = record case k: (alpha, beta) of
       alpha: (c: (red, green));
       beta:  (n: integer) end;
var v: t;
begin v.k := alpha; v.c := green;
  writeln(ord(v.k), ' ', ord(v.c)) end.
