(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
*)

program p(output);
const Foo = 10;
type K = integer value Foo;
     J = K;
procedure Inner;
const Foo = 99;
var n: J;
begin writeln(n) end;
begin Inner end.
