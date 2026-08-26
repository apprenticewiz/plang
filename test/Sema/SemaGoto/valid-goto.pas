(*
RUN: %plang -dump-ast %s
*)

program p;
label 10;
var x : integer;
begin goto 10; 10: x := 1 end.
