(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 A
*)

program p(output);
type u = record n: integer;
       case boolean of true: (i: integer); false: (c: char) end;
var x: u;
begin x.n := 1; x.i := 65; writeln(x.n, ' ', x.c) end.
