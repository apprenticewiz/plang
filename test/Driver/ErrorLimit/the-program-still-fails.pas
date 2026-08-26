(*
RUN: not %plang -ferror-limit=1 %s -o %t
*)

program p;
begin a:=1; b:=2; c:=3 end.
