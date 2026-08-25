(*
RUN: %plang_ir /nonexistent_file_plang_test.pas > %t.out 2>&1; true
RUN: grep -c 'error: ' %t.out | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
COUNT:1
*)
