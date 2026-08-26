(*
RUN: %plang -dump-ast %s
*)

program p;
var r : integer;
function sq(x : integer) : integer;
begin sq := x * x end;
begin r := sq(3) end.
