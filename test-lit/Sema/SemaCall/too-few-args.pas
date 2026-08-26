(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
procedure f(x : integer); begin end;
begin f end.

(*
CHECK: 1
*)
