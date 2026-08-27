(*
Issue #151: a leading UTF-8 BOM (EF BB BF) was tokenized byte-by-byte as
three bogus "unexpected character" errors instead of being skipped. The
very first three bytes of this file, before even this comment opens, are
that BOM -- SourceManager strips it from the buffer before line/column
bookkeeping ever starts, so the real error below must be the only
diagnostic, and it must land at the same line/column it would if the
BOM were never there.

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=NOERR %s < %t.err
*)

(*
ERR: [[FILE:.*]]:22:12: error: undefined identifier 'notdeclared'
NOERR-NOT: unexpected character
*)

program p;
var x: integer;
begin x := notdeclared end.
