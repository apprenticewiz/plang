(*
RUN: %plang_ep -dump-ast %s
*)

module M;
  function f(x: integer): integer; begin f := x end;
  function g(x: integer): integer; begin g := x end;
end.
program p;
  import M only (f, g);
var v: integer;
begin v := f(1) + g(2) end.
