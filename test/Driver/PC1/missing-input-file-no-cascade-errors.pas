(*
RUN: %plang_ir -pc1 /nonexistent_file_plang_test.pas > %t.out 2>&1; true
RUN: FileCheck --allow-empty --check-prefix=OUT-ABSENT %s < %t.out
*)

(*
OUT-ABSENT-NOT: expected
*)
