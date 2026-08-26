(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[inner]
*)

program p;
procedure q; const tag = 'inner'; var s: string(10);
begin s := tag; writeln('[', s, ']') end;
begin q end.
