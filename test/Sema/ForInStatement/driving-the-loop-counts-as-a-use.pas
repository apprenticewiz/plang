(*
Issue #689: with the control variable now the REAL declared one (not a
per-loop shadow Sema analyzed while CodeGen silently wrote through the
outer variable instead), a variable whose only mention is driving a
`for ... in` loop must not be flagged "declared but never used" -- the
shadow used to get marked Referenced instead of the real symbol, so this
false positive fired on exactly the one construct the feature exists for.

RUN: %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: declared but never used
*)

program p(output);
var c: char; s: set of char;
begin s := ['a', 'b']; for c in s do end.
