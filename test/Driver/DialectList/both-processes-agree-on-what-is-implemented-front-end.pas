(* The front end reports an unimplemented -std= dialect (turbo) by naming
   the implemented dialects, iso7185 and iso10206, in that order -- the
   same two, in the same order, from the same list as the driver process. *)

(*
RUN: %plang_ir -pc1 -std=turbo nosuchfile.pas > %t.out 2>&1; true
RUN: FileCheck %s < %t.out
*)

(*
CHECK-DAG: not yet implemented
CHECK-DAG: iso7185, iso10206
*)
