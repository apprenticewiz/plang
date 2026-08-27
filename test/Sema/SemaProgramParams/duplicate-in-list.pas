(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(*
ISO §6.10: a name may not appear twice in a program's own parameter
list (issue #292).
*)

program t(f, f); begin end.

(*
CHECK: duplicate parameter name
*)
