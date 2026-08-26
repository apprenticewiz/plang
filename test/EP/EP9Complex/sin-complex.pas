(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0.0
CHECK-NEXT:0.0
*)

program p;
var c: complex;
    r: complex;
begin
  c := cmplx(0.0, 0.0);
  r := sin(c);
  writeln(re(r):1:1);
  writeln(im(r):1:1)
end.
