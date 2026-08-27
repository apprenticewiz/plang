(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

xy

(*
Issue #285: SourceManager indexed only '\n' when it split a buffer into
lines, so a bare CR (no LF at all -- classic Mac OS's line ending, still
seen from files round-tripped through older Mac tooling) was invisible to
it and merged the line it ends into whatever line came next.  The '\r'
between x and y below is the only line break in this file that is not a
plain '\n' -- everything else (this comment included) stays ordinary so
lit itself, which splits RUN/CHECK lines on a literal '\n', keeps reading
the file normally. Without the fix, y is column 3 of line 5 (the byte
right after "x\r" on what SourceManager still thinks is one line);
with it, y starts its own line 6, same as it would after a plain '\n'.

CHECK: 5:1: Identifier "x"
CHECK-NEXT: 6:1: Identifier "y"
*)
