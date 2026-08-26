(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:  2.0000
CHECK-NEXT:  0.0000
CHECK-NEXT:  0.0000
CHECK-NEXT:  1.0000
*)

program p(output);
begin
  writeln(sqrt(4.0):8:4);
  writeln(sqrt(0.0):8:4);
  writeln(ln(1.0):8:4);
  writeln(ln(2.718281828):8:4)
end.
