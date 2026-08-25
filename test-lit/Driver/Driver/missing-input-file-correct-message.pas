(*
RUN: %plang_ir /nonexistent_file_plang_test.pas > %t.out 2>&1; true
RUN: FileCheck --check-prefix=OUT %s < %t.out
*)

(*
OUT: no such file or directory
*)
