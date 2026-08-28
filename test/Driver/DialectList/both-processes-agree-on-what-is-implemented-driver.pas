(* The driver process reports an unimplemented -std= dialect (delphi) by
   naming the implemented dialects, iso7185, iso10206 and turbo, in that
   order. *)

(*
RUN: %plang_ir -std=delphi nosuchfile.pas > %t.out 2>&1; true
RUN: FileCheck %s < %t.out
*)

(*
CHECK-DAG: not yet implemented
CHECK-DAG: iso7185, iso10206, turbo
*)
