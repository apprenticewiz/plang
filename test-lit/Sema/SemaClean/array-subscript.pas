(*
RUN: %plang -dump-ast %s
*)

program p;
var a : array[1..10] of integer;
    i : integer;
begin a[i] := 0 end.
