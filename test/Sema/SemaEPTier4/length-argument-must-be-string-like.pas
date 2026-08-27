(*
RUN: %plang_ep -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #261: length's argument must be string-like (EP §6.7.6.7), the
   same requirement substr/trim now enforce (see
   substr-argument-must-be-string-like.pas).  This had no check of its
   own at all: every argument type-checked, and CodeGen's fallback for
   anything it does not recognize as string-shaped calls libc strlen on
   the raw value reinterpreted as a pointer. *)
program p; var i, n: integer; begin i := 42; n := length(i) end.

(*
CHECK: 'integer' cannot be an argument of length; it must be char or string
*)
