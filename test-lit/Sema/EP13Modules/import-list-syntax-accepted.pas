(*
RUN: split-file %s %t.dir
RUN: %plang_ep -dump-ast %t.dir/test.pas
*)

//--- test.pas
module M;
  function f(x: integer): integer; begin f := x end;
  function g(x: integer): integer; begin g := x end;
end.
program p;
  import M only (f, g);
var v: integer;
begin v := f(1) + g(2) end.
