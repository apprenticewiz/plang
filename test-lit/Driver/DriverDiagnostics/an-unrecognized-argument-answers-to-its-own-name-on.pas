(*
Cataloged as a warning, so it has a -W name derived from the DiagID
like every other warning, and can be turned off on its own.  This half
leaves the warning on.
*)

(*
RUN: %plang_ir -fnot-a-real-option /dev/null > %t.out 2>&1; true
RUN: FileCheck --check-prefix=OUT %s < %t.out
*)

(*
OUT: plang: warning: unrecognized argument
*)
