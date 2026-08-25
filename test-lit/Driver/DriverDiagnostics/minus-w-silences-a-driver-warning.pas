(*
RUN: %plang_ir -fnot-a-real-option -w /dev/null > %t.out 2>&1; true
RUN: FileCheck --allow-empty --check-prefix=OUT-ABSENT %s < %t.out
*)

(*
OUT-ABSENT-NOT: unrecognized argument
*)
