(*
TP `RandSeed` (Sema::registerBuiltins, -std=turbo only) is a predefined,
SETTABLE global holding Random's own internal generator state -- the exact
same predefined-mutable-Var mechanism ExitCode uses (see that Symbol's own
comment: LinkName bound to a runtime global, plang_tp_randseed, that every
compiled object only declares, not defines).

This is the task's own determinism anchor: setting RandSeed to a fixed
value, reading Random (both the integer-range and the real-valued shape),
resetting RandSeed to the SAME fixed value, and reading Random again
reproduces IDENTICAL results both times -- proof RandSeed genuinely drives
the generator, not a comparison against any real Borland Turbo Pascal 7 or
Free Pascal output table (this is plang's OWN generator; see
runtime/plang_math.cpp's own comment).  A different seed is also confirmed
to (overwhelmingly likely, not guaranteed by construction, the same as any
PRNG reseed) produce a different result, so agreement from the same seed is
not simply true unconditionally.  RandSeed's own read-back (assign, then
read the same value back) confirms it is a genuine two-way variable, not a
write-only knob.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:randseed-readback=TRUE
CHECK-NEXT:same-int-from-same-seed=TRUE
CHECK-NEXT:same-real-from-same-seed=TRUE
CHECK-NEXT:different-seed-gives-a-different-result=TRUE
*)

program p;
var
  a1, a2: LongInt;
  r1, r2: Real;
begin
  RandSeed := 12345;
  writeln('randseed-readback=', RandSeed = 12345);

  RandSeed := 42;
  a1 := Random(1000000);
  r1 := Random;

  RandSeed := 42;
  a2 := Random(1000000);
  r2 := Random;

  writeln('same-int-from-same-seed=', a1 = a2);
  writeln('same-real-from-same-seed=', r1 = r2);

  RandSeed := 42;
  a1 := Random(1000000);
  RandSeed := 1729;
  a2 := Random(1000000);
  writeln('different-seed-gives-a-different-result=', a1 <> a2);
end.
