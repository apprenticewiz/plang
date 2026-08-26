(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3.0 4.0
*)

program p(output);
const c = cmplx(3.0, 4.0);
begin writeln(re(c):3:1, ' ', im(c):3:1) end.
