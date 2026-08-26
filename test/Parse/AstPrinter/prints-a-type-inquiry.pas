(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

program p; var a: integer; b: type of a; begin end.

(*
CHECK: (type-of a)
*)
