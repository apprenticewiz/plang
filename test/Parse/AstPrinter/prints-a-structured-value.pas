(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

program p; type pt = record x, y: integer end; var v: pt;
begin v := pt[x: 1; y: 2] end.

(*
CHECK: (value pt (x : 1) (y : 2))
*)
