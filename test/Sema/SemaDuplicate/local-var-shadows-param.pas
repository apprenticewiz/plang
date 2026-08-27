(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(*
ISO §6.2.2: a procedure's formal-parameter-list and its block are one
region, so a local declaration repeating a parameter's name is a
duplicate declaration, not a shadow of it (issue #289).
*)

program p; procedure f(x : integer); var x : real; begin x := 2.5 end; begin end.

(*
A bare one-letter check would also match the pre-existing (and
unrelated) unused-parameter warning this procedure earns on its own, so
this looks for the duplicate-declaration wording specifically -- the
one diagnostic only the fix for #289 emits.
CHECK: duplicate declaration of 'x'
*)
