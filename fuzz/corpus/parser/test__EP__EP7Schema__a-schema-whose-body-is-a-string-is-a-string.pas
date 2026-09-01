(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:eq
CHECK-NEXT:[hi] len=2
*)

program p(output);
type s(n: integer) = string(n);
var v: s(10);
begin v := 'hi';
  if v = 'hi' then writeln('eq');
  writeln('[', v, '] len=', length(v):1) end.
