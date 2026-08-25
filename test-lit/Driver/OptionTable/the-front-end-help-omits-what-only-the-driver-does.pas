(*
RUN: %plang_ir -pc1 --help > %t.out 2>&1; true
RUN: FileCheck --allow-empty --check-prefix=OUT-ABSENT %s < %t.out
*)

(*
OUT-ABSENT-NOT: -Xlinker
OUT-ABSENT-NOT: -save-temps
*)
