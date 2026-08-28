(*
Lowercase hex digits are accepted too, matching EP's own nondecimal literal
and scanHexLiteral's std::tolower fold.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

$ff

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "255"
*)
