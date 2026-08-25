(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 1 9
*)

program p(output);
type t = record k: integer;
       case s: integer of 1: (a: integer); 2: (b: real) end;
var x: t;
begin x := t[k: 7; s: 1; a: 9];
  writeln(x.k, ' ', x.s, ' ', x.a) end.
