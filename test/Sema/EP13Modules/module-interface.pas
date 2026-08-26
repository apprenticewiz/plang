(*
RUN: split-file %s %t.dir
RUN: %plang_ep -dump-ast %t.dir/test.pas
*)

//--- test.pas
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
