(*
RUN: %plang -dump-ast %s
*)

program p;
label 1;
var x : integer;
begin goto 1; 1: x := 0 end.
