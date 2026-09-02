(*
Issue #635's fix widened Hi/Lo/Swap's argument check to accept a
subrange-typed argument (ordinalBase unwraps it to its Integer host), but
an Enum argument must still be rejected: ordinalBase leaves a non-subrange
type unchanged, so an enum's own Kind (Enum, not Integer) still fails the
guard -- matching `fpc -Mtp`, which refuses this identical program too.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'hi' requires an integer argument of at least 16 bits, got 'Color'
*)

program p;
type
  Color = (Red, Green, Blue);
var
  c: Color;
  n: Byte;
begin
  c := Red;
  n := Hi(c);
end.
