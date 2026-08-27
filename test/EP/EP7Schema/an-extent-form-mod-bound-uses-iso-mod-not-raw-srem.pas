(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(* Issue #228: emitExtentForm (SchemaLayoutEngine.cpp) lowered a schema
   bound's `mod` to a raw LLVM srem, whose sign follows the DIVIDEND (C's
   `%`) rather than ISO §6.7.2.2's `mod` (result in [0, j), sign of the
   DIVISOR) -- the same distinction Arith.h's isoMod exists for and
   CGBinaryOps.cpp's language-level `mod` already adjusts for.

   -4 mod 3 is 2 under ISO §6.7.2.2 (-4 = (-2)*3 + 2), but srem(-4, 3) is
   -1.  So the buggy lower bound was -1 instead of 2: a 4-element array
   -1..2 that silently accepted index 0, instead of the correct 1-element
   array 2..2 that must range-check-trap on it.

   The discriminant is threaded through a VAR parameter, not written
   directly as `new(q, -4)`, so Sema cannot fold the whole bound to a
   constant at the declaration site (ActiveSchemaBindings_ would otherwise
   resolve `n` to -4 during Sema and never exercise emitExtentForm's own
   runtime lowering at all -- the very thing under test here). *)

(*
ERR: out of bounds 2..2
*)

program p(output);
type vec(n: integer) = array[n mod 3 .. 2] of integer;
var q: ^vec; i: integer;

procedure alloc(m: integer);
begin
  new(q, m)
end;

begin
  alloc(-4);
  i := 0;
  q^[i] := 1;
  writeln('not reached')
end.
