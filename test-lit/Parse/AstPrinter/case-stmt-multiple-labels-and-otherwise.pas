(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

program p; var i:integer; begin   case i of 5,7: writeln; otherwise writeln end end.

(*
CHECK: (arm (5 7)
CHECK: (otherwise
*)
