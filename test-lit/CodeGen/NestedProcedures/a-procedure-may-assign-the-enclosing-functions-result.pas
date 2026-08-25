(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:total(4) = 14
*)

program p(output);
function total(k: integer): integer;
var n: integer;
  procedure setit;
  begin n := n * 2; total := n + k end;
begin n := k + 1; setit end;
begin writeln('total(4) = ', total(4):1) end.
