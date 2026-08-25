(*
RUN: not %plang_ir > %t.out 2>&1
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
*)

(*
CHECK:plang: error: no input files
*)
