(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hello 5 true
*)

program p(output);
type A(m: integer) = string(m);
     B(n: integer) = A(n);
var v, w: B(10);
begin v := 'hello'; w := 'hello';
  writeln(v, ' ', length(v):1, ' ', v = w) end.
