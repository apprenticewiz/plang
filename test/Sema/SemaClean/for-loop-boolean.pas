(*
RUN: %plang -dump-ast %s
*)

program p; var b : boolean; x : integer;
begin for b := false to true do x := 1 end.
