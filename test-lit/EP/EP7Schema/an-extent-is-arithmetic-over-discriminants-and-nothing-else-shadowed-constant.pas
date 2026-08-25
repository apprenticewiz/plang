(*
R3.  A schema body's bounds are carried to codegen as a closed form over
the discriminants BY INDEX, with every other leaf folded in the scope the
schema was declared in.  The form contains no identifier, so there is
nothing left for a procedure's locals to capture at the allocation site
-- the defect 0.1.6 shipped a scope barrier to guard against.
*)

(* The same shape with the constant shadowed by a local at the allocation
   site.  This is the 0.1.6 defect; it now cannot arise, because the form
   has no name in it to resolve here. *)

(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10 20 30 40 50 60 70 
*)

program p(output);
const k = 3;
type v(n: integer) = array[1..n+k] of integer;
var q: ^v; i: integer;
procedure alloc;
var k: integer;
begin k := 1; new(q, 4) end;
begin alloc;
  for i := 1 to 7 do q^[i] := i * 10;
  for i := 1 to 7 do write(q^[i]:1, ' ');
  writeln end.
