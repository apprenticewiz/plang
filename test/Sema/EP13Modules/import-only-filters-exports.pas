(*
RUN: split-file %s %t.dir
RUN: not %plang_ep -dump-ast %t.dir/test.pas 2> %t.err
RUN: FileCheck %s < %t.err
*)

//--- test.pas
module M;
  function f(x: integer): integer;
  begin f := x end;
  function g(x: integer): integer;
  begin g := x + 1 end;
end.
program p;
  import M only f;
var v: integer;
begin
  v := f(1);
  v := g(2)
end.

(*
CHECK: g
*)
