(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4
CHECK-NEXT:-4
CHECK-NEXT:2
CHECK-NEXT:3
CHECK-NEXT:-3
CHECK-NEXT:-9223372036854775808
CHECK-NEXT:-9223372036854775808
CHECK-NEXT:9223372036854774784
CHECK-NEXT:9223372036854774784
*)

program p(output);
begin
  writeln(round(3.5));
  writeln(round(-3.5));
  writeln(round(2.4));
  writeln(trunc(3.9));
  writeln(trunc(-3.9));
  writeln(trunc(-9223372036854775808.0));
  writeln(round(-9223372036854775808.0));
  writeln(trunc(9223372036854774784.0));
  writeln(round(9223372036854774784.0))
end.
