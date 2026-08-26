(*
codegen's schemaDefs_ is keyed by the schema's NAME and was the one of
the five per-procedure tables nobody restored -- typeAliases, consts,
requiredConsts and labelBlocks all are.  So a procedure declaring a
schema whose spelling an outer one already used left its definition
behind for every procedure emitted after it, and a sibling's new() was
sized from a stranger's body.

main escaped this by accident, which is why it went unnoticed: emitMain
re-registers the program block's schemas, putting the outer definition
back before the body is emitted.  It takes a SIBLING procedure to see.
*)

(* Under-allocating: `second` allocates through the outer vec, which is a
   hundred times bigger than first's. *)

(*
RUN: %plang_ep -fno-range-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:500
*)

program p(output);
type vec(n: integer) = array[1..n*100] of integer;
var q: ^vec; i: integer;
procedure first;
type vec(n: integer) = array[1..n] of integer;
var r: ^vec;
begin new(r, 1); r^[1] := 0 end;
procedure second;
begin new(q, 5) end;
begin first; second;
  for i := 1 to 500 do q^[i] := i;
  writeln(q^[500]:1) end.
