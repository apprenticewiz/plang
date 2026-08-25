(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 7 10 99
CHECK-NEXT:1.5
*)

program p(output);
type t = record
       case a: integer of
         1: (x: integer;
             case b: integer of
               10: (p: integer);
               11: (q: real));
         2: (y: real)
     end;
var v: t;
begin
  v.a := 1; v.x := 7; v.b := 10; v.p := 99;
  writeln(v.a, ' ', v.x, ' ', v.b, ' ', v.p);
  v.q := 1.5; writeln(v.q:0:1)
end.
