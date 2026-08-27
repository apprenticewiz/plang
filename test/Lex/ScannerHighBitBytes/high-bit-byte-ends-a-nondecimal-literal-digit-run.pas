(*
Issue #221 (see bare-byte-in-code-position-is-not-a-crash.pas next to this
file for the full root-cause writeup): the EP §6.1.7 nondecimal-literal
digit run (base '#' digits, -std=iso10206 only) called
std::isalnum(Text[Pos]) on a plain `char` to decide where the digit run
ends, so a source byte >= 0x80 right after the digits reached <cctype> as
a negative `int`.

16#F is one valid hex digit (value 15); the accented byte pair right after
it (U+00E9, UTF-8 0xC3 0xA9) is what exercises the fixed call site.  It
classifies as "not alnum" under the scanner's ASCII-only, locale-pinned
classification (see the bare-byte companion file for why that holds on
every platform plang actually runs on), so the digit run correctly stops
at "F" either side of the fix and the literal comes out as 15, matching
nondecimal-hex.pas's own 16#ff precedent one digit at a time; the two
accent bytes then fail the top-level dispatch and come back as elided
unexpected-character errors, same as the other files here.
*)

(*
RUN: not %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

16#Fé

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "15"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
