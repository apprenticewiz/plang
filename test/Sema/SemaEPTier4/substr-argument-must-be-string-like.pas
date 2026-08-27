(*
RUN: %plang_ep -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #261: substr's first argument must be string-like (EP §6.7.6.7).
   Every shape that failed both of the recognized cases (var-string,
   packed-array-of-char) fell all the way through to "return TyStr" with
   no diagnostic at all, so substr(i, 1, 2) compiled silently and reached
   a CodeGen path with no call to lower it to. *)
program p; var i: integer; s: string(10); begin i := 42; s := substr(i, 1, 2) end.

(*
CHECK: 'integer' cannot be an argument of substr; it must be char or string
*)
