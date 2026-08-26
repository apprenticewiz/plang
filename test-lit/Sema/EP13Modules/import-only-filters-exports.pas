(*
RUN: not %plang_ep -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

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
