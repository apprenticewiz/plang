(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

begin

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Begin
*)
