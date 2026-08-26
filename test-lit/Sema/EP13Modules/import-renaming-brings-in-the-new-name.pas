(*
RUN: %plang_ep -dump-ast %s
*)

module M;
  function f(x: integer): integer; begin f := x end;
end.
program p;
  import M (f => h);
var v: integer;
begin v := h(1) end.
