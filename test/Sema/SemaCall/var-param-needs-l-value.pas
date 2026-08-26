(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
procedure f(var x : integer); begin end;
begin f(42) end.

(*
CHECK: var
*)
