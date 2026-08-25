(*
The caret line carries nothing but blanks and the caret, and the caret
stands in the column the headline named -- verified here by pinning the
real, empirically-confirmed column (12) and the exact whitespace before
the caret, rather than computing the relationship at RUN time (lit has
no arithmetic/substring-position primitive to re-derive one from the
other the way the original C++ assertion did). The line number is
deliberately NOT pinned: it shifts with this header comment's own
length, unlike the original GTest fixture, which had no directive
preamble in front of its source.

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=CARET --strict-whitespace --match-full-lines %s < %t.err
*)

(*
ERR: :12: error: undefined identifier 'notdeclared'
CARET:begin x := notdeclared end.
CARET-NEXT:           ^
*)

program p;
var x: integer;
begin x := notdeclared end.
