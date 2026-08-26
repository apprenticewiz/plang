(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p;
procedure foo; begin end;
procedure foo; begin end;
begin end.

(*
CHECK: foo
*)
