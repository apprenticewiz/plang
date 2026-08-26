(*
RUN: split-file %s %t.dir
RUN: not %plang_ep -dump-ast %t.dir/test.pas
*)

//--- test.pas
module M interface;
  export M = (f..g);
  function f: integer;
  function g: integer;
end.
program p;
begin end.
