(*
RUN: not %plang_ep -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program t; procedure p; var s: string(maxint); begin s := 'x' end; begin p end.

(*
CHECK: too large to be a local variable
*)
