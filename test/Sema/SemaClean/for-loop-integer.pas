(*
RUN: %plang -dump-ast %s
*)

program p; var i, s : integer;
begin for i := 1 to 10 do s := s + i end.
