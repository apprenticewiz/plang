(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

program p; var i:integer; begin   case i of 1..3: writeln; otherwise writeln end end.

(*
CHECK: (case i
CHECK: 1..3
CHECK: (otherwise
*)
