(*
The classic recursive idiom -- 'Fib := Fib(n-1) + Fib(n-2)' -- always
spells its recursive calls WITH the argument list, so it is unaffected by
this reversal (see the sibling
classic-fib-recursion-with-explicit-parentheses-is-unaffected.pas).  This
is the case that DOES change: a bare, unparenthesized 'Fib' inside Fib's
own body, read (not assigned) where Fib takes a parameter, is now
ISO §6.7.3's function-designator with no argument list to supply one --
exactly the same err_function_requires_args a bare 'Fib' anywhere OUTSIDE
its own body already reports (Sema::checkIdent's generic
SymbolKind::Proc/IsFunction arm), reached by falling through rather than
by any special-casing of its own.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

program fibbare;
function Fib(n: integer): integer;
begin
  if n <= 1 then
    Fib := n
  else
    Fib := Fib + 1;
end;
begin
  writeln(Fib(10));
end.

(*
CHECK: function 'Fib' requires an argument list
*)
