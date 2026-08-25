(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hi!]
*)

program p(output);
function f(s: string): string(20); begin f := s + '!' end;
begin writeln('[', f('hi'), ']') end.
