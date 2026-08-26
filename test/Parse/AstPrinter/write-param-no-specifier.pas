(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck %s
*)

program p; var i:integer; begin write(i) end.

(*
CHECK-NOT: write-param
CHECK: (call write i)
*)
