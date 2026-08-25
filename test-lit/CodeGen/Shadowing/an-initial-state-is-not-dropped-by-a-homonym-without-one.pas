(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:k=9
*)

program p(output);
type inner = record k: integer value 9; j: integer end;
     outer = record a: inner; b: integer end;
procedure q;
type inner = record k: integer; j: integer end;
var g: outer;
begin writeln('k=', g.a.k:1) end;
begin q end.
