(*
RUN: %plang -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -O1 %s -o %t.O1
RUN: %run %t.O1 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -O3 %s -o %t.O3
RUN: %run %t.O3 | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4800
*)

program p;
var i, j, acc: integer;
begin
  acc := 0;
  for i := 1 to 40 do for j := 1 to 40 do acc := acc + (i mod 7) * (j mod 3);
  writeln(acc)
end.
