(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck %s
*)

program p; var r:real; begin write(r:10:2) end.

(*
CHECK: (write-param r :10 :2)
*)
