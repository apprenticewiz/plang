(*
Issue #676: `Random(Range)` with Range < 0 used to answer a flat 0 --
runtime/plang_math.cpp's plang_tp_random_range folded EVERY Range <= 0 into
the same "nothing to answer from" case as Range == 0.  `fpc -Mtp` 3.2.2
disagrees: a negative Range has a well-defined (Range, 0] to answer from,
just like a positive Range has [0, Range), and it answers a deterministic,
nonzero value from it -- the runtime's own prior comment claiming TP/FPC are
"inconsistent with each other" here was stale, not a real field-practice
finding.  Range == 0 alone still has no [0, 0) (or (0, 0]) to answer from
and stays a flat 0, unchanged.

plang's own generator does not claim to reproduce TP's or FPC's own sequence
bit-for-bit (see plang_tp_random_range's own comment, including why an
earlier, signed-floor-division attempt at this very fix was abandoned), so
this checks the RELATIONSHIP the runtime actually implements rather than any
particular value: Random(-R) reuses Random(R)'s own magnitude computation
against |R| and negates it, so from an identical RandSeed the two are always
EXACT negations of one another, and a negative Range's result can never
equal Range itself (the open end of (Range, 0]) for the same reason a
positive Range's result can never equal Range (the open end of [0, Range)).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:range-neg-50-in-bounds=TRUE
CHECK-NEXT:range-neg-50-ever-nonzero=TRUE
CHECK-NEXT:range-neg-1000000-in-bounds=TRUE
CHECK-NEXT:negation-relationship-ok=TRUE
CHECK-NEXT:range-0-is=0
*)

var
  i: Integer;
  n, nNeg, seed: LongInt;
  ok, everNonzero: Boolean;
begin
  ok := true;
  everNonzero := false;
  for i := 1 to 5000 do begin
    n := Random(-50);
    if (n > 0) or (n <= -50) then ok := false;
    if n <> 0 then everNonzero := true;
  end;
  writeln('range-neg-50-in-bounds=', ok);
  writeln('range-neg-50-ever-nonzero=', everNonzero);

  ok := true;
  for i := 1 to 5000 do begin
    n := Random(-1000000);
    if (n > 0) or (n <= -1000000) then ok := false;
  end;
  writeln('range-neg-1000000-in-bounds=', ok);

  { Same RandSeed -> same generator state -> Random(-R) is EXACTLY
    -Random(R), since both reuse the identical magnitude computation
    against |R|. }
  ok := true;
  for i := 1 to 200 do begin
    seed := i * 7919;
    RandSeed := seed;
    n := Random(37);
    RandSeed := seed;
    nNeg := Random(-37);
    if -nNeg <> n then ok := false;
  end;
  writeln('negation-relationship-ok=', ok);

  writeln('range-0-is=', Random(0));
end.
