(*
TP7 ch.19's storage-width-selection rule (TypeContext::getSubrange,
-std=turbo only): a subrange gets the narrowest of 8-, 16- or 32-bit
signed or unsigned storage that holds its OWN declared bounds, not the
width of whatever integer type its bounds happen to be written against --
`type Grade = 1..100` is a byte even though the literal bounds' own type
(Turbo's 16-bit `Integer`) is wider.

Each case below drives that home with truncation rather than a SizeOf
query (SizeOf is not a wired-up builtin in this compiler yet): a WIDER
variable is assigned an in-range-for-it value that does NOT fit the
narrowed subrange, with checking off (range checking's own dollar-R-minus
default under -std=turbo, no flag needed -- see range-checks-default-off-
under-turbo-lets-an-out-of-range-write-through.pas), and the two's-
complement value that lands is exactly what wraps into the NARROW
storage this rule selects, not the wide one
plang used before this rule existed:
  - Grade = 1..100 -> unsigned 8-bit; 300 wraps to 300 mod 256 = 44.
  - Neg = -100..100 -> signed 8-bit (100 alone fits unsigned, but -100
    needs the sign bit); 200 (0xC8) reinterpreted signed-8 is -56.
  - Neg2 = -200..200 -> signed 16-bit (-200 does not fit signed-8's
    -128..127); 40000 (0x9C40) reinterpreted signed-16 is -25536.
  - Big = 0..1000000 -> unsigned 32-bit; 5000000000 wraps to
    5000000000 mod 2^32 = 705032704.

Confirmed against a real Turbo-Pascal-mode compiler (`fpc -Mtp -R-`),
which prints these identical four values for the identical program.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:g=44
CHECK-NEXT:n=-56
CHECK-NEXT:n2=-25536
CHECK-NEXT:b=705032704
*)

program subrangestoragenarrows;
type
  Grade = 1..100;
  Neg   = -100..100;
  Neg2  = -200..200;
  Big   = 0..1000000;
var
  g:  Grade;
  n:  Neg;
  n2: Neg2;
  b:  Big;
  i:  Integer;
  li: LongInt;
  i6: Int64;
begin
  i  := 300;         g  := i;  writeln('g=', g);
  i  := 200;         n  := i;  writeln('n=', n);
  li := 40000;        n2 := li; writeln('n2=', n2);
  i6 := 5000000000;   b  := i6; writeln('b=', b);
end.
