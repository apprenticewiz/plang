(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10 20 100
*)

program p(output);
type Inner = array[1..2] of integer;
     Outer = record a: Inner; b: integer end;
var o: Outer;
begin o := Outer[a: [1: 10; 2: 20]; b: 100];
  writeln(o.a[1]:1, ' ', o.a[2]:1, ' ', o.b:1) end.
