(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

program p; var b:boolean; begin b := true and_then false end.

(*
CHECK: (and_then true false)
*)
