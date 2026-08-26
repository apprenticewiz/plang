(*
RUN: %plang_ir --help-warnings > %t.out 2>&1; true
RUN: FileCheck --check-prefix=OUT %s < %t.out
*)

(*
OUT: -Wno-unrecognized-argument
*)
