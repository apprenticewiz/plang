(*
RUN: split-file %s %t.dir
RUN: %plang_ep -dump-ast %t.dir/test.pas
*)

//--- test.pas
module M;
  function f(x: integer): integer; begin f := x end;
end.
program p;
  import M (f => h);
var v: integer;
begin v := h(1) end.
