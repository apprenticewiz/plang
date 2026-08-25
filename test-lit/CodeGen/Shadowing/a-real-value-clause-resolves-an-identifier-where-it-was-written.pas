(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:  3.5
*)

program p(output);
const Pi2 = 3.5;
type R = real value Pi2;
     S = R;
procedure Inner;
const Pi2 = 9.9;
var x: S;
begin writeln(x:5:1) end;
begin Inner end.
