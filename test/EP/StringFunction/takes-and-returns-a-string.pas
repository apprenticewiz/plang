(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hi!]
*)

program p(output);
function g(s: string(10)): string(20); begin g := s + '!' end;
begin writeln('[', g('hi'), ']') end.
