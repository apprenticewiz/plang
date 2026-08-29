(*
plangFormatReal is now parameterized by a small profile struct (width,
exponent-digit count, exponent letter) so a dialect's default real-write
shape need not be a single hardcoded constant.  Turbo's own profile
(PlangRealProfileTurbo, runtime/plang_real.h) differs from ISO 7185/EP's in
exactly one field: the exponent letter is uppercase 'E', not lowercase 'e' --
confirmed against `fpc -Mtp`, which maps Turbo's `real` onto the same 8-byte
Double this runtime always uses (Extended is a distinct, 10-byte type FPC
gives only an explicitly-declared `extended`, never a bare literal's
default), and writes it at the identical width and decimal-place count as
ISO's own default -- 24 characters, 17 significant digits, a 3-digit
exponent.

PERMANENT DIVERGENCE FROM REAL TURBO PASCAL: an unsuffixed real literal in
real `fpc -Mtp` is typed `Extended` (80-bit, ~21 significant decimal
digits), not `Double`; plang has only `Double` (64-bit, ~17 digits).  No
golden output for a Turbo real test can ever be transcribed directly from a
real `fpc -Mtp` run -- the expected output below is plang's OWN profile
applied to plang's OWN Double values, not anything copied from a real fpc
transcript (compare the values here against
test/CodeGen/WriteReal/the-default-real-profile-under-iso7185-is-unchanged-by-turbo.pas's
own baseline: identical shape, save for the exponent's case, is exactly the
point).

The ISO/EP sibling baselines this reversal must not have moved are that
file and test/EP/FieldWidth/the-default-real-profile-under-extended-pascal-is-unchanged-by-turbo.pas.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 0.0000000000000000E+000
CHECK-NEXT: 2.0000000000000000E+000
CHECK-NEXT: 1.0000000000000000E-010
CHECK-NEXT: 1.0000000000000000E+308
CHECK-NEXT:-3.5000000000000000E+000
*)

program p;
begin
  writeln(0.0);
  writeln(2.0);
  writeln(1.0e-10);
  writeln(1.0e308);
  writeln(-3.5)
end.
