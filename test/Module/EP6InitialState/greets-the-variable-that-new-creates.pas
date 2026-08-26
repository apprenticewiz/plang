(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 9
*)

program p(output);
type k = integer value 5;
     r = record a: k; b: integer value 9 end;
     pr = ^r;
var q: pr;
begin new(q); writeln(q^.a, ' ', q^.b); dispose(q) end.
