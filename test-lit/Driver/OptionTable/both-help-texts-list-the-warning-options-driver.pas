(*
usagePC1 used to omit -Wno-, -Wall and -I although the front end took
all three.  Both texts are rendered from the shared table now.  This half
checks the driver's --help text.
*)

(*
RUN: %plang_ir --help > %t.out 2>&1; true
RUN: FileCheck --check-prefix=OUT %s < %t.out
*)

(*
OUT-DAG: -Wno-<warning>
OUT-DAG: -Wall
OUT-DAG: -I<dir>
*)
