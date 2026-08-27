(*
Issue #285: a diagnostic's column, and the caret drawn under it, was a raw
byte count, so a multi-byte UTF-8 character earlier on the same line pushed
both the printed line:col and the caret past the token they are meant to
mark.  The string literal on the source line below holds three accented
e's -- three characters but six bytes in UTF-8 -- so a byte-based column
put "notdeclared" at column 29: 3 cells further right than where it
actually starts on screen, instead of the 26 pinned below.

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=CARET --strict-whitespace --match-full-lines %s < %t.err
*)

(*
ERR: :26: error: undefined identifier 'notdeclared'
CARET:begin write('ééé'); x := notdeclared end.
CARET-NEXT:                         ^
*)

program p;
var x: integer;
begin write('ééé'); x := notdeclared end.
