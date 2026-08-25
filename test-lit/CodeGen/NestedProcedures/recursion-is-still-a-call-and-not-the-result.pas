(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:120 42
*)

program p(output);
function fact(n: integer): integer;
begin if n <= 1 then fact := 1 else fact := n * fact(n - 1) end;
function g: integer;
  procedure h; begin g := 42 end;
begin g := 0; h end;
begin writeln(fact(5):1, ' ', g:1) end.
