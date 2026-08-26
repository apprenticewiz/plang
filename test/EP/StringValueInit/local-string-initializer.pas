(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[local]
*)

program p;
procedure q; var s: string(10) value 'local';
begin writeln('[', s, ']') end;
begin q end.
