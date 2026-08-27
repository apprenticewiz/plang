(*
RUN: %plang_ep -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #261: EP §6.7.6.5's step count k is always of type integer,
   whatever the first argument's type is -- succ(x, k) does not walk k
   steps through x's own type the way succ(x) walks one.  The first
   argument's ordinal check (see succ-two-arg-ep.pas for the accepted
   form) never looked at the second one at all, so succ(i, c) silently
   walked ord(c) steps instead of being rejected. *)
program p; var i: integer; c: char; begin i := 5; c := 'a'; i := succ(i, c) end.

(*
CHECK: the second argument of 'succ' must be an integer, got 'char'
*)
