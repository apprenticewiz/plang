(*
RUN: not %plang -dump-ast %s
*)

program p; var x : integer; begin while x do x := 0 end.
