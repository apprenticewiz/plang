(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
procedure f(x : integer); begin end;
begin f(3.14) end.

(*
CHECK: x
*)
