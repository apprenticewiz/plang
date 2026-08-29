(*
TP `Random(Range)` (Builtins.def, -std=turbo only), the one-argument shape:
an INTEGER-kind value in [0, Range), staying in Range's own type -- the same
"stays in the argument's own type" rule Abs/Sqr/Succ/Pred/High/Low already
follow (Sema::checkCallExpr's own Random arm).  Exercised over several
different Range values, including the two edge cases plang's own generator
defines on its own terms: Random(1), always 0 (the only value in [0, 1)),
and Random(0), which has no [0, 0) to answer from at all and so answers 0
rather than aborting (plang's own choice -- runtime/plang_math.cpp's
plang_tp_random_range -- not a claim about real Borland Turbo Pascal 7's or
Free Pascal's own, mutually inconsistent, behavior for a zero Range).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:range-50-ok=TRUE
CHECK-NEXT:range-1000000-ok=TRUE
CHECK-NEXT:range-1-always-zero=TRUE
CHECK-NEXT:range-0-is=0
*)

program p;
var
  i: Integer;
  n: LongInt;
  ok: Boolean;
begin
  ok := true;
  for i := 1 to 5000 do begin
    n := Random(50);
    if (n < 0) or (n >= 50) then ok := false;
  end;
  writeln('range-50-ok=', ok);

  ok := true;
  for i := 1 to 5000 do begin
    n := Random(1000000);
    if (n < 0) or (n >= 1000000) then ok := false;
  end;
  writeln('range-1000000-ok=', ok);

  ok := true;
  for i := 1 to 100 do begin
    n := Random(1);
    if n <> 0 then ok := false;
  end;
  writeln('range-1-always-zero=', ok);

  n := Random(0);
  writeln('range-0-is=', n);
end.
