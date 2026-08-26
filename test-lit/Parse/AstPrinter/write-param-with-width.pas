(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck %s
*)

program p; var i:integer; begin write(i:8) end.

(*
CHECK: (write-param i :8)
*)
