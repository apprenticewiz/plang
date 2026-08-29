(*
Turbo's 'absolute' directive overlays a new variable's storage directly onto
an existing variable's -- W and B[0]/B[1] genuinely share one run of memory,
not merely start out equal.  Proven here by mutating B AFTER W has already
been read once and observing W's own next read change to match: a copy
would not.  Byte/Word do not exist yet in this build (see the plan's scope
note), so two Chars overlaid as an Integer stand in for them -- the overlay
mechanism itself (CodeGenProcs.cpp's emitBlockAllocas/emitGlobals: the new
symbol is defVar'd straight onto the aliased variable's own existing
pointer, with no storage of its own) does not care what either side's type
is.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:2
*)

var
  B: array[0..1] of Char;
  W: Integer absolute B;
begin
  B[0] := Chr(1);
  B[1] := Chr(0);
  writeln(W);
  B[0] := Chr(2);
  writeln(W);
end.
