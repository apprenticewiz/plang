(*
EP section 6.4.7: a discriminant is a value the schematic variable carries,
not a component of it. It is spelled like a field and reads like one, so
every way of writing to it was accepted by Sema and then killed codegen
with "record has no field named 'n'" -- an internal error on four
separate programs that should each have had a diagnostic.
*)

(* Reading one is how a program learns how big its own value is, so the
   rule has to stop at writing. All three spellings still read. *)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4
CHECK-NEXT:6
CHECK-NEXT:4
*)

program p(output);
type t(n: integer) = record a: array[1..n] of integer end;
var q: ^t; v: t(6);
begin new(q, 4);
      writeln(q^.n:1); writeln(v.n:1);
      with q^ do writeln(n:1) end.
