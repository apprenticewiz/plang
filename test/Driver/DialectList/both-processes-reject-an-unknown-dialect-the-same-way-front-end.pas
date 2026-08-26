(* The front end rejects an unknown -std= dialect by naming every dialect
   that does exist, the same way the driver process does. *)

(*
RUN: %plang_ir -pc1 -std=nonesuch nosuchfile.pas > %t.out 2>&1; true
RUN: FileCheck %s < %t.out
*)

(*
CHECK-DAG: iso7185
CHECK-DAG: iso10206
CHECK-DAG: fpc
CHECK-DAG: delphi
CHECK-DAG: turbo
*)
