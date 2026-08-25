(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[xxx]
*)

program p(output);
function rep(n: integer): string(20);
begin if n <= 0 then rep := '' else rep := 'x' + rep(n - 1) end;
begin writeln('[', rep(3), ']') end.
