(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(*
ISO §6.10: every program-parameter besides the required 'input' and
'output' must be given a defining declaration in the program block.
'f' names no declaration anywhere in this program (issue #292).
*)

program t(f); begin end.

(*
CHECK: never declared
*)
