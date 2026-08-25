(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:one
CHECK-NEXT:many
*)

program p(output);
function ap(function f(n: integer): string(20); v: integer): string(20);
begin ap := f(v) end;
function greet(n: integer): string(20);
begin if n = 1 then greet := 'one' else greet := 'many' end;
begin writeln(ap(greet, 1)); writeln(ap(greet, 2)) end.
