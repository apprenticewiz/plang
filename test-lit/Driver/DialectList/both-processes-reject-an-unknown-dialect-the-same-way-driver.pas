(* The driver process rejects an unknown -std= dialect by naming every
   dialect that does exist. *)

(*
RUN: %plang_ir -std=nonesuch nosuchfile.pas > %t.out 2>&1; true
RUN: FileCheck %s < %t.out
*)

(*
CHECK-DAG: iso7185
CHECK-DAG: iso10206
CHECK-DAG: fpc
CHECK-DAG: delphi
CHECK-DAG: turbo
*)
