(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[zz]
CHECK-NEXT:[ab]
*)

program p(output); var a: string(5);
procedure q(s: string); begin s := 'zz'; writeln('[', s, ']') end;
begin a := 'ab'; q(a); writeln('[', a, ']') end.
