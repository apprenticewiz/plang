(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p;
function g(n: integer): integer; forward;
function g(n: integer): integer;
begin g := n * 2 end;
begin writeln(g(21)) end.
