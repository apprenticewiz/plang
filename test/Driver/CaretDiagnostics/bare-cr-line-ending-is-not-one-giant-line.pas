(*
Issue #285: a diagnostic's own line/column and the source snippet it quotes
both come from SourceManager, which indexed only '\n' as a line ending.
A file using bare CR (no LF at all -- classic Mac OS's line ending) below
this comment has none, so before the fix every position in it resolved to
line 1 and getLineText handed back the whole three-statement program as
"the line" -- not just the one line the error is actually on.  The
program below is the same one puts-the-caret-under-the-column-reported.pas
already covers, just split by bare CR instead of a plain newline.

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=CARET --strict-whitespace --match-full-lines %s < %t.err
*)

program p;var x: integer;begin x := notdeclared end.

(*
ERR: :18:12: error: undefined identifier 'notdeclared'
CARET:begin x := notdeclared end.
CARET-NEXT:           ^
*)

