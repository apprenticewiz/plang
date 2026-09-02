(*
Issue #677: `write`/`Str`'s fixed-decimal form (`v:W:D`) used to hand V
straight to printf's `%*.*f`, which prints a double's EXACT binary value out
to however many decimals D asks for -- correct arithmetic, but not what any
Pascal reader expects once D (or a large integer part) asks for more
significant digits than a double actually carries (15-17 of them):
`str(1e30:3:2, s)` used to answer '...19884624838656.00' (1e30's own exact
IEEE-754 double expansion showing through), not a clean, zero-padded
'...00000000000.00'.  `fpc -Mtp` does not show that expansion either, so
runtime/plang_real.cpp's plangRealFixedNeedsCap now caps a request whose own
significant-digit span exceeds 17 to that many, treating every digit
position beyond as 0 (see that function's own comment for the exact
threshold and the ORDINARY-rounding cases it deliberately steps aside for,
such as `0.06:1:1`, which only plain printf's own %.*f rounding -- not a
fixed-significant-digit reconstruction -- gets right).

Exercises: the issue's own two repros (Str of 1e30, and a wide-D write of an
ordinary value); a value whose integer part alone already exceeds 17
significant digits; and -- the regression this item's own first, buggy
attempt at a fix introduced and this test guards against -- ordinary
ROUNDING at a coarse decimal place still works (0.06 rounds to 0.1 at one
decimal place, 1.5 and 2.5 round to 2 at zero decimal places, matching
round-half-to-even), since none of those requests actually exceed 17
significant digits and so must never reach the capped/padded path at all.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:str-1e30=1000000000000000000000000000000.00
CHECK-NEXT:wide-d=123456789.12300000000000000
CHECK-NEXT:huge-int-part=-100000000000000000000.000
CHECK-NEXT:round-half-up-tenths=  0.1
CHECK-NEXT:round-half-to-even-1=    2
CHECK-NEXT:round-half-to-even-2=    2
*)

var
  s: string;
begin
  str(1e30:3:2, s);
  writeln('str-1e30=', s);

  writeln('wide-d=', 123456789.123:20:17);

  writeln('huge-int-part=', -1e20:5:3);

  writeln('round-half-up-tenths=', 0.06:5:1);

  writeln('round-half-to-even-1=', 1.5:5:0);
  writeln('round-half-to-even-2=', 2.5:5:0);
end.
