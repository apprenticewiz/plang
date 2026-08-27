(*
RUN: %plang_ep -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #261: card (EP §6.7.6.3) is the cardinality of a set, so its
   argument must be one -- CodeGen's lowering (population-count on the
   set's own bit-vector representation) reads whatever value is there
   regardless, so card(i) silently population-counted i's bit pattern
   instead of being rejected. *)
program p; var i, n: integer; begin i := 5; n := card(i) end.

(*
CHECK: 'card' requires a set argument, got 'integer'
*)
