(*
RUN: %plang_ep -dump-ast %s
*)

module Vector interface;
  export function Scale(x: real; k: integer): real;
end.
module Vector;
  function Scale(x: real; k: integer): real;
  begin Scale := x * k end;
end.
program p;
  import Vector;
var r: real;
begin
  r := Scale(2.0, 3)
end.
