(*
RUN: split-file %s %t.dir
RUN: %plang_ep -dump-ast %t.dir/test.pas
*)

//--- test.pas
module M;
  function f(x: integer): integer;
  begin f := x end;
end.
program p;
  import M qualified;
begin end.
