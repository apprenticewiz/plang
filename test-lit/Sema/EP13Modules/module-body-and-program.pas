(*
RUN: %plang_ep -dump-ast %s
*)

module M;
  function Scale(x: real; k: integer): real;
  begin Scale := x * k end;
end.
program p;
  import M;
var r: real;
begin
  r := Scale(2.0, 3)
end.
