(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hello]
*)

program p(output);
procedure shows(s: string); begin writeln('[', s, ']') end;
procedure runs(procedure s(t: string)); begin s('hello') end;
begin runs(shows) end.
