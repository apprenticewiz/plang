(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:21
*)

program p;
function fib(n: integer) = r : integer;
begin
  if n <= 1 then r := n
  else r := fib(n-1) + fib(n-2)
end;
begin writeln(fib(8)) end.
