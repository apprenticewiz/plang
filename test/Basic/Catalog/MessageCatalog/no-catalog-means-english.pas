(*
RUN: not %plang 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: no input files
*)
