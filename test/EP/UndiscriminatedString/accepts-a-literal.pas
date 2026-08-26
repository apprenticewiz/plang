(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[xy]
*)

program p(output);
procedure q(s: string); begin writeln('[', s, ']') end;
begin q('xy') end.
