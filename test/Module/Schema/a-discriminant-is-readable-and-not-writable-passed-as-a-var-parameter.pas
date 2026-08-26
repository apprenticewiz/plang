(*
EP section 6.4.7: a discriminant is a value the schematic variable carries,
not a component of it. It is spelled like a field and reads like one, so
every way of writing to it was accepted by Sema and then killed codegen
with "record has no field named 'n'" -- an internal error on four
separate programs that should each have had a diagnostic.
*)

(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: requires a variable
*)

program p(output);
type t(n: integer) = record a: array[1..n] of integer end;
var q: ^t; v: t(6);
procedure r(var x: integer); begin x := 77 end;
begin new(q, 4); r(q^.n) end.
