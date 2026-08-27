(*
Issue #221 (see bare-byte-in-code-position-is-not-a-crash.pas next to this
file for the full root-cause writeup): Scanner::scanNumber has four separate
std::isdigit(Text[Pos-or-Look]) call sites that took a plain `char` --
the integer-digit run, the fractional-digit run, the exponent lookahead,
and the exponent-digit run -- so a source byte >= 0x80 immediately after
any of an integer, a fraction, or an exponent reached <cctype> as a
negative `int` at whichever call site stops that run.

Each of the four space-separated tokens below terminates one of those four
runs with the same accented byte pair (U+00E9, UTF-8 0xC3 0xA9):
  9<byte>     -- the plain integer-digit run (right after scanNumber's
                 entry loop)
  1.5<byte>   -- the fractional-digit run
  1e<byte>    -- the exponent lookahead (peeks past 'e' for a sign then a
                 digit; finding neither, 'e' is left for the next token,
                 same as the existing an-e-with-no-digits-is-not-part-of-
                 the-number.pas case)
  1e2<byte>   -- the exponent-digit run, once an exponent was accepted

Every one of those std::isdigit calls sees a byte that classifies as
"not a digit" under the scanner's ASCII-only, locale-pinned classification
(see the bare-byte companion file for why that holds on every platform
plang actually runs on), so each number is left exactly where a sane
implementation would leave it either side of the fix; the accent bytes
themselves each fail the top-level dispatch afterwards and come back as
elided unexpected-character errors, same as the other files here.
*)

(*
RUN: not %plang_ir -dump-tokens %s | FileCheck %s
*)

9é 1.5é 1eé 1e2é

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "9"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: RealLit "1.5"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: IntLit "1"
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Identifier "e"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: RealLit "1e2"
CHECK-NEXT: [[P6:[0-9]+:[0-9]+]]: Eof
*)
