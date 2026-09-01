(*
Issue #303: the source line printSnippet quotes back at you under a
diagnostic was never escaped, so a raw control byte on the offending line
reached stderr as itself, the same threat as the already-fixed
"file:line:col:" prefix and message-body sinks, just a third sink. The
source line below carries a literal ESC byte right where the scanner
trips on it; both the re-echoed line and the caret beneath it must line up
under the *escaped* form ("\x1b" is 4 columns, not 1), not the raw byte.

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=CARET --strict-whitespace --match-full-lines %s < %t.err
*)

(*
ERR: error: unexpected character: '\x1b'
CARET:program p; var x: integer; begin x := 1 \x1b end.
CARET-NEXT:                                        ^
*)

program p; var x: integer; begin x := 1  end.
