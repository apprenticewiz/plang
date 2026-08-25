(*
Cataloged as a warning, so it has a -W name derived from the DiagID
like every other warning, and can be turned off on its own.  This half
silences it with its own -W name.
*)

(*
RUN: %plang_ir -fnot-a-real-option -Wno-unrecognized-argument /dev/null > %t.out 2>&1; true
RUN: FileCheck --allow-empty --check-prefix=OUT-ABSENT %s < %t.out
*)

(*
OUT-ABSENT-NOT: unrecognized argument
*)
