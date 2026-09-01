(*
Issue #303: a nonexistent, argv-supplied filename containing a control byte
reaches DiagnosticPrinter::printHeadline's "no location" branch (the "no
such file or directory" diagnostic has no SourceLocation at all, unlike the
already-fixed resolved-source-location filename case in
a-control-character-in-the-source-filename-is-escaped-not-executed.pas).
Before the message-body escaping fix, D.Message there was passed straight
through, so the raw byte -- quoted into the message by the caller, not by
printHeadline's own escaped-filename formatting -- reached stderr as
itself.

RUN: not %plang "%t.dir/nonexistentfile.pas" -o %t.o 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: no such file or directory: '{{.*}}nonexistent\x1bfile.pas'
*)
