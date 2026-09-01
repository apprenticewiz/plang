(*
Issue #303: DiagnosticPrinter::printHeadline escaped the "file:line:col:"
prefix (a filename from argv) but never the message body itself, so a raw
control byte the scanner quotes back at you -- "unexpected character:
'<byte>'" -- reached stderr as itself instead of as text. A raw ESC (0x1B)
in that position can rewrite what the terminal shows for the rest of the
line, the same threat class as the already-fixed filename/locale-tag sinks,
just a different sink. The source line below carries a literal ESC byte
right after the assignment; %plang_ir's "unexpected character: '<byte>'"
message must show it as \x1b, not as the byte itself.

RUN: not %plang_ir -dump-tokens %s 2>%t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: unexpected character: '\x1b'
*)

program p; begin x := 1  end.
