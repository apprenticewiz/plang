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

(* And the bounds the range check uses come from the same place, so the
   check went missing in `second` while main's read of the same object was
   checked correctly. *)

(*
RUN: %plang_ep -frange-checks %s -o %t
RUN: not %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 1..5
*)

program p(output);
type vec(n: integer) = array[1..n] of integer;
var q: ^vec;
procedure first;
type vec(n: integer) = array[1..n*100] of integer;
var r: ^vec;
begin new(r, 1); r^[1] := 0 end;
procedure second;
begin new(q, 5); q^[6] := 77 end;
begin first; second; writeln('unreachable') end.
