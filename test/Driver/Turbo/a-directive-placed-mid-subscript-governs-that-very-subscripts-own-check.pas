(*
Issue #655: real Turbo/FPC apply a `{$R+}`/`{$R-}` switch as soon as it is
lexically encountered -- including partway through the very statement it
appears in, not just to statements that come after it.  `a[ {$R+} i ]`
under `{$R-}` must still trap on an out-of-range `i`: the directive sits
inside the subscript's own brackets, before the index is actually read, so
by the time the index is checked the switch has already flipped.

Before this fix, CGIndexAccess.cpp's range-check call sites queried the
switch state at the whole IndexExpr's own location (`a`'s position, the
START of `a[ {$R+} i ]`) rather than at the subscript's own location (`i`'s
position, AFTER the directive) -- so a directive written between the `[`
and the subscript it governs was invisible to that subscript's own check,
even though ordinary, top-level `{$R+}` between statements already worked
correctly (switch-directive-r-plus-turns-range-checks-on-partway-through-
the-file.pas).

A READ, not a write, for the same reason that sibling test gives: an
out-of-range WRITE with checking off scribbles on whatever memory the
index lands in, not something a lit test can depend on.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 201 %run %t > %t.out 2> %t.err
RUN: FileCheck %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
CHECK:before the mid-subscript {$R+}
ERR: Runtime error 201 at $
*)

program mid_subscript_directive;
var a: array[1..3] of integer;
    i, dummy: integer;
begin
  {$R-}
  i := 10;
  writeln('before the mid-subscript {$R+}');
  dummy := a[ {$R+} i ];
  writeln('unreachable: mid-subscript {$R+} did not govern its own check')
end.
