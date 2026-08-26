(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

program p; var r:real; begin r := 2.0 ** 8.0 end.

(*
CHECK: (** 2
*)
