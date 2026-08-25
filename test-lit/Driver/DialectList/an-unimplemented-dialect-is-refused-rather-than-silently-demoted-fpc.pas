(* The front end mapped every -std= that was not iso10206 onto ISO 7185, so
   an unimplemented dialect would have compiled as standard Pascal and said
   nothing. It is rejected instead -- when fpc becomes real, the mapping
   carries it rather than dropping it. *)

(*
RUN: %plang_ir -pc1 -std=fpc nosuchfile.pas > %t.out 2>&1; true
RUN: FileCheck %s < %t.out
*)

(*
CHECK: not yet implemented
*)
