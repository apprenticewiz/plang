(*
RUN: not %plang_ir -fnot-a-real-option -Werror /dev/null > %t.out 2>&1
RUN: FileCheck --check-prefix=OUT %s < %t.out
*)

(*
OUT: plang: error: unrecognized argument
*)
