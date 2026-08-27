(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
var
  a : array [1..3000000000] of integer;
begin
  a[1] := 0
end.

(*
CHECK: too large to be a global variable
*)
