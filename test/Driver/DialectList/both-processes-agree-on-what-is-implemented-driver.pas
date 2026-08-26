(* The driver process reports an unimplemented -std= dialect (turbo) by
   naming the implemented dialects, iso7185 and iso10206, in that order. *)

(*
RUN: %plang_ir -std=turbo nosuchfile.pas > %t.out 2>&1; true
RUN: FileCheck %s < %t.out
*)

(*
CHECK-DAG: not yet implemented
CHECK-DAG: iso7185, iso10206
*)
