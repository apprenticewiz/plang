(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true
CHECK-NEXT:false
*)

program p;
type pi = ^integer;
var a, b: pi;
begin new(a); b := a; writeln(a = b);
 new(b); writeln(a = b); dispose(a); dispose(b) end.
