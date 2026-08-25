(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true false
CHECK-NEXT:false true
*)

program p;
type pi = ^integer;
var q: pi;
begin q := nil; writeln(q = nil, ' ', q <> nil);
 new(q); writeln(q = nil, ' ', q <> nil); dispose(q) end.
