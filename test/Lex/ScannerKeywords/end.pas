(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

end

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: End
*)
