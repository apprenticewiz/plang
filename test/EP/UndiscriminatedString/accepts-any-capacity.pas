(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[ab] 2
CHECK-NEXT:[cdefgh] 6
*)

program p(output); var a: string(5); b: string(40);
procedure q(s: string); begin writeln('[', s, '] ', length(s)) end;
begin a := 'ab'; b := 'cdefgh'; q(a); q(b) end.
