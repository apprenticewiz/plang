(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 9 6 3 57
*)

program p(output);
const n = 5;
type t1 = 1..abs(-5);
     t2 = 1..sqr(3);
     t3 = 1..succ(n);
     t4 = 1..pred(n, 2);
     t5 = 1..ord('9');
var a: t1; b: t2; c: t3; d: t4; e: t5;
begin
  a := 5; b := 9; c := 6; d := 3; e := 57;
  writeln(a, ' ', b, ' ', c, ' ', d, ' ', e)
end.
