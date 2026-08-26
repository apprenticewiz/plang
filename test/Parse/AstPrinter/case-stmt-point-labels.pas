(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck %s
*)

program p; var i:integer; begin   case i of 1: writeln; 2: writeln end end.

(*
CHECK: (case i
CHECK: (arm (1)
CHECK: (arm (2)
*)
