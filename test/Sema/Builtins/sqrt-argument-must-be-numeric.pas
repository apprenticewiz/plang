(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #261: sqrt (and, by the same unchecked fallthrough, sin, cos, exp,
   ln and arctan) accepted any argument type at all and always returned
   real -- 'a' has no square root, so sqrt('a') should be refused rather
   than silently typed as real. *)
program p; var c: char; r: real; begin c := 'a'; r := sqrt(c) end.

(*
CHECK: 'sqrt' requires a numeric argument, got 'char'
*)
