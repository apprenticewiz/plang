(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2 2.50
*)

program p(output);
type t = record k: integer;
       case s: integer of 1: (a: integer); 2: (b: real) end;
var x, y: t;
begin x.k := 1; x.s := 2; x.b := 2.5; y := x;
  writeln(y.k, ' ', y.s, ' ', y.b:0:2) end.
