(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[init]
*)

program p;
type st = string(12);
procedure q; var a: st value 'init';
begin writeln('[', a, ']') end;
begin q end.
