(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #261: abs's argument has to be integer-type or real-type (ISO
   §6.6.6.2).  isOrdinal() also admits boolean, char and enumerations, so
   this used to type abs(true) as boolean rather than being rejected. *)
program p; var b1, b2: boolean; begin b1 := true; b2 := abs(b1) end.

(*
CHECK: 'abs' requires a numeric argument, got 'boolean'
*)
