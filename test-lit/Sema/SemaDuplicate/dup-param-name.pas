(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p; procedure f(a, a : integer); begin end; begin end.

(*
CHECK: a
*)
