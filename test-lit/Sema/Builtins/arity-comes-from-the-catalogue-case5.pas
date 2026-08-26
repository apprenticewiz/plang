(*
RUN: %plang_ep -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p; var s: string(8); begin s := substr('abc') end.

(*
CHECK: 'substr' expects 2 or 3 argument(s), got 1
*)
