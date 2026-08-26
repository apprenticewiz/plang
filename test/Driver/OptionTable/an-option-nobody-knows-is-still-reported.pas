(*
RUN: %plang_ir -fnot-a-real-option /dev/null > %t.out 2>&1; true
RUN: FileCheck --check-prefix=OUT %s < %t.out
*)

(*
OUT: unrecognized argument
*)
