(*
Issue #221 (see bare-byte-in-code-position-is-not-a-crash.pas next to this
file for the full root-cause writeup): Scanner::scanIdentifierOrKeyword's
continuation loop called std::isalnum(Text[Pos]) on a plain `char`, so once
an identifier is under way, a following source byte >= 0x80 reached
<cctype> as a negative `int` -- undefined behavior on the common
platform where `char` is signed.

'a' opens an identifier; the accented byte pair right after it (U+00E9,
UTF-8 0xC3 0xA9) is what exercises the fixed call site, deciding whether
the identifier keeps growing.  Both bytes classify as "not alnum" under the
scanner's ASCII-only, locale-pinned classification (see the companion file
for why that holds on every platform plang actually runs on), so the
identifier correctly stops after the single letter "a" either side of the
fix; the two bytes then each fail the same classification at the top-level
dispatch and come back as elided unexpected-character errors, the same as
the bare-byte companion file.
*)

(*
RUN: not %plang_ir -dump-tokens %s | FileCheck %s
*)

aé

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "a"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
