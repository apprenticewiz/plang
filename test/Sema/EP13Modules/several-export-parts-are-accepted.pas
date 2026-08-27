(*
RUN: split-file %s %t.dir
RUN: %plang_ep -dump-ast %t.dir/test.pas
*)

//--- test.pas
module M interface;
  export A = (f);
  export B = (g);
  function f: integer;
  function g: integer;
end.
module M;
  function f: integer; begin f := 0 end;
  function g: integer; begin g := 0 end;
end.
program p;
begin end.
