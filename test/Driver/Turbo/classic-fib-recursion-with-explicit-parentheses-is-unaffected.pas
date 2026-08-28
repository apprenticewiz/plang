(*
The reversal in
a-recursive-function-called-by-its-own-bare-name-actually-recurses.pas
and
a-bare-read-of-a-parameterized-functions-own-name-requires-an-argument-list.pas
is about a BARE (no-parentheses) read only.  'Fib(n-1)' and 'Fib(n-2)'
below are CallExpr nodes from the parser onward, never reaching
Sema::checkIdent (the bare-IdentExpr path) at all, so the ordinary,
always-recursive call these already made is completely unaffected.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

program fib;
function Fib(n: integer): integer;
begin
  if n <= 1 then
    Fib := n
  else
    Fib := Fib(n-1) + Fib(n-2);
end;
begin
  writeln(Fib(10));
end.

(*
CHECK:55
*)
